#!/usr/bin/env python3
"""Probe a separate Codex app-server without controlling desktop sessions.

Default mode only lists/reads metadata. --live creates one ephemeral test thread,
checks reply/completion events, and declines a harmless command approval. Reports
contain IDs and statistics, never existing task titles, prompts, replies or keys.
Python standard library only; no persistent settings or authentication changes.
"""

from __future__ import annotations

import argparse
import ast
from collections import Counter
import json
import os
from pathlib import Path
import re
import secrets
import selectors
import shlex
import signal
import subprocess
import tempfile
import time


SOURCE_KINDS = ["cli", "vscode", "exec", "appServer", "unknown"]
DISABLED_FEATURES = ["hooks", "apps", "plugins", "browser_use", "computer_use", "multi_agent"]
UUID = re.compile(r"^[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$")
STATUS_NAMES = {"notLoaded", "idle", "systemError", "active", "running", "completed",
                "failed", "interrupted", "waiting", "waitingForApproval", "needsAttention"}


class ProbeError(Exception):
    """Keep server messages out of diagnostics: they can include private content."""


def rpc_error_category(error):
    """Inspect server diagnostics in memory, retain only a fixed safe category."""
    if not isinstance(error, dict):
        return "other"
    message = error.get("message", "")
    message = message.lower() if isinstance(message, str) else ""
    if "paginated" in message or "pagination" in message:
        return "paginated"
    if error.get("code") == -32601 or any(word in message for word in
            ("unsupported", "not supported", "not implemented", "unknown method", "method not found")):
        return "unsupported"
    if any(word in message for word in ("unauthorized", "authentication", "credentials", "login", "sign in", "token expired")):
        return "auth"
    if any(word in message for word in ("not found", "no rollout", "does not exist", "missing thread", "unknown thread")):
        return "not_found"
    if any(word in message for word in ("permission denied", "forbidden", "access denied")):
        return "permission"
    if any(word in message for word in ("rate limit", "quota", "usage limit")):
        return "rate_limit"
    if error.get("code") == -32602:
        return "invalid_params"
    return "other"


def status_name(value):
    name = value.get("type") if isinstance(value, dict) else value
    return name if isinstance(name, str) and name in STATUS_NAMES else "other"


def safe_id(value):
    return value if isinstance(value, str) and UUID.fullmatch(value) else None


