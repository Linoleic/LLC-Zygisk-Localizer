import json
import subprocess
import unittest
from unittest import mock

from tools import check_status


def report(pid=123, ticks="456", request_id="req-1", stage="running", coverage_state="complete"):
    return {
        "schema": 1,
        "kind": "localization_status",
        "pid": pid,
        "process_start_ticks": ticks,
        "request_id": request_id,
        "runtime": {"stage": stage},
        "compatibility": {},
        "resources": {},
        "coverage": {"state": coverage_state},
        "updates": {"state": "not_checked"},
    }


class CheckStatusTests(unittest.TestCase):
    def test_rejects_old_request_and_old_pid(self):
        self.assertTrue(check_status.validate_status(report(request_id="old"), 123, "456", "req-1"))
        self.assertTrue(check_status.validate_status(report(pid=12), 123, "456", "req-1"))

    def test_rejects_reused_pid_with_different_start_ticks(self):
        self.assertTrue(check_status.validate_status(report(ticks="457"), 123, "456", "req-1"))

    def test_initializing_old_report_is_not_accepted_but_terminal_is(self):
        self.assertTrue(check_status.validate_status(report(request_id="old", stage="initializing"),
                                                     123, "456", "req-1"))
        self.assertTrue(check_status.validate_status(report(request_id="req-1", stage="initializing"),
                                                     123, "456", "req-1"))
        self.assertEqual([], check_status.validate_status(
            report(request_id="old", stage="disabled"), 123, "456", "req-1"))

    def test_start_ticks_parser_handles_parenthesized_comm(self):
        # Fields after ')' start at stat field 3; starttime is field 22 (index 19).
        stat = "123 (game (worker)) S " + " ".join(str(n) for n in range(3, 23))
        self.assertEqual("21", check_status.process_start_ticks(stat))

    def test_waits_through_initializing_report(self):
        pending = report(request_id="old", stage="initializing")

        class FakeRemote:
            def find_process(self):
                return 123, "456"

            def trigger(self, request_id):
                self.request_id = request_id

            def read_process_start(self, pid):
                return "456"

            def read_report(self):
                value = pending if not hasattr(self, "read_once") else report(request_id=self.request_id)
                self.read_once = True
                return json.dumps(value).encode()

        with mock.patch.object(check_status.time, "sleep"):
            result = check_status.fetch_status(FakeRemote(), 1)
        self.assertEqual("running", result["runtime"]["stage"])

    def test_accepts_terminal_report_without_request_id(self):
        terminal = report(request_id=None, stage="resource_unavailable")

        class FakeRemote:
            def find_process(self):
                return 123, "456"

            def trigger(self, request_id):
                pass

            def read_process_start(self, pid):
                return "456"

            def read_report(self):
                return json.dumps(terminal).encode()

        self.assertEqual(terminal, check_status.fetch_status(FakeRemote(), 1))

    def test_rejects_report_if_pid_was_reused_before_acceptance(self):
        class FakeRemote:
            def find_process(self):
                return 123, "456"

            def trigger(self, request_id):
                self.request_id = request_id

            def read_report(self):
                return json.dumps(report(request_id=self.request_id)).encode()

            def read_process_start(self, pid):
                return "789"

        with self.assertRaisesRegex(RuntimeError, "PID was reused"):
            check_status.fetch_status(FakeRemote(), 1)

    def test_partial_translation_is_a_successful_report(self):
        partial = report(coverage_state="partial_translation")
        self.assertEqual([], check_status.validate_status(partial, 123, "456", "req-1"))

    def test_root_parses_remote_status_even_when_adb_exit_is_zero(self):
        remote = check_status.Remote("adb", "device", 1)
        result = subprocess.CompletedProcess([], 0, b"operation failed\n__LLC_EXIT__7\n", b"")
        with mock.patch.object(remote, "call", return_value=result) as call:
            with self.assertRaisesRegex(RuntimeError, "remote command exited 7"):
                remote.root("false")
        command = call.call_args.args[-1]
        self.assertIn("__LLC_EXIT__", command)

    def test_root_returns_payload_when_marked_remote_exit_is_zero(self):
        remote = check_status.Remote("adb", "device", 1)
        result = subprocess.CompletedProcess([], 0, b"payload\n__LLC_EXIT__0\n", b"")
        with mock.patch.object(remote, "call", return_value=result):
            self.assertEqual(b"payload", remote.root("cat /x"))

    def test_trigger_uses_private_exclusive_request_specific_temp_and_atomic_move(self):
        request_id = "0123456789abcdef0123456789abcdef"
        remote = check_status.Remote("adb", "device", 1)
        with mock.patch.object(remote, "root") as root:
            remote.trigger(request_id)
        command = root.call_args.args[0]
        temporary = f"{check_status.TRIGGER}.{request_id}.next"
        self.assertIn("umask 077", command)
        self.assertIn("set -C", command)
        self.assertIn(f": > {temporary}", command)
        self.assertIn(f"printf %s {request_id} >> {temporary}", command)
        stat = f"stat -c %u /data/user/0/{check_status.PACKAGE}"
        chown = f'chown "$llc_status_owner" {temporary}'
        move = f"mv -f {temporary} {check_status.TRIGGER}"
        self.assertIn(stat, command)
        self.assertIn(chown, command)
        self.assertIn(move, command)
        self.assertLess(command.index(stat), command.index(chown))
        self.assertLess(command.index(chown), command.index(move))
        self.assertIn(f"&& {chown} && {move}", command)
        self.assertIn(f"rm -f {temporary}", command)
        self.assertNotIn(f"rm -f {check_status.TRIGGER};", command)

    def test_trigger_rejects_non_hex_request_id_before_remote_call(self):
        remote = check_status.Remote("adb", "device", 1)
        with mock.patch.object(remote, "root") as root:
            with self.assertRaisesRegex(ValueError, "32 lowercase hexadecimal"):
                remote.trigger("bad/request-id")
        root.assert_not_called()


if __name__ == "__main__":
    unittest.main()
