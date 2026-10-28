"""Request story snapshots or validated main-thread actions; no screenshots or navigation."""
import argparse
import json
import pathlib
import subprocess
import time
import uuid

PACKAGE = "com.ProjectMoon.LimbusCompany"
TRIGGER = f"/data/user/0/{PACKAGE}/files/llc_story_check"
REPORT = f"/sdcard/Android/data/{PACKAGE}/files/llc_story_state.json"


def validate_snapshot(report, request_id, require_story=False, require_data=False, require_body_cache=False, require_place_cache=False):
    errors = []
    if report.get("schema") != 1 or report.get("request_id") != request_id:
        return ["unrecognized schema or stale request_id"]
    if require_story or require_data or require_body_cache or require_place_cache:
        if report.get("status") != "observed":
            errors.append(f"active story required, got {report.get('status')}")
    if require_data and report.get("data_matches_resource") is not True:
        errors.append("runtime story fields do not all match the resource")
    if require_body_cache:
        current = report.get("current_data", {}).get("content")
        shown = report.get("display_fields", {}).get("tmp_serif")
        if not isinstance(current, str) or not current or shown != current:
            errors.append("tmp_serif cache does not exactly equal the current dialog content")
        if report.get("snapshot_unchanged") is not True:
            errors.append("story changed during observation")
    if require_place_cache:
        place = report.get("current_data", {}).get("place")
        shown = report.get("controller_texts", {}).get("_uiAnimator", {}).get("tmp_mapText")
        if not isinstance(place, str) or not place or shown != place:
            errors.append("tmp_mapText cache does not exactly equal this dialog's nonempty place")
        if report.get("snapshot_unchanged") is not True:
            errors.append("story changed during observation")
    return errors


def validate_main_probe(report, request_id, kind="main_thread_probe"):
    if report.get("schema") != 1 or report.get("request_id") != request_id or report.get("kind") != kind:
        return ["unrecognized probe schema or stale request_id"]
    if (report.get("status") != "passed" or report.get("delegate_abi_selftest") is not True
            or report.get("context_matches") is not True or not report.get("unity_main_id")
            or report.get("unity_main_id") != report.get("managed_callback_id")
            or not report.get("native_callback_tid") or not report.get("native_sender_tid")
            or report.get("native_callback_tid") == report.get("native_sender_tid")):
        return [f"{kind} failed: {report.get('error')}"]
    if kind == "story_refresh" and (report.get("action_attempted") is not True or report.get("action_applied") is not True):
        return ["story refresh was not applied"]
    return []


