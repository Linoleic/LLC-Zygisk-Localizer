import contextlib
import hashlib
import importlib.util
import io
import json
import pathlib
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("run_probe", pathlib.Path(__file__).parents[1] / "tools/run_probe.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FakeDevice:
    def __init__(self, normal, probe, fail_copy=False, fail_restore=False, status="passed", statuses=None):
        self.normal = normal
        self.probe = probe
        self.files = {path: normal for path in module.DESTINATIONS}
        self.commands = []
        self.running_probe = False
        self.fail_copy = fail_copy
        self.fail_restore = fail_restore
        self.status = status
        self.statuses = list(statuses or [])

    def run(self, command, **kwargs):
        self.commands.append(command)
        args = command[3:]
        output, error, code = b"", b"", 0
        if args[0] == "push":
            self.files[args[2]] = hashlib.sha256(pathlib.Path(args[1]).read_bytes()).hexdigest()
        elif args[:3] == ["shell", "am", "force-stop"]:
            self.running_probe = False
        elif args[:3] == ["shell", "am", "start"]:
            self.running_probe = self.files[module.DESTINATIONS[0]] == self.probe
            output = b"Status: ok\n"
        elif args[0] == "exec-out":
            raw = args[1][len("su -c '"):-1]
            words = shlex.split(raw)
            if words[0] == "sha256sum":
                output = (self.files[words[1]] + "  " + words[1]).encode()
            elif words[0] == "stat":
                output = b"10345:10345\n"
            elif words[:2] == ["ls", "-Zd"]:
                output = b"u:object_r:app_data_file:s0:c89,c257,c512,c768 core.so\n"
            elif words[0] == "cp":
                destination = words[words.index("mv") + 2]
                if self.fail_copy and self.files[words[1]] == self.probe and destination == module.DESTINATIONS[1]:
                    self.fail_copy = False
                    code, error = 1, b"simulated copy failure"
                elif self.fail_restore and self.files[words[1]] == self.normal and destination == module.DESTINATIONS[1]:
                    code, error = 1, b"simulated restoration failure"
                else:
                    self.files[destination] = self.files[words[1]]
            elif words[0] == "cat" and words[1].endswith("/cmdline"):
                output = module.PACKAGE.encode() + b"\0"
            elif words[0] == "cat" and words[1].endswith("/maps"):
                output = ("1000-2000 r-xp 0 00:00 1 " + module.DESTINATIONS[2] + "\n").encode()
            elif words[0] == "cat":
                if self.running_probe:
                    status = self.statuses.pop(0) if self.statuses else self.status
                    output = json.dumps({"schema": 1, "kind": "dataset_sandbox_probe", "pid": 77, "status": status}).encode()
                else:
                    code = 1
        return subprocess.CompletedProcess(command, code, output, error)


class ProbeRollbackTests(unittest.TestCase):
    def execute(self, directory, device):
        argv = ["run_probe.py", "--serial", "test-device", "--probe", str(directory / "probe.so"),
                "--normal-core", str(directory / "normal.so"), "--output-dir", str(directory / "evidence")]
        with patch.object(sys, "argv", argv), patch.object(module.subprocess, "run", device.run), contextlib.redirect_stdout(io.StringIO()):
            return module.main()

    def fixture(self, directory, **options):
        (directory / "normal.so").write_bytes(b"normal core")
        (directory / "probe.so").write_bytes(b"diagnostic core")
        return FakeDevice(hashlib.sha256(b"normal core").hexdigest(), hashlib.sha256(b"diagnostic core").hexdigest(), **options)

    def test_success_restores_every_loading_path(self):
        with tempfile.TemporaryDirectory() as name:
            directory = pathlib.Path(name)
            device = self.fixture(directory)
            self.assertEqual(self.execute(directory, device), 0)
            self.assertTrue(all(device.files[path] == device.normal for path in module.DESTINATIONS))
            self.assertTrue(json.loads((directory / "evidence/build.json").read_bytes())["restored"])

    def test_partial_deployment_failure_restores_every_path(self):
        with tempfile.TemporaryDirectory() as name:
            directory = pathlib.Path(name)
            device = self.fixture(directory, fail_copy=True)
            with self.assertRaisesRegex(RuntimeError, "simulated copy failure"):
                self.execute(directory, device)
            self.assertTrue(all(device.files[path] == device.normal for path in module.DESTINATIONS))

    def test_failed_diagnostic_still_restores(self):
        with tempfile.TemporaryDirectory() as name:
            directory = pathlib.Path(name)
            device = self.fixture(directory, status="failed")
            self.assertEqual(self.execute(directory, device), 1)
            self.assertTrue(all(device.files[path] == device.normal for path in module.DESTINATIONS))

    def test_waiting_report_is_not_treated_as_finished(self):
        with tempfile.TemporaryDirectory() as name:
            directory = pathlib.Path(name)
            device = self.fixture(directory, statuses=["waiting_for_tables", "passed"])
            self.assertEqual(self.execute(directory, device), 0)
            self.assertEqual(json.loads((directory / "evidence/report.json").read_bytes())["status"], "passed")

    def test_preflight_mismatch_does_not_stop_game_or_push(self):
        with tempfile.TemporaryDirectory() as name:
            directory = pathlib.Path(name)
            device = self.fixture(directory)
            device.files[module.DESTINATIONS[1]] = "different"
            with self.assertRaisesRegex(RuntimeError, "differs from deployed"):
                self.execute(directory, device)
            self.assertFalse(any(command[3] in ("push", "shell") for command in device.commands))

    def test_restore_failure_still_attempts_other_paths_and_keeps_backup(self):
        with tempfile.TemporaryDirectory() as name:
            directory = pathlib.Path(name)
            device = self.fixture(directory, fail_restore=True)
            with self.assertRaisesRegex(RuntimeError, "restoration incomplete"):
                self.execute(directory, device)
            self.assertEqual(device.files[module.DESTINATIONS[0]], device.normal)
            self.assertEqual(device.files[module.DESTINATIONS[2]], device.normal)
            manifest = json.loads((directory / "evidence/build.json").read_bytes())
            self.assertFalse(manifest["restored"])
            self.assertIn(manifest["retained_restore_file"], device.files)


if __name__ == "__main__":
    unittest.main()
