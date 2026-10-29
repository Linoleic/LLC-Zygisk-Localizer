"""Temporarily run a standalone diagnostic core, always restoring normal Stage 2.

Run only after game resource downloads finish. Does not update Stage 1 or the
normal local binaries. Requires explicitly selecting the device and probe.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import time
import uuid

PACKAGE = "com.ProjectMoon.LimbusCompany"
DESTINATIONS = ["/data/local/tmp/llc/libllc_core.so", "/data/adb/modules/llc_zygisk/libllc_core.so",
                f"/data/user/0/{PACKAGE}/cache/libllc_core.so"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--normal-core", type=pathlib.Path, default=pathlib.Path("module/libllc_core.so"))
    parser.add_argument("--report-name", choices=["llc_dataset_probe.json", "llc_metadata_probe.json"], default="llc_dataset_probe.json")
    parser.add_argument("--kind", default="dataset_sandbox_probe")
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=35)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 60:
        parser.error("timeout must be between 1 and 60 seconds")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    adb = ["adb", "-s", args.serial]

    def run(*command, check=True):
        result = subprocess.run(adb + list(command), capture_output=True, timeout=20)
        if check and result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", "replace") or result.stdout.decode("utf-8", "replace"))
        return result

    def root(command, check=True):
        return run("exec-out", "su -c '" + command + "'", check=check)

    def remote_hash(path):
        return root("sha256sum " + path).stdout.decode().split()[0]

    normal_hash = hashlib.sha256(args.normal_core.read_bytes()).hexdigest()
    probe_hash = hashlib.sha256(args.probe.read_bytes()).hexdigest()
    if normal_hash == probe_hash:
        raise RuntimeError("probe must differ from the normal core")
    for destination in DESTINATIONS:
        if remote_hash(destination) != normal_hash:
            raise RuntimeError("normal local core differs from deployed core: " + destination)
    owner = root("stat -c %u:%g " + DESTINATIONS[2]).stdout.decode().strip()
    context = root("ls -Zd " + DESTINATIONS[2]).stdout.decode().split()[0]
    if not all(part.isdigit() for part in owner.split(":")) or not context.startswith("u:object_r:"):
        raise RuntimeError("unrecognized cache owner/context")
    token = uuid.uuid4().hex
    staged = "/data/local/tmp/llc-probe-" + token + ".so"
    restore = "/data/local/tmp/llc-restore-" + token + ".so"
    report_path = f"/sdcard/Android/data/{PACKAGE}/files/" + args.report_name
    run("push", str(args.normal_core), restore)
    run("push", str(args.probe), staged)
    manifest = {"probe_sha256": probe_hash, "normal_sha256": normal_hash,
                "report_name": args.report_name, "kind": args.kind, "restored": False}

    def install(source, expected, restore_all=False):
        failures = []
        for destination in DESTINATIONS:
            try:
                command = f"cp {source} {destination}.next && chmod 755 {destination}.next"
                if destination == DESTINATIONS[2]:
                    command += f" && chown {owner} {destination}.next && chcon {context} {destination}.next"
                root(command + f" && mv {destination}.next {destination}")
                if remote_hash(destination) != expected:
                    raise RuntimeError("deployed hash mismatch: " + destination)
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                if not restore_all:
                    raise
                failures.append(destination + ": " + str(error))
        if failures:
            raise RuntimeError("normal core restoration incomplete: " + "; ".join(failures))

    report = None
    try:
        run("shell", "am", "force-stop", PACKAGE)
        previous = root("cat " + report_path, check=False)
        if previous.returncode == 0:
            (args.output_dir / "previous-report.json").write_bytes(previous.stdout)
        root("rm -f " + report_path + " " + report_path + ".next")
        install(staged, probe_hash)
        launch = run("shell", "am", "start", "-W", "-n", PACKAGE + "/.LimbusCompanyActivity")
        (args.output_dir / "probe-launch.txt").write_bytes(launch.stdout)
        print("diagnostic launched; waiting for correlated report", flush=True)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            candidate = root("cat " + report_path, check=False)
            try:
                value = json.loads(candidate.stdout)
                pid = value["pid"]
                if value.get("schema") != 1 or value.get("kind") != args.kind or not isinstance(pid, int) or pid <= 0:
                    raise ValueError("unrecognized report")
                if value.get("status") not in ("passed", "failed", "collected"):
                    time.sleep(0.3)
                    continue
                cmdline = root(f"cat /proc/{pid}/cmdline").stdout.split(b"\0")[0].decode()
                maps = root(f"cat /proc/{pid}/maps").stdout.decode()
                paths = {line.split()[-1] for line in maps.splitlines() if "libllc_core.so" in line}
                if cmdline != PACKAGE or not paths or any(remote_hash(path) != probe_hash for path in paths):
                    raise ValueError("report process/core mismatch")
                report = value
                (args.output_dir / "report.json").write_bytes(candidate.stdout)
                (args.output_dir / "probe-log.txt").write_bytes(run("logcat", "-d", "--pid=" + str(pid), "-s", "LLC_CORE:I", "Unity:I", "AndroidRuntime:E").stdout)
                break
            except (ValueError, KeyError, RuntimeError):
                time.sleep(0.3)
        if report is None:
            raise RuntimeError("probe report timed out")
    finally:
        try:
            run("shell", "am", "force-stop", PACKAGE)
            install(restore, normal_hash, restore_all=True)
            manifest["restored"] = True
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            manifest["restore_error"] = str(error)
            manifest["retained_restore_file"] = restore
            raise
        finally:
            (args.output_dir / "build.json").write_bytes(json.dumps(manifest, indent=2).encode())
        launch = run("shell", "am", "start", "-W", "-n", PACKAGE + "/.LimbusCompanyActivity")
        (args.output_dir / "restored-launch.txt").write_bytes(launch.stdout)
        root("rm -f " + staged + " " + restore)
        print("restored all three normal core paths", flush=True)
    inspection = report.get("inspection") or {}
    summary = {key: inspection[key] for key in ("live_dataset", "remote_loaded", "validated_nonempty_tables",
               "live_game_fields_written", "owned_write", "live_ui_trial") if key in inspection}
    summary["tables"] = [{"table": table.get("table"), "status": table.get("status"), "slots": table.get("slots"),
                          "samples": len(table.get("samples", [])), "snapshot_unchanged": table.get("snapshot_unchanged")}
                         for table in inspection.get("tables", [])]
    print(json.dumps({"pid": report["pid"], "status": report.get("status"), "error": report.get("error"),
                      "bridge": report.get("bridge"), "inspection": summary}, ensure_ascii=False, indent=2))
    return 0 if report.get("status") == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
