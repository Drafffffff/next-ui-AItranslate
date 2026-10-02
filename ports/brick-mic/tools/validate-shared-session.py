#!/usr/bin/env python3
"""Validate persisted context and active-turn boundaries across separate servers.

Only this script's newly created persistent test threads are read/resumed/archived.
All approvals are declined. No desktop tasks, GUI, credentials, or global config
are edited. JSON output contains only test IDs, counts, and marker-match booleans.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import secrets
import shlex
import tempfile
import time


_spec = importlib.util.spec_from_file_location(
    "brick_mic_appserver_probe", Path(__file__).with_name("validate-appserver.py"))
probe = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(probe)
ProbeError = probe.ProbeError


class AppServer(probe.AppServer):
    """Classify errors here without retaining server diagnostic values."""

    def __init__(self, binary, cwd):
        super().__init__(binary, cwd)
        self.last_rpc_error = None

    def request(self, method, params, timeout=15):
        self.last_rpc_error = None
        request_id = self.next_id
        self.next_id += 1
        self.send({"id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + timeout
        while request_id not in self.responses:
            self.pump(deadline)
        response = self.responses.pop(request_id)
        if "error" in response:
            error = response["error"]
            error = error if isinstance(error, dict) else {}
            message = error.get("message", "")
            message = message.lower() if isinstance(message, str) else ""
            code = error.get("code")
            code = code if isinstance(code, int) else "unknown"
            if "active writer" in message or "writer lease" in message:
                category = "active_writer"
            elif "writer lock" in message:
                category = "writer_lock"
            elif code == -32602 or any(value in message for value in
                    ("unknown field", "invalid parameter", "invalid params", "missing field")):
                category = "schema"
            else:
                category = probe.rpc_error_category(error)
            safe_data_keys = {"threadId", "turnId", "itemId", "writerId", "activeWriter", "owner",
                              "code", "reason", "type", "kind", "details", "retryable", "error", "message"}
            data = error.get("data")
            self.last_rpc_error = {"method": method, "code": code, "category": category,
                                   "data_keys": sorted(set(data) & safe_data_keys) if isinstance(data, dict) else []}
            raise ProbeError(f"rpc_error:{method}:{code}:{category}")
        result = response.get("result")
        if not isinstance(result, dict):
            raise ProbeError(f"invalid_result:{method}")
        return result


def attempt_resume(server, thread_id, cwd, owned_ids):
    try:
        return dict(resume(server, thread_id, cwd, owned_ids), ok=True)
    except ProbeError as error:
        result = {"ok": False, "error": str(error)}
        if server.last_rpc_error:
            result["rpc_error"] = dict(server.last_rpc_error)
            category = server.last_rpc_error["category"]
            result["safe_explanation"] = {
                "active_writer": "Another loaded app-server instance owns this test thread's writer lease.",
                "writer_lock": "The server could not acquire the test thread's writer lock.",
                "not_found": "No recoverable rollout or thread was found for this test ID.",
                "schema": "The requested parameters do not match this server's accepted schema.",
                "unsupported": "This server does not support the requested operation."
            }.get(category, "The operation failed; no raw server diagnostic is retained.")
        return result


def initialize(server):
    server.request("initialize", {
        "clientInfo": {"name": "brick_mic_shared_session_validation", "version": "0.1"},
        "capabilities": {"experimentalApi": True}})
    server.send({"method": "initialized", "params": {}})


def new_thread(server, cwd, owned_ids):
    response = server.request("thread/start", {
        "cwd": cwd, "ephemeral": False, "sandbox": "read-only",
        "approvalPolicy": "untrusted", "approvalsReviewer": "user",
        "developerInstructions": "This is an isolated context/protocol test. Remember the test markers "
                                 "from conversation context only. Never read files, logs, network, "
                                 "or private data. Never call any tool except the exact print-only "
                                 "python3 command explicitly requested by the approval test. "
                                 "If that command is denied, do not retry; finish with DENIED."})
    thread = response.get("thread", {})
    thread_id = probe.safe_id(thread.get("id"))
    if not thread_id or thread_id in owned_ids or thread.get("ephemeral") is not False:
        raise ProbeError("new_persistent_test_thread_not_verified")
    owned_ids.add(thread_id)
    server.owned_thread = thread_id
    if response.get("approvalPolicy") != "untrusted" or response.get("approvalsReviewer") != "user":
        raise ProbeError("test_approval_policy_not_applied")
    return thread_id


def require_owned(thread_id, owned_ids):
    if thread_id not in owned_ids:
        raise ProbeError("refused_non_test_thread")


def resume(server, thread_id, cwd, owned_ids):
    require_owned(thread_id, owned_ids)
    result = server.request("thread/resume", {
        "threadId": thread_id, "excludeTurns": True, "cwd": cwd,
        "sandbox": "read-only", "approvalPolicy": "untrusted", "approvalsReviewer": "user"})
    if result.get("thread", {}).get("id") != thread_id:
        raise ProbeError("resume_changed_thread_id")
    server.owned_thread = thread_id
    return {"same_thread_id": True,
            "separate_server_status": probe.status_name(result.get("thread", {}).get("status"))}


def history(server, thread_id, owned_ids, markers):
    require_owned(thread_id, owned_ids)
    started = time.monotonic()
    fallback_error = None
    try:
        result = server.request("thread/read", {"threadId": thread_id, "includeTurns": True})
        thread = result.get("thread", {})
        if thread.get("id") != thread_id:
            raise ProbeError("history_changed_thread_id")
        turns = thread.get("turns", [])
        if not isinstance(turns, list):
            raise ProbeError("invalid_test_history")
        items = [item for turn in turns for item in turn.get("items", [])]
        method = "thread/read"
        pages = 1
    except ProbeError as error:
        # Newer paginated histories reject deprecated full-history hydration.
        fallback_error = str(error)
        turns, pages, more = probe.paginated_list(server, "thread/turns/list", {
            "threadId": thread_id, "itemsView": "full", "sortDirection": "asc"})
        entries, item_pages, items_more = probe.paginated_list(server, "thread/items/list", {
            "threadId": thread_id, "sortDirection": "asc"})
        if more or items_more:
            raise ProbeError("test_history_not_fully_paged")
        items = [entry.get("item", {}) for entry in entries if isinstance(entry, dict)]
        method = "thread/turns/list+thread/items/list"
        pages += item_pages
    serialized = json.dumps(items, ensure_ascii=False)
    report = {"threadId": thread_id, "method": method, "turn_count": len(turns),
              "item_count": len(items), "pages": pages,
              "marker_matches": {name: value in serialized for name, value in markers.items()},
              "elapsed_seconds": round(time.monotonic() - started, 3),
              "model_context_verified_by_this_read": False}
    if fallback_error:
        report["full_read_error"] = fallback_error
    return report


def final_answer(messages):
    finals = [item.get("text", "") for item in messages if item.get("phase") == "final_answer"]
    return finals[-1] if finals and isinstance(finals[-1], str) else None


def ask(server, thread_id, text, expected, owned_ids):
    require_owned(thread_id, owned_ids)
    if server.owned_thread != thread_id:
        raise ProbeError("test_thread_not_resumed_in_this_server")
    started = time.monotonic()
    response = server.request("turn/start", {"threadId": thread_id,
        "input": [{"type": "text", "text": text}], "effort": "low",
        "approvalPolicy": "untrusted", "approvalsReviewer": "user"})
    turn_id = response.get("turn", {}).get("id")
    if not isinstance(turn_id, str):
        raise ProbeError("test_turn_id_missing")
    result, messages = server.wait_turn(turn_id, started)
    answer = final_answer(messages)
    tokens = answer.split() if answer is not None else []
    result.update({"final_answer_event_present": answer is not None,
                   "exact_markers_received": tokens == expected,
                   "passed": result["status"] == "completed" and tokens == expected,
                   "recall_prompt_contains_expected_values": any(marker in text for marker in expected)})
    return result


class HeldApprovalServer(AppServer):
    """Hold only the exact test command; never grant an approval."""

    def __init__(self, binary, cwd):
        super().__init__(binary, cwd)
        self.hold_approval = True
        self.held_requests = []

    def handle_server_request(self, message):
        params = message.get("params", {})
        if (self.hold_approval and message.get("method") == "item/commandExecution/requestApproval"
                and isinstance(params, dict) and params.get("threadId") == self.owned_thread
                and self.approval_marker and probe.harmless_command(params.get("command"), self.approval_marker)):
            self.server_request_counts[message["method"]] += 1
            record = {"threadId": self.owned_thread, "turnId": params.get("turnId"),
                      "requestId": message["id"], "itemId": params.get("itemId"),
                      "command_matches_test_marker": True, "decision": "decline"}
            self.approval_records.append(record)
            self.held_requests.append(record)
        else:
            super().handle_server_request(message)

    def decline_held(self):
        self.hold_approval = False
        for record in self.held_requests:
            if (record["requestId"] not in self.resolved_request_ids
                    and record["turnId"] not in self.turn_results):
                self.send({"id": record["requestId"], "result": {"decision": "decline"}})
        self.held_requests.clear()

    def close(self):
        try:
            self.decline_held()
        except ProbeError:
            pass
        super().close()


def bounded_observe(server, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            server.pump(deadline)
        except ProbeError as error:
            if str(error) != "timeout":
                raise


def active_turn_boundary(binary, cwd, owned_ids, servers, report):
    d = HeldApprovalServer(binary, cwd)
    servers.append(d)
    initialize(d)
    thread_id = new_thread(d, cwd, owned_ids)
    marker = "BRICK_MIC_APPROVAL_" + secrets.token_hex(8)
    d.approval_marker = marker
    command = "python3 -c " + shlex.quote("print(" + repr(marker) + ")")
    started = time.monotonic()
    response = d.request("turn/start", {"threadId": thread_id,
        "input": [{"type": "text", "text": "Execute exactly this print-only command once with your shell tool. "
                   "If an approval is denied, do not retry; reply DENIED. Command: " + command}],
        "effort": "low", "approvalPolicy": "untrusted", "approvalsReviewer": "user"})
    turn_id = response.get("turn", {}).get("id")
    if not isinstance(turn_id, str):
        raise ProbeError("boundary_turn_id_missing")
    d.active_turn = turn_id
    boundary = {"threadId": thread_id, "original_turnId": turn_id, "approvals_accepted": 0,
                "other_instance_turn_start_count": 0, "approval_held": False}
    report["active_turn_boundary"] = boundary
    try:
        while not d.held_requests and turn_id not in d.turn_results:
            d.pump(started + 60)
    except ProbeError as error:
        boundary["hold_error"] = str(error)
        d.interrupt_owned_turn()
        return
    if not d.held_requests:
        boundary["not_verified_reason"] = "no_command_approval_request_observed"
        d.active_turn = None
        return
    boundary["approval_held"] = True
    e = AppServer(binary, cwd)
    servers.append(e)
    initialize(e)
    try:
        boundary["other_instance_history"] = history(e, thread_id, owned_ids, {"command_marker": marker})
    except ProbeError as error:
        boundary["other_instance_history"] = {"ok": False, "error": str(error)}
    boundary["other_instance_resume"] = attempt_resume(e, thread_id, cwd, owned_ids)
    for name, method, params in [
        ("steer", "turn/steer", {"threadId": thread_id, "expectedTurnId": turn_id,
                                  "input": [{"type": "text", "text": "After the pending test command is denied, "
                                             "do not retry it. Reply DENIED only."}]}),
        ("interrupt", "turn/interrupt", {"threadId": thread_id, "turnId": turn_id})]:
        try:
            result = e.request(method, params, timeout=10)
            boundary[name] = {"rpc_ok": True, "same_turn_id_in_response": result.get("turnId") == turn_id,
                              "effect_on_original_instance_verified": False}
        except ProbeError as error:
            boundary[name] = {"rpc_ok": False, "error": str(error),
                              "effect_on_original_instance_verified": False}
            if e.last_rpc_error:
                boundary[name]["rpc_error"] = dict(e.last_rpc_error)
        bounded_observe(d, 1)
        original = d.turn_results.get(turn_id, {})
        resolved_before_decline = any(record["requestId"] in d.resolved_request_ids for record in d.held_requests)
        interrupted_before_decline = original.get("status") == "interrupted"
        if name == "interrupt":
            boundary[name].update({"original_turn_interrupted_before_owner_decline": interrupted_before_decline,
                                   "original_approval_resolved_before_owner_decline": resolved_before_decline,
                                   "effect_on_original_instance_verified": interrupted_before_decline or resolved_before_decline})
    d.decline_held()
    final, _ = d.wait_turn(turn_id, started)
    d.settle_approval_receipts(turn_id)
    command_items = {item.get("id"): item for item in d.turn_items.get(turn_id, [])
                     if item.get("type") == "commandExecution"}
    records = [dict(record, resolved=record["requestId"] in d.resolved_request_ids,
                    command_item_status=command_items.get(record["itemId"], {}).get("status", "missing"))
               for record in d.approval_records if record.get("turnId") == turn_id]
    final.update({"requests": records, "decline_path_verified": final["status"] == "completed"
                  and bool(records) and all(record["resolved"] and record["command_item_status"] == "declined"
                                            for record in records)})
    boundary["owner_decline_result"] = final
    d.active_turn = None


def archive_tests(binary, cwd, owned_ids, report):
    cleanup = AppServer(binary, cwd)
    results = []
    try:
        initialize(cleanup)
        for thread_id in sorted(owned_ids):
            try:
                cleanup.request("thread/archive", {"threadId": thread_id})
                results.append({"threadId": thread_id, "archived": True})
            except ProbeError as error:
                results.append({"threadId": thread_id, "archived": False, "error": str(error)})
    finally:
        cleanup.close()
        report["archive_cleanup"] = results


def run(args, report):
    owned_ids, servers = set(), []
    with tempfile.TemporaryDirectory(prefix="brick-mic-shared-session-") as cwd:
        try:
            a = AppServer(args.codex, cwd)
            servers.append(a)
            b = AppServer(args.codex, cwd)
            servers.append(b)
            initialize(a)
            initialize(b)
            thread_id = new_thread(a, cwd, owned_ids)
            marker_a = "BRICK_MIC_A_" + secrets.token_hex(8)
            marker_b = "BRICK_MIC_B_" + secrets.token_hex(8)
            core = {"threadId": thread_id, "ephemeral": False,
                    "separate_server_processes": a.process.pid != b.process.pid,
                    "context_validation_mode": "cold_hand_off",
                    "hot_shared_runtime_verified": False}
            report["shared_context"] = core
            core["a_initial"] = ask(a, thread_id,
                "Remember this value as A-marker: " + marker_a + ". Reply with exactly that value. Use no tools.",
                [marker_a], owned_ids)
            core["b_full_history_before_resume"] = history(b, thread_id, owned_ids, {"a_marker": marker_a})
            core["b_hot_resume_while_a_loaded"] = attempt_resume(b, thread_id, cwd, owned_ids)
            # A successful metadata read does not release A's writer lease. Stop
            # its process before B's actual model turn; never force a second writer.
            a.close()
            servers.remove(a)
            core["a_exited_before_b_model_turn"] = a.process.poll() is not None
            core["b_resume_after_a_exit"] = attempt_resume(b, thread_id, cwd, owned_ids)
            if not core["b_resume_after_a_exit"]["ok"]:
                raise ProbeError("same_id_cold_handoff_to_b_failed")
            core["b_recall_a"] = ask(b, thread_id,
                "Recall A-marker from the earlier conversation and reply with exactly its value. "
                "Also remember this new value as B-marker: " + marker_b + ". Do not print B-marker now. Use no tools.",
                [marker_a], owned_ids)
            a2 = AppServer(args.codex, cwd)
            servers.append(a2)
            initialize(a2)
            core["a2_full_history_while_b_loaded"] = history(a2, thread_id, owned_ids, {"a_marker": marker_a, "b_marker": marker_b})
            core["a2_hot_resume_while_b_loaded"] = attempt_resume(a2, thread_id, cwd, owned_ids)
            b.close()
            servers.remove(b)
            core["b_exited_before_a2_model_turn"] = b.process.poll() is not None
            core["a2_resume_after_b_exit"] = attempt_resume(a2, thread_id, cwd, owned_ids)
            if not core["a2_resume_after_b_exit"]["ok"]:
                raise ProbeError("same_id_cold_handoff_to_a2_failed")
            core["a2_recall_b"] = ask(a2, thread_id,
                "Recall B-marker from the previous conversation. Reply with exactly its value. "
                "Use no tools and do not read files or logs.", [marker_b], owned_ids)
            a2.close()
            servers.remove(a2)
            core["all_previous_context_writers_exited_before_c"] = all(
                server.process.poll() is not None for server in (a, b, a2))
            c = AppServer(args.codex, cwd)
            servers.append(c)
            initialize(c)
            core["c_full_history"] = history(c, thread_id, owned_ids, {"a_marker": marker_a, "b_marker": marker_b})
            core["c_resume"] = attempt_resume(c, thread_id, cwd, owned_ids)
            if not core["c_resume"]["ok"]:
                raise ProbeError("same_id_cold_recovery_to_c_failed")
            core["c_cold_recall_a_b"] = ask(c, thread_id,
                "Recall A-marker and B-marker from our earlier conversation. Reply with exactly A-marker's value "
                "followed by B-marker's value separated by one space. Use no tools; do not read files or logs.",
                [marker_a, marker_b], owned_ids)
            core["passed"] = all(core[name]["passed"] for name in
                                 ("a_initial", "b_recall_a", "a2_recall_b", "c_cold_recall_a_b"))
            core["cold_hand_off_verified"] = core["passed"]
            if not args.core_only:
                active_turn_boundary(args.codex, cwd, owned_ids, servers, report)
        finally:
            close_errors = []
            for server in reversed(servers):
                try:
                    server.close()
                except (ProbeError, OSError, ValueError) as error:
                    close_errors.append(str(error) if isinstance(error, ProbeError) else type(error).__name__)
            report["child_process_cleanup"] = {"all_exited": all(server.process.poll() is not None for server in servers)}
            if close_errors:
                report["child_process_cleanup"]["errors"] = close_errors
            if owned_ids:
                try:
                    archive_tests(args.codex, cwd, owned_ids, report)
                except (ProbeError, OSError) as error:
                    report["archive_cleanup_error"] = str(error) if isinstance(error, ProbeError) else type(error).__name__


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--core-only", action="store_true", help="Skip the held-approval active-turn boundary test")
    args = parser.parse_args()
    report = {"schema_version": 1, "real_user_tasks_accessed": 0, "forks_created": 0,
              "approvals_accepted": 0, "disabled_features": probe.DISABLED_FEATURES}
    try:
        run(args, report)
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
    print(json.dumps({"probe_completed": report["probe_completed"],
                      "shared_context_passed": report.get("shared_context", {}).get("passed", False)}))
    return 0 if report["probe_completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
