#!/usr/bin/env python3
"""R18 6186-item source freeze check against the accepted R17 freeze.

New file for the R18 build/package pass; no previous version. The R17 freeze
is read-only. Exactly the three accepted production files may differ; every
other one of the 6186 paths must still match. Writes a new R18 freeze JSON
that keeps the original (old) and current (new) hashes for the three files.
"""
import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NOVA = HERE.parents[2]
R17_FREEZE = (NOVA / "artifacts-nova/boot-speed-round17-nova/"
              "log-encode-completion-nova/source-freeze-r17-nova.json")
R17_FREEZE_SHA = "1ebeb2bb4ece61019c7a4567ffe0455c1dd4174d23207d68957a08408f32b69b"
ALLOWED = {
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h",
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c",
    "unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp",
}
EXPECTED_COUNT = 6186


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main():
    baseline_sha = sha(R17_FREEZE)
    if baseline_sha != R17_FREEZE_SHA:
        print("R17 freeze sha mismatch: %s" % baseline_sha)
        return 1
    data = json.loads(R17_FREEZE.read_text())
    base = data["source_sha256"]
    errors, missing, mismatched = [], [], []
    if data.get("count") != EXPECTED_COUNT or len(base) != EXPECTED_COUNT:
        errors.append("R17 freeze count is not 6186")
    source = {}
    for rel, want in base.items():
        p = (NOVA / rel).resolve()
        if not p.is_file():
            missing.append(rel)
            continue
        got = sha(p)
        source[rel] = got
        if got != want:
            mismatched.append(rel)
    unexpected = sorted(set(mismatched) - ALLOWED)
    result = {
        "schema": "nova-r18-source-freeze-v1",
        "new_file_no_previous_version": True,
        "baseline": str(R17_FREEZE),
        "baseline_sha256": baseline_sha,
        "baseline_count": data.get("count"),
        "count": len(source),
        "expected_count": EXPECTED_COUNT,
        "allowed_files": sorted(ALLOWED),
        "changed_from_baseline": sorted(mismatched),
        "unexpected_changed_files": unexpected,
        "missing": missing,
        "source_sha256": source,
        "old_source_sha256": {k: v for k, v in base.items() if k in ALLOWED},
        "new_source_sha256": {k: v for k, v in source.items() if k in ALLOWED},
        "r17_freeze_unmodified": baseline_sha == R17_FREEZE_SHA,
        "exactly_three_intended_changes": set(mismatched) == ALLOWED,
        "pass": (not errors and not missing and not unexpected
                 and set(mismatched) == ALLOWED
                 and len(source) == EXPECTED_COUNT
                 and baseline_sha == R17_FREEZE_SHA),
    }
    (HERE / "source-freeze-r18-nova.json").write_text(json.dumps(result, indent=1) + "\n")
    verify = {
        "schema": "nova-r18-source-verify-v1",
        "new_file_no_previous_version": True,
        "baseline": str(R17_FREEZE),
        "baseline_sha256": baseline_sha,
        "expected_count": EXPECTED_COUNT,
        "verified_count": len(source),
        "missing": missing,
        "mismatched": sorted(mismatched),
        "unexpected_changed": unexpected,
        "exactly_three_intended_changes": set(mismatched) == ALLOWED,
        "old_source_sha256": result["old_source_sha256"],
        "new_source_sha256": result["new_source_sha256"],
        "pass": result["pass"],
    }
    (HERE / "source-verify-r18-nova.json").write_text(json.dumps(verify, indent=1) + "\n")
    print(json.dumps({
        "pass": result["pass"], "count": len(source),
        "changed": sorted(mismatched), "unexpected": unexpected,
        "missing": len(missing), "baseline_sha256": baseline_sha,
        "freeze_sha256": sha(HERE / "source-freeze-r18-nova.json"),
    }, indent=1))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
