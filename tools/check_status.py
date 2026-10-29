"""Read the live LLC localization status report through ADB."""
import argparse
import json
import pathlib
import re
import shlex
import subprocess
import sys
import time
import uuid

PACKAGE = "com.ProjectMoon.LimbusCompany"
TRIGGER = f"/data/user/0/{PACKAGE}/files/llc_status_check"
REPORT = f"/sdcard/Android/data/{PACKAGE}/files/llc_status.json"
TERMINAL_STAGES = {"disabled", "resource_unavailable", "api_unavailable"}
EXIT_MARKER = b"\n__LLC_EXIT__"


def validate_status(report, pid, start_ticks, request_id=None, allow_terminal=True):
    """Return errors if a status object is not for this process/request."""
    if not isinstance(report, dict):
        return ["status report is not a JSON object"]
    if report.get("schema") != 1 or report.get("kind") != "localization_status":
        return ["unrecognized status schema or kind"]
    if report.get("pid") != pid or report.get("process_start_ticks") != start_ticks:
        return ["status report belongs to a stale or different process"]
    runtime = report.get("runtime")
    stage = runtime.get("stage") if isinstance(runtime, dict) else None
    if stage == "initializing":
        return ["localization status is still initializing"]
    if request_id is not None and report.get("request_id") != request_id:
        if not (allow_terminal and stage in TERMINAL_STAGES):
            return ["status report belongs to an older request"]
    if stage not in {"initializing", "running", *TERMINAL_STAGES}:
        return ["status report has an unknown runtime stage"]
    for section in ("compatibility", "resources", "coverage", "updates"):
        if not isinstance(report.get(section), dict):
            return [f"status report is missing object section {section}"]
    return []


def process_start_ticks(stat_text):
    """Extract Linux /proc/PID/stat field 22 despite spaces in comm."""
    close = stat_text.rfind(") ")
    if close < 0:
        raise ValueError("malformed /proc/PID/stat")
    fields_after_comm = stat_text[close + 2:].split()
    if len(fields_after_comm) <= 19:
        raise ValueError("/proc/PID/stat has no start time")
    return fields_after_comm[19]


class Remote:
    """ADB wrapper with explicit remote exit status for every su command."""

    def __init__(self, adb="adb", serial="", timeout=10):
        self.adb = adb
        self.serial = serial
        self.timeout = timeout

    def call(self, *args, check=True):
        result = subprocess.run([self.adb, "-s", self.serial, *args], capture_output=True,
                                timeout=min(self.timeout, 5))
        if check and result.returncode:
            detail = result.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(detail or f"adb exited {result.returncode}")
        return result

    def root(self, command, check=True):
        marked = command + '; llc_exit=$?; printf "\\n__LLC_EXIT__%s\\n" "$llc_exit"'
        raw = self.call("exec-out", "su -c " + shlex.quote(marked), check=False).stdout
        try:
            data, status = raw.rsplit(EXIT_MARKER, 1)
            code = int(status.strip())
        except (ValueError, TypeError):
            raise RuntimeError("remote su response has no valid exit marker") from None
        if check and code:
            raise RuntimeError(f"remote command exited {code}: {command}")
        return data

    def read_process_start(self, pid):
        raw = self.root(f"cat /proc/{int(pid)}/stat")
        return process_start_ticks(raw.decode("utf-8", "replace"))

    def find_process(self):
        result = self.call("shell", "pidof", PACKAGE, check=False)
        if result.returncode not in (0, 1):
            raise RuntimeError(result.stderr.decode("utf-8", "replace").strip() or "pidof failed")
        for item in result.stdout.decode("ascii", "replace").split():
            if not item.isdigit():
                continue
            pid = int(item)
            try:
                cmdline = self.root(f"cat /proc/{pid}/cmdline").split(b"\0", 1)[0]
                maps = self.root(f"grep -F libllc_core.so /proc/{pid}/maps", check=False)
                if cmdline.decode("utf-8", "replace").strip() == PACKAGE and maps.strip():
                    return pid, self.read_process_start(pid)
            except RuntimeError:
                continue
        raise RuntimeError("target game process with libllc_core.so is not running")

    def trigger(self, request_id):
        if re.fullmatch(r"[0-9a-f]{32}", request_id) is None:
            raise ValueError("request_id must be 32 lowercase hexadecimal characters")
        quoted_id = shlex.quote(request_id)
        temporary = f"{TRIGGER}.{request_id}.next"
        quoted_temporary = shlex.quote(temporary)
        quoted_trigger = shlex.quote(TRIGGER)
        command = (
            f"umask 077; if (set -C; : > {quoted_temporary}); then "
            f"if printf %s {quoted_id} >> {quoted_temporary} "
            f"&& llc_status_owner=$(stat -c %u {shlex.quote(f'/data/user/0/{PACKAGE}')}) "
            f"&& test -n \"$llc_status_owner\" "
            f"&& chown \"$llc_status_owner\" {quoted_temporary} "
            f"&& mv -f {quoted_temporary} {quoted_trigger}; then :; "
            f"else llc_status_exit=$?; rm -f {quoted_temporary}; exit $llc_status_exit; fi; "
            "else exit 1; fi"
        )
        self.root(command)

    def read_report(self):
        return self.root("cat " + shlex.quote(REPORT), check=False)


def fetch_status(remote, timeout):
    pid, ticks = remote.find_process()
    request_id = uuid.uuid4().hex
    remote.trigger(request_id)
    deadline = time.monotonic() + timeout
    while True:
        try:
            raw = remote.read_report()
            candidate = json.loads(raw.decode("utf-8"))
            errors = validate_status(candidate, pid, ticks, request_id)
            if not errors:
                current_ticks = remote.read_process_start(pid)
                if current_ticks != ticks:
                    raise RuntimeError("target PID was reused while waiting for status")
                return candidate
            runtime = candidate.get("runtime") if isinstance(candidate, dict) else None
            stage = runtime.get("stage") if isinstance(runtime, dict) else None
            # Initialization is expected to become correlated; terminal reports can
            # be accepted immediately because the core no longer services requests.
            if stage in TERMINAL_STAGES and candidate.get("pid") == pid and candidate.get("process_start_ticks") == ticks:
                terminal_errors = validate_status(candidate, pid, ticks, request_id)
                if not terminal_errors:
                    current_ticks = remote.read_process_start(pid)
                    if current_ticks != ticks:
                        raise RuntimeError("target PID was reused while waiting for status")
                    return candidate
        except (ValueError, UnicodeError):
            pass
        if time.monotonic() >= deadline:
            raise RuntimeError("timed out waiting for a correlated localization status report")
        time.sleep(min(0.2, max(0, deadline - time.monotonic())))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=10)
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.timeout > 60:
        parser.error("--timeout must be in (0, 60]")
    try:
        report = fetch_status(Remote(args.adb, args.serial, args.timeout), args.timeout)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, ensure_ascii=False, separators=(",", ":")))
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