def validate_compatibility(report, pid, start_ticks):
    if (report.get("schema") != 1 or report.get("kind") != "compatibility"
            or report.get("pid") != pid or report.get("process_start_ticks") != start_ticks):
        return ["stale or unrecognized compatibility report"]
    digest = report.get("il2cpp_sha256")
    if (report.get("status") != "approved" or report.get("runtime_modifications_allowed") is not True
            or not isinstance(digest, str) or len(digest) != 64
            or any(c not in "0123456789abcdef" for c in digest)
            or digest != report.get("verified_il2cpp_sha256")):
        return [f"core modifications disabled: {report.get('status')} ({report.get('error')})"]
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--require-story", action="store_true")
    parser.add_argument("--probe-main-thread", action="store_true", help="test delegate ABI and Unity Post queue; no UI changes")
    parser.add_argument("--refresh-story", action="store_true", help="refresh the current idle normal dialog on Unity's main thread")
    parser.add_argument("--compatibility", action="store_true", help="read the startup native-build gate result; works when patching is disabled")
    parser.add_argument("--require-data", action="store_true")
    parser.add_argument("--require-body-cache", action="store_true",
                        help="strict raw TMP body-cache equality, not rendered pixels")
    parser.add_argument("--require-place-cache", action="store_true",
                        help="strict map-title cache equality; requires a dialog with a nonempty place")
    args = parser.parse_args()
    if sum((args.probe_main_thread, args.refresh_story, args.compatibility)) > 1:
        parser.error("choose one action/check mode")
    action = args.probe_main_thread or args.refresh_story
    if (action or args.compatibility) and (args.require_story or args.require_data or args.require_body_cache or args.require_place_cache):
        parser.error("main-thread actions cannot be combined with story assertions")
    if args.timeout <= 0 or args.timeout > 60:
        parser.error("--timeout must be in (0, 60]")
    adb = [args.adb, "-s", args.serial]

    def run(*command, check=True):
        result = subprocess.run(adb + list(command), capture_output=True, timeout=3)
        if check and result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", "replace").strip() or f"adb exited {result.returncode}")
        return result

    request_id = uuid.uuid4().hex
    trigger = TRIGGER.replace("llc_story_check", "llc_main_probe") if args.probe_main_thread else TRIGGER
    report_path = REPORT.replace("llc_story_state.json", "llc_main_probe.json") if args.probe_main_thread else REPORT
    if args.refresh_story:
        trigger = TRIGGER.replace("llc_story_check", "llc_story_refresh")
        report_path = REPORT.replace("llc_story_state.json", "llc_story_refresh.json")
    started = time.monotonic()
    try:
        processes = run("shell", "pidof", PACKAGE, check=False).stdout.decode("ascii", "replace").split()
        pid = None
        for candidate in processes:
            if not candidate.isdigit():
                continue
            command = run("exec-out", "su", "-c", f"cat /proc/{candidate}/cmdline", check=False)
            mapped = run("shell", f"su -c 'grep -F libllc_core.so /proc/{candidate}/maps'", check=False)
            if command.stdout.split(b"\0")[0].decode("utf-8", "replace").strip() == PACKAGE and mapped.returncode == 0 and mapped.stdout:
                pid = int(candidate)
                break
        if pid is None:
            raise RuntimeError("target process is not running")
        if args.compatibility:
            stat = run("exec-out", "su", "-c", f"cat /proc/{pid}/stat").stdout.decode("utf-8", "replace")
            start_ticks = stat[stat.rfind(") ") + 2:].split()[19]
            path = REPORT.replace("llc_story_state.json", "llc_compatibility.json")
            report = json.loads(run("exec-out", "su", "-c", f"cat {path}").stdout)
            errors = validate_compatibility(report, pid, start_ticks)
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
            print(json.dumps({"pid": pid, "status": report.get("status"),
                              "il2cpp_sha256": report.get("il2cpp_sha256"), "elapsed_ms": report.get("elapsed_ms"),
                              "checks_passed": not errors, "errors": errors}, ensure_ascii=False, indent=2))
            return 1 if errors else 0
        run("shell", f"su -c 'printf %s {request_id} > {trigger}.next && mv {trigger}.next {trigger}'")
        report = None
        while time.monotonic() - started < args.timeout:
            raw = run("exec-out", "su", "-c", f"cat {report_path}", check=False)
            if raw.returncode == 0:
                try:
                    candidate = json.loads(raw.stdout)
                    if isinstance(candidate, dict) and candidate.get("request_id") == request_id:
                        if candidate.get("pid") != pid:
                            raise RuntimeError("target restarted while waiting")
                        report = candidate
                        break
                except (ValueError, UnicodeError):
                    pass
            if run("shell", "su", "-c", f"kill -0 {pid}", check=False).returncode:
                raise RuntimeError("target process exited while waiting")
            time.sleep(0.2)
        if report is None:
            raise RuntimeError("timed out waiting for a report with this request_id")
        errors = (validate_main_probe(report, request_id, "story_refresh" if args.refresh_story else "main_thread_probe") if action else
                  validate_snapshot(report, request_id, args.require_story, args.require_data, args.require_body_cache, args.require_place_cache))
        summary = {"request_id": request_id, "elapsed_ms": round((time.monotonic() - started) * 1000),
                   "pid": report.get("pid"), "status": report.get("status"), "checks_passed": not errors, "errors": errors}
        if not action:
            for field in ("story_id", "dialog_id", "runtime_rows", "matched_rows", "checked_fields", "equal_fields", "data_matches_resource"):
                summary[field] = report.get(field)
        if action:
            for field in ("kind", "error", "delegate_abi_selftest", "context_matches", "native_sender_tid",
                          "native_callback_tid", "managed_callback_id", "unity_main_id", "action_applied"):
                summary[field] = report.get(field)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return 1 if errors else 0
    except (OSError, RuntimeError, ValueError, IndexError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"request_id": request_id, "checks_passed": False, "error": str(error)}, ensure_ascii=False))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