def load_inventory(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    rows = data.get("threads", []) if isinstance(data, dict) else data
    if not isinstance(rows, list):
        raise ProbeError("invalid_inventory")
    return {safe_id(row.get("id")): status_name(row.get("status"))
            for row in rows if isinstance(row, dict) and safe_id(row.get("id"))}


def harmless_command(command, marker):
    """Recognize only python3 -c with a single print of the exact random marker."""
    if not isinstance(command, str) or len(command) > 4096:
        return False
    try:
        parts = shlex.split(command)
        if len(parts) == 3 and Path(parts[0]).name in {"sh", "bash", "zsh"} and parts[1] in {"-c", "-lc"}:
            parts = shlex.split(parts[2])
        if len(parts) != 3 or Path(parts[0]).name != "python3" or parts[1] != "-c":
            return False
        tree = ast.parse(parts[2], mode="exec")
        if len(tree.body) != 1 or not isinstance(tree.body[0], ast.Expr):
            return False
        call = tree.body[0].value
        return (isinstance(call, ast.Call) and isinstance(call.func, ast.Name)
                and call.func.id == "print" and not call.keywords and len(call.args) == 1
                and isinstance(call.args[0], ast.Constant) and call.args[0].value == marker)
    except (ValueError, SyntaxError):
        return False


class AppServer:
    def __init__(self, binary, cwd):
        args = [binary, "app-server", "--listen", "stdio://"]
        for feature in DISABLED_FEATURES:
            args += ["-c", f"features.{feature}=false"]
        self.process = subprocess.Popen(args, cwd=cwd, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        start_new_session=True, bufsize=0)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ, "stdout")
        self.selector.register(self.process.stderr, selectors.EVENT_READ, "stderr")
        self.buffer = b""
        self.messages = []
        self.responses = {}
        self.next_id = 1
        self.stderr_bytes = 0
        self.notification_counts = Counter()
        self.server_request_counts = Counter()
        self.owned_thread = None
        self.active_turn = None
        self.approval_marker = None
        self.approval_records = []
        self.resolved_request_ids = set()
        self.turn_items = {}
        self.turn_results = {}
        self.turn_started_at = {}

    def send(self, message):
        try:
            wire = (json.dumps(message, separators=(",", ":")) + "\n").encode()
            self.process.stdin.write(wire)
            self.process.stdin.flush()
        except (OSError, BrokenPipeError, ValueError) as error:
            raise ProbeError("server_stdin_closed") from error

    def request(self, method, params, timeout=15):
        request_id = self.next_id
        self.next_id += 1
        self.send({"id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + timeout
        while request_id not in self.responses:
            self.pump(deadline)
        response = self.responses.pop(request_id)
        if "error" in response:
            code = response["error"].get("code") if isinstance(response["error"], dict) else None
            code = code if isinstance(code, int) else "unknown"
            category = rpc_error_category(response["error"])
            raise ProbeError(f"rpc_error:{method}:{code}:{category}")
        result = response.get("result")
        if not isinstance(result, dict):
            raise ProbeError(f"invalid_result:{method}")
        return result

    def pump(self, deadline):
        if self.messages:
            self.dispatch(self.messages.pop(0))
            return
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ProbeError("timeout")
        if not self.selector.get_map():
            raise ProbeError("server_streams_closed")
        ready = self.selector.select(min(remaining, 1))
        for key, _ in ready:
            try:
                chunk = os.read(key.fileobj.fileno(), 65536)
            except OSError as error:
                raise ProbeError("server_stream_read_error") from error
            if not chunk:
                self.selector.unregister(key.fileobj)
                continue
            if key.data == "stderr":
                # Drain to avoid deadlock; do not retain/log credentials or task text.
                self.stderr_bytes += len(chunk)
                continue
            self.buffer += chunk
            if len(self.buffer) > 32 * 1024 * 1024:
                raise ProbeError("server_line_too_large")
            while b"\n" in self.buffer:
                line, self.buffer = self.buffer.split(b"\n", 1)
                if not line.strip():
                    continue
                try:
                    message = json.loads(line)
                except (ValueError, UnicodeDecodeError) as error:
                    raise ProbeError("server_invalid_json") from error
                if not isinstance(message, dict):
                    raise ProbeError("server_invalid_message")
                self.messages.append(message)
        if self.messages:
            self.dispatch(self.messages.pop(0))
        elif self.process.poll() is not None and not self.selector.get_map():
            raise ProbeError("server_exited")

    def dispatch(self, message):
        method = message.get("method")
        if "id" in message and isinstance(method, str):
            self.handle_server_request(message)
            return
        if "id" in message:
            self.responses[message["id"]] = message
            return
        if not isinstance(method, str):
            return
        self.notification_counts[method] += 1
        params = message.get("params", {})
        if not isinstance(params, dict) or params.get("threadId") != self.owned_thread:
            return
        if method == "serverRequest/resolved":
            request_id = params.get("requestId")
            if isinstance(request_id, (int, str)):
                self.resolved_request_ids.add(request_id)
        elif method == "item/completed":
            turn_id = params.get("turnId")
            item = params.get("item", {})
            if not isinstance(turn_id, str) or not isinstance(item, dict):
                return
            # Only test-thread items are retained, never existing desktop content.
            self.turn_items.setdefault(turn_id, []).append(item)
        elif method == "turn/completed":
            turn = params.get("turn", {})
            if isinstance(turn, dict) and isinstance(turn.get("id"), str):
                self.turn_results[turn["id"]] = turn

    def handle_server_request(self, message):
        method, request_id = message["method"], message["id"]
        self.server_request_counts[method] += 1
        params = message.get("params", {})
        params = params if isinstance(params, dict) else {}
        if (method == "item/commandExecution/requestApproval"
                and params.get("threadId") == self.owned_thread and self.owned_thread):
            marker = self.approval_marker
            matched = bool(marker and harmless_command(params.get("command"), marker))
            self.approval_records.append({"threadId": self.owned_thread,
                                          "turnId": params.get("turnId"),
                                          "requestId": request_id,
                                          "itemId": params.get("itemId"),
                                          "command_matches_test_marker": matched,
                                          "decision": "decline"})
            # Even an unexpected command in our test thread is always declined.
            self.send({"id": request_id, "result": {"decision": "decline"}})
        elif (method == "item/fileChange/requestApproval"
              and params.get("threadId") == self.owned_thread and self.owned_thread):
            self.send({"id": request_id, "result": {"decision": "decline"}})
        else:
            # No approval, authorization, dynamic tool or credentials RPC is granted.
            self.send({"id": request_id, "error": {"code": -32601,
                       "message": "Unsupported request in isolated validation client"}})

    def wait_turn(self, turn_id, started_at, timeout=60):
        self.active_turn = turn_id
        self.turn_started_at[turn_id] = started_at
        deadline = started_at + timeout
        try:
            while turn_id not in self.turn_results:
                self.pump(deadline)
        except ProbeError:
            self.interrupt_owned_turn()
            raise
        finally:
            self.active_turn = None
        turn = self.turn_results[turn_id]
        items = self.turn_items.get(turn_id, [])
        messages = [item for item in items if item.get("type") == "agentMessage"]
        result = {"threadId": self.owned_thread, "turnId": turn_id,
                  "status": status_name(turn.get("status")),
                  "elapsed_seconds": round(time.monotonic() - started_at, 3),
                  "agent_message_events": len(messages),
                  "command_execution_events": sum(item.get("type") == "commandExecution" for item in items),
                  "turn_completed_event": True}
        return result, messages

    def interrupt_owned_turn(self):
        if self.owned_thread and self.active_turn:
            try:
                self.request("turn/interrupt", {"threadId": self.owned_thread,
                                                "turnId": self.active_turn}, timeout=5)
            except ProbeError:
                pass

    def settle_approval_receipts(self, turn_id, timeout=3):
        """A completion notification alone does not prove the RPC was resolved."""
        deadline = time.monotonic() + timeout
        while any(record["requestId"] not in self.resolved_request_ids
                  for record in self.approval_records if record.get("turnId") == turn_id):
            try:
                self.pump(deadline)
            except ProbeError as error:
                if str(error) == "timeout":
                    return
                raise

    def close(self):
        self.interrupt_owned_turn()
        try:
            self.process.stdin.close()
        except (OSError, ValueError):
            pass
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(self.process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.process.wait(timeout=3)
        self.selector.close()
        for stream in (self.process.stdout, self.process.stderr):
            stream.close()


def paginated_list(server, method, base_params, maximum=200):
    rows, cursor, pages = [], None, 0
    seen_cursors = set()
    while len(rows) < maximum:
        params = dict(base_params, limit=min(50, maximum - len(rows)))
        if cursor:
            params["cursor"] = cursor
        response = server.request(method, params)
        data = response.get("data", [])
        if not isinstance(data, list):
            raise ProbeError(f"invalid_list:{method}")
        rows.extend(data[:maximum - len(rows)])
        pages += 1
        cursor = response.get("nextCursor")
        if not cursor:
            break
        if not isinstance(cursor, str) or cursor in seen_cursors:
            raise ProbeError(f"invalid_cursor:{method}")
        seen_cursors.add(cursor)
    return rows, pages, bool(cursor)


def run_probe(args, report):
    inventory = load_inventory(args.inventory)
    with tempfile.TemporaryDirectory(prefix="brick-mic-appserver-probe-") as cwd:
        server = AppServer(args.codex, cwd)
        try:
            started = time.monotonic()
            initialized = server.request("initialize", {
                "clientInfo": {"name": "brick_mic_validation", "title": "Brick Mic validation", "version": "0.1"},
                "capabilities": {"experimentalApi": True}})
            server.send({"method": "initialized", "params": {}})
            report["initialize"] = {"ok": True,
                                    "elapsed_seconds": round(time.monotonic() - started, 3),
                                    "user_agent_present": isinstance(initialized.get("userAgent"), str)}
            rows, pages, more = paginated_list(server, "thread/list", {
                "sourceKinds": SOURCE_KINDS, "useStateDbOnly": True,
                "sortKey": "updated_at", "sortDirection": "desc", "archived": False})
            listed = {safe_id(row.get("id")): row for row in rows
                      if isinstance(row, dict) and safe_id(row.get("id"))}
            matched_ids = sorted(set(listed) & set(inventory))
            report["desktop_inventory"] = {
                "inventory_count": len(inventory), "listed_count": len(listed), "pages": pages,
                "limit_reached_with_more": more, "source_kinds": SOURCE_KINDS,
                "use_state_db_only": True, "matched_count": len(matched_ids),
                "missing_ids": sorted(set(inventory) - set(listed)),
                "matches": [{"id": thread_id, "desktop_status": inventory[thread_id],
                             "separate_server_status": status_name(listed[thread_id].get("status"))}
                            for thread_id in matched_ids],
                "statuses_are_separate_server_runtime": True}
            loaded, loaded_pages, loaded_more = paginated_list(server, "thread/loaded/list", {})
            loaded_ids = {thread_id for thread_id in loaded if safe_id(thread_id)}
            report["loaded_threads"] = {"count": len(loaded_ids), "pages": loaded_pages,
                                        "limit_reached_with_more": loaded_more,
                                        "desktop_match_ids": sorted(loaded_ids & set(inventory))}
            # Metadata read does not resume/load/subscribe to the desktop's runtime.
            if inventory:
                read_id = matched_ids[0] if matched_ids else next(iter(inventory))
                try:
                    response = server.request("thread/read", {"threadId": read_id, "includeTurns": False})
                    thread = response.get("thread", {})
                    report["desktop_metadata_read"] = {
                        "ok": isinstance(thread, dict) and thread.get("id") == read_id,
                        "threadId": read_id, "includeTurns": False,
                        "turn_count": len(thread.get("turns", [])) if isinstance(thread, dict) else 0,
                        "full_history_verified": False, "live_subscription_verified": False}
                except ProbeError as error:
                    report["desktop_metadata_read"] = {"ok": False, "error": str(error)}
            if args.live:
                live_probe(server, cwd, inventory, report)
            else:
                report["live_probe"] = {"requested": False, "model_started": False}
        finally:
            report["protocol"] = {"notification_counts": dict(server.notification_counts),
                                  "server_request_counts": dict(server.server_request_counts),
                                  "stderr_bytes_discarded": server.stderr_bytes}
            server.close()
            report["child_process_cleanup"] = {"exited": server.process.poll() is not None}


def live_probe(server, cwd, inventory, report):
    report["live_probe"] = {"requested": True, "model_started": False}
    response = server.request("thread/start", {
        "cwd": cwd, "ephemeral": True, "sandbox": "read-only",
        "approvalPolicy": "untrusted", "approvalsReviewer": "user",
        "developerInstructions": "This is an isolated protocol test. Follow the exact test input. "
                                 "Never use network, read private data, modify files, or call tools "
                                 "except the exact print-only command explicitly requested by the test."})
    thread = response.get("thread", {})
    thread_id = safe_id(thread.get("id")) if isinstance(thread, dict) else None
    if not thread_id or thread_id in inventory or thread.get("ephemeral") is not True:
        raise ProbeError("test_thread_not_new_ephemeral")
    server.owned_thread = thread_id
    report["live_probe"].update({"threadId": thread_id, "ephemeral": True,
                                "desktop_task_mutated": False,
                                "approval_policy": response.get("approvalPolicy"),
                                "approvals_reviewer": response.get("approvalsReviewer")})
    if response.get("approvalPolicy") != "untrusted" or response.get("approvalsReviewer") != "user":
        raise ProbeError("test_approval_policy_not_applied")
    reply_marker = "BRICK_MIC_REPLY_" + secrets.token_hex(8)
    started = time.monotonic()
    response = server.request("turn/start", {"threadId": thread_id,
        "input": [{"type": "text", "text": "Reply with exactly this marker and nothing else: " + reply_marker}],
        "effort": "low"}, timeout=15)
    turn_id = response.get("turn", {}).get("id")
    if not isinstance(turn_id, str):
        raise ProbeError("test_turn_id_missing")
    report["live_probe"]["model_started"] = True
    reply_result, messages = server.wait_turn(turn_id, started)
    reply_result["exact_marker_received"] = any(message.get("text", "").strip() == reply_marker for message in messages)
    reply_result["passed"] = (reply_result["status"] == "completed"
                              and reply_result["exact_marker_received"])
    report["reply_test"] = reply_result
    marker = "BRICK_MIC_APPROVAL_" + secrets.token_hex(8)
    server.approval_marker = marker
    command = "python3 -c " + shlex.quote("print(" + repr(marker) + ")")
    started = time.monotonic()
    response = server.request("turn/start", {"threadId": thread_id,
        "input": [{"type": "text", "text": "Run exactly the following command once with your shell tool. "
                   "Do not substitute any command, inspect files, or request additional permissions. "
                   "If denied, do not retry; reply DENIED. Command: " + command}],
        "effort": "low", "approvalPolicy": "untrusted", "approvalsReviewer": "user"}, timeout=15)
    turn_id = response.get("turn", {}).get("id")
    if not isinstance(turn_id, str):
        raise ProbeError("approval_turn_id_missing")
    approval_result, _ = server.wait_turn(turn_id, started)
    server.settle_approval_receipts(turn_id)
    records = [record for record in server.approval_records if record.get("turnId") == turn_id]
    commands = {item.get("id"): item for item in server.turn_items.get(turn_id, [])
                if item.get("type") == "commandExecution" and isinstance(item.get("id"), str)}
    verified_records = []
    for record in records:
        item_status = commands.get(record["itemId"], {}).get("status")
        item_status = item_status if item_status in {"inProgress", "completed", "failed", "declined"} else "missing"
        verified_records.append(dict(record, resolved=record["requestId"] in server.resolved_request_ids,
                                     command_item_status=item_status))
    passed = (approval_result["status"] == "completed" and bool(verified_records)
              and all(record["command_matches_test_marker"] and record["resolved"]
                      and record["command_item_status"] == "declined"
                      for record in verified_records))
    approval_result.update({"approval_requests": len(records),
                            "marker_matched_declined_requests": sum(record["command_matches_test_marker"] for record in records),
                            "decisions": sorted(set(record["decision"] for record in records)),
                            "requests": verified_records,
                            "command_items": [{"id": item_id, "status": item.get("status")}
                                              for item_id, item in commands.items()],
                            "passed": passed})
    if not records:
        approval_result["not_verified_reason"] = "no_command_approval_request_observed"
    elif not passed:
        approval_result["not_verified_reason"] = "declined_item_and_resolved_receipt_not_fully_verified"
    report["approval_test"] = approval_result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex", required=True, help="Codex CLI binary, preferably the desktop-bundled CLI")
    parser.add_argument("--inventory", required=True, help="Sanitized JSON with desktop thread IDs/statuses")
    parser.add_argument("--output", required=True, help="Sanitized report JSON")
    parser.add_argument("--live", action="store_true", help="Run model only in a newly created ephemeral test thread")
    args = parser.parse_args()
    report = {"schema_version": 1, "mode": "live" if args.live else "read-only",
              "separate_child_server": True, "disabled_features": DISABLED_FEATURES,
              "existing_desktop_turns_started": 0, "approvals_accepted": 0,
              "desktop_runtime_control_verified": False}
    try:
        run_probe(args, report)
        report["probe_completed"] = True
    except (ProbeError, OSError, ValueError, TypeError) as error:
        report["probe_completed"] = False
        report["error"] = str(error) if isinstance(error, ProbeError) else type(error).__name__
    except KeyboardInterrupt:
        report["probe_completed"] = False
        report["error"] = "interrupted"
    output = Path(args.output).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    output.chmod(0o600)
    print(json.dumps({"probe_completed": report["probe_completed"], "mode": report["mode"]}))
    return 0 if report["probe_completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
