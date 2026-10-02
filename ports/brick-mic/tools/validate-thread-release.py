#!/usr/bin/env python3
"""Check whether unsubscribe releases one allowlisted test thread's writer.

No model turns are started. Only an archived test thread proven by a successful
shared-session probe report is unarchived/resumed/archived. Existing desktop tasks
and global settings are unused.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import time


_spec = importlib.util.spec_from_file_location(
    "brick_mic_shared_probe", Path(__file__).with_name("validate-shared-session.py"))
shared = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(shared)


def allowlisted_thread(report):
    """Require provenance and a completed archive receipt before any RPC starts."""
    if not isinstance(report, dict) or report.get("probe_completed") is not True:
        raise shared.ProbeError("source_probe_not_completed")
    for field in ("real_user_tasks_accessed", "forks_created"):
        if type(report.get(field)) is not int or report[field] != 0:
            raise shared.ProbeError("source_probe_scope_not_safe")
    context = report.get("shared_context")
    if (not isinstance(context, dict) or context.get("ephemeral") is not False
            or context.get("separate_server_processes") is not True):
        raise shared.ProbeError("source_test_context_not_verified")
    thread_id = shared.probe.safe_id(context.get("threadId"))
    if not thread_id:
        raise shared.ProbeError("source_test_thread_id_invalid")
    receipts = report.get("archive_cleanup")
    if (not isinstance(receipts, list) or not any(
            isinstance(receipt, dict) and receipt.get("threadId") == thread_id
            and receipt.get("archived") is True for receipt in receipts)):
        raise shared.ProbeError("source_test_archive_receipt_missing")
    return thread_id


class ReleaseServer(shared.AppServer):
    def __init__(self, binary, cwd, test_thread_id):
        super().__init__(binary, cwd)
        self.test_thread_id = test_thread_id
        self.closed_at = None

    def dispatch(self, message):
        if (message.get("method") == "thread/closed"
                and message.get("params", {}).get("threadId") == self.test_thread_id):
            self.closed_at = time.monotonic()
        super().dispatch(message)


def loaded(server, timeout=5):
    result = server.request("thread/loaded/list", {"limit": 200}, timeout=timeout)
    return server.test_thread_id in result.get("data", [])


def error_report(server, error):
    result = {"error": str(error) if isinstance(error, shared.ProbeError) else type(error).__name__}
    if server and server.last_rpc_error:
        result["rpc_error"] = dict(server.last_rpc_error)
    return result


def run(args, report, thread_id):
    owned, servers = {thread_id}, []
    with tempfile.TemporaryDirectory(prefix="brick-mic-thread-release-") as cwd:
        unarchived = False
        try:
            a = ReleaseServer(args.codex, cwd, thread_id)
            servers.append(a)
            b = ReleaseServer(args.codex, cwd, thread_id)
            servers.append(b)
            shared.initialize(a)
            shared.initialize(b)
            response = a.request("thread/unarchive", {"threadId": thread_id})
            if response.get("thread", {}).get("id") != thread_id:
                raise shared.ProbeError("unarchive_returned_wrong_test_id")
            unarchived = True
            report["unarchived_allowlisted_test"] = True
            report["a_resume"] = shared.attempt_resume(a, thread_id, cwd, owned)
            if not report["a_resume"]["ok"]:
                raise shared.ProbeError("test_thread_a_resume_failed")
            report["a_loaded_before_unsubscribe"] = loaded(a)
            report["b_before_unsubscribe"] = shared.attempt_resume(b, thread_id, cwd, owned)
            baseline = report["b_before_unsubscribe"].get("rpc_error", {}).get("category")
            if baseline not in {"active_writer", "writer_lock"}:
                raise shared.ProbeError("baseline_exclusive_writer_not_observed")
            started = time.monotonic()
            response = a.request("thread/unsubscribe", {"threadId": thread_id})
            status = response.get("status")
            report["unsubscribe"] = {"status": status if status in
                                     {"notLoaded", "notSubscribed", "unsubscribed"} else "other"}
            deadline = started + 45
            latest_loaded = True
            observations = 0
            observed_release = False
            while time.monotonic() < deadline:
                remaining = deadline - time.monotonic()
                try:
                    latest_loaded = loaded(a, timeout=min(5, remaining))
                    observations += 1
                except shared.ProbeError as error:
                    report["observation_error"] = error_report(a, error)
                    break
                if a.closed_at is not None or not latest_loaded:
                    observed_release = True
                    break
                # Drain asynchronous events while waiting, with no active model.
                shared.bounded_observe(a, min(0.5, max(0, deadline - time.monotonic())))
            report["release_observation"] = {
                "thread_closed_event": a.closed_at is not None,
                "a_loaded_after_unsubscribe": latest_loaded,
                "observation_count": observations,
                "elapsed_seconds": round(time.monotonic() - started, 3),
                "release_observed": observed_release,
                "a_process_alive": a.process.poll() is None,
                "b_process_alive": b.process.poll() is None,
                "wait_limit_seconds": 45}
            if observed_release:
                report["b_retry_after_observed_release"] = shared.attempt_resume(b, thread_id, cwd, owned)
                report["unsubscribe_handoff_verified"] = (
                    report["b_retry_after_observed_release"]["ok"] and a.process.poll() is None)
            else:
                report["unsubscribe_handoff_verified"] = False
                report["release_observation"]["timeout_is_not_permanent_unsupported_evidence"] = True
            if not report["unsubscribe_handoff_verified"]:
                a.close()
                servers.remove(a)
                report["a_closed_for_fallback"] = a.process.poll() is not None
                report["b_after_a_process_exit"] = shared.attempt_resume(b, thread_id, cwd, owned)
            report["model_turns_started"] = 0
        finally:
            cleanup_errors = []
            for server in reversed(servers):
                try:
                    server.close()
                except (shared.ProbeError, OSError, ValueError) as error:
                    cleanup_errors.append(error_report(server, error))
            report["writer_process_cleanup"] = {"all_exited": all(server.process.poll() is not None for server in servers)}
            if cleanup_errors:
                report["writer_process_cleanup"]["errors"] = cleanup_errors
            if unarchived:
                cleanup = shared.AppServer(args.codex, cwd)
                try:
                    shared.initialize(cleanup)
                    cleanup.request("thread/archive", {"threadId": thread_id})
                    report["archive_cleanup"] = {"threadId": thread_id, "archived": True}
                except (shared.ProbeError, OSError) as error:
                    report["archive_cleanup"] = {"threadId": thread_id, "archived": False,
                                                 **error_report(cleanup, error)}
                finally:
                    cleanup.close()
                    report["archive_cleanup"]["cleanup_process_exited"] = cleanup.process.poll() is not None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex", required=True)
    parser.add_argument("--source-report", default="build/brick-mic/appserver-validation/shared-session.json",
                        help="Successful shared-session probe report with its test thread archive receipt")
    parser.add_argument("--output", default="build/brick-mic/appserver-validation/thread-release.json")
    args = parser.parse_args()
    report = {"schema_version": 1, "model_turns_started": 0,
              "real_user_tasks_accessed": 0, "approvals_accepted": 0}
    try:
        source = json.loads(Path(args.source_report).expanduser().read_text(encoding="utf-8"))
        thread_id = allowlisted_thread(source)
        report["threadId"] = thread_id
        report["source_test_provenance_verified"] = True
        run(args, report, thread_id)
        report["probe_completed"] = True
    except (shared.ProbeError, OSError, ValueError, TypeError) as error:
        report["probe_completed"] = False
        report.update(error_report(None, error))
    except KeyboardInterrupt:
        report["probe_completed"] = False
        report["error"] = "interrupted"
    output = Path(args.output).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    output.chmod(0o600)
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["probe_completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
