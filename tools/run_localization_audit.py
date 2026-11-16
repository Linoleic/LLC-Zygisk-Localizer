"""Run the offline audit with reproducible input fingerprints and optional comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def snapshot(root):
    digest = hashlib.sha256()
    count = 0
    for path in sorted(root.rglob("*.json"), key=lambda p: p.relative_to(root).as_posix()):
        if not path.is_file() or path.suffix != ".json":
            continue
        name = path.relative_to(root).as_posix().encode("utf-8")
        with path.open("rb") as stream:
            content = hashlib.file_digest(stream, "sha256").digest()
        digest.update(len(name).to_bytes(8, "big") + name + content)
        count += 1
    return {"directory": root.name, "json_files": count, "sha256": digest.hexdigest()}


def compare(old, current):
    if old["schema"] != current["schema"]:
        raise ValueError("Report schemas differ. Regenerate the baseline with the current tool.")
    changes = {key: [] for key in ("new", "resolved", "persistent", "unverified", "files_changed")}
    before_hash = old.get("inputs", {}).get("original", {}).get("sha256")
    after_hash = current["inputs"]["original"]["sha256"]
    old_files, new_files = old["files"], current["files"]
    for name in sorted(old_files.keys() | new_files.keys()):
        before, after = old_files.get(name, {}), new_files.get(name, {})
        def status(file):
            return {key: file.get(key) for key in
                    ("source", "translation", "coverage", "layout_mismatch", "duplicate_ids")}

        if status(before) != status(after) or after.get("source_structure_changed"):
            changes["files_changed"].append({"file": name, "before": status(before),
                                             "after": status(after),
                                             "source_structure_changed": after.get("source_structure_changed", False)})
        def issues(file):
            return {(item["id"], item["field"], item["kind"].replace("new_missing_translation", "missing_translation"))
                    for item in file.get("issues", []) if item["kind"] not in ("source_changed", "source_removed")}

        past, present = issues(before), issues(after)
        comparable = (before_hash == after_hash
                      and after.get("source") == "ok" and after.get("translation") == "ok"
                      and after.get("coverage") == before.get("coverage")
                      and not after.get("layout_mismatch"))
        for category, entries in (("new", present - past), ("persistent", present & past),
                                  ("resolved" if comparable else "unverified", past - present)):
            changes[category].extend({"file": name, "id": key, "field": field, "kind": kind}
                                     for key, field, kind in sorted(entries))
    return changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("original", type=Path)
    parser.add_argument("translation", type=Path)
    parser.add_argument("--previous-original", type=Path)
    parser.add_argument("--baseline", type=Path, help="Previous JSON audit report")
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()
    roots = {"original": args.original.resolve(), "translation": args.translation.resolve()}
    if args.previous_original:
        roots["previous_original"] = args.previous_original.resolve()
    output = args.output.resolve()
    if any(root in output.parents for root in roots.values()):
        parser.error("Write the report outside the input directories.")
    report = json.loads(subprocess.check_output([args.executable.resolve(), *roots.values()]))
    report["inputs"] = {key: snapshot(root) for key, root in roots.items()}
    if args.baseline:
        report["difference"] = compare(json.loads(args.baseline.read_text(encoding="utf-8-sig")), report)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
