"""Read existing content fields on UnityMain; no navigation or game-state writes."""
import argparse
import json
import pathlib
import shlex
import subprocess
import time
import uuid

from check_story import validate_main_probe, validate_compatibility

PACKAGE = "com.ProjectMoon.LimbusCompany"
TABLES = ("_bufList", "_keywordList", "_egoGiftData", "_danteNoteDesc")
PRIVATE = f"/data/user/0/{PACKAGE}/files/"
PUBLIC = f"/sdcard/Android/data/{PACKAGE}/files/"


def select_pid(candidates, compatibility):
    pid = compatibility.get("pid")
    if type(pid) is not int or pid <= 0 or pid not in candidates:
        raise ValueError("startup core PID is not a live game process")
    return pid


def validate(report, bridge, request, table, pid, ticks, require_complete=False):
    if (report.get("schema") != 1 or report.get("kind") != "content_readback"
            or report.get("request_id") != request or report.get("table") != table
            or report.get("pid") != pid or report.get("process_start_ticks") != ticks):
        return ["stale or unrecognized content report"]
    errors = validate_main_probe(bridge, request, "content_readback_bridge")
    if bridge.get("pid") != pid or report.get("native_callback_tid") != bridge.get("native_callback_tid"):
        errors.append("content readback and main-thread bridge do not share a live callback")
    if bridge.get("action_attempted") is not True or bridge.get("action_applied") is not True:
        errors.append("readback action did not complete")
    if (report.get("status") != "passed" or report.get("read_only") is not True
            or report.get("traversal_complete") is not True or report.get("snapshot_unchanged") is not True):
        errors.append("readback did not complete on an unchanged snapshot")
    for name in ("compared_fields", "equal_fields", "mismatched_fields", "invalid_runtime_rows",
                 "structure_mismatches", "missing_local_rows", "unsupported_source_fields", "unidentified_runtime_rows"):
        value = report.get(name)
        if type(value) is not int or value < 0:
            errors.append("invalid count: " + name)
    if errors:
        return errors
    if (report["compared_fields"] == 0 or report["equal_fields"] != report["compared_fields"]
            or report["mismatched_fields"] or report["invalid_runtime_rows"] or report["structure_mismatches"]):
        errors.append("runtime fields do not all equal their nonempty mapped translations")
    if require_complete and any(report[name] for name in (
            "missing_local_rows", "unsupported_source_fields", "unidentified_runtime_rows")):
        errors.append("this table has missing, unsupported or unidentified content")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--table", choices=TABLES, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=20)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    if not 0 < args.timeout <= 60:
        parser.error("timeout must be in (0, 60]")
    adb = [args.adb, "-s", args.serial]

    def run(*words):
        return subprocess.run(adb + list(words), capture_output=True, check=True, timeout=15).stdout

    def root(command):
        raw = run("exec-out", "su -c " + shlex.quote(command + '; e=$?; printf "\n__LLC_EXIT__%s\n" "$e"'))
        data, code = raw.rsplit(b"\n__LLC_EXIT__", 1)
        if int(code.strip()):
            raise RuntimeError("device command failed")
        return data

    candidates = [int(value) for value in run("shell", "pidof", PACKAGE).split()]
    compatibility = json.loads(root("cat " + PUBLIC + "llc_compatibility.json"))
    pid = select_pid(candidates, compatibility)
    def identity():
        stat = root(f"cat /proc/{pid}/stat").decode()
        cmd = root(f"cat /proc/{pid}/cmdline").split(b"\0", 1)[0].decode()
        if cmd != PACKAGE:
            raise RuntimeError("PID no longer belongs to the game")
        return stat.rsplit(")", 1)[1].split()[19]
    ticks = identity()
    if validate_compatibility(compatibility, pid, ticks):
        raise RuntimeError("startup core approval does not match the live process")
    request = uuid.uuid4().hex
    root(f"printf '%s\\n%s\\n' {request} {args.table} > {PRIVATE}llc_content_check")
    deadline = time.monotonic() + args.timeout
    report = bridge = None
    while time.monotonic() < deadline:
        try:
            report = json.loads(root("cat " + PUBLIC + "llc_content_state.json"))
            bridge = json.loads(root("cat " + PUBLIC + "llc_content_bridge.json"))
            if report.get("request_id") == bridge.get("request_id") == request:
                break
        except (ValueError, RuntimeError):
            pass
        time.sleep(.25)
    if not report or not bridge or identity() != ticks:
        raise RuntimeError("no correlated report from the original live game process")
    errors = validate(report, bridge, request, args.table, pid, ticks, args.require_complete)
    result = {"report": report, "bridge": bridge, "validation_errors": errors}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
