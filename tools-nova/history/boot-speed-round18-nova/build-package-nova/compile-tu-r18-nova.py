#!/usr/bin/env python3
"""R18 exact ARM64 TU replay for the two changed production objects.

New file for the R18 build/package pass; no previous version. Reads the actual
post-build compile recipes, changes only -o to a new output path, preserves -g
and every other flag, and compares the result byte-for-byte with the full-build
object and with the Sol-accepted GREEN TU object.
"""
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NOVA = HERE.parents[2]
TREE = NOVA / "unixnu-main-nova/out/meizu20pro/tree"
OBJ = (TREE / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837")
NRB_CWD = OBJ / "iokit/DEVELOPMENT"
OUT = HERE / "tu-r18"
GREEN = (NOVA / "artifacts-nova/boot-speed-round18-nova/warm-sha-stream-nova/"
         "green-run-nova")
CASES = {
    "corecrypto_sha2": {
        "object": OBJ / "libkern/DEVELOPMENT/corecrypto_sha2.o",
        "record": OBJ / "libkern/DEVELOPMENT/corecrypto_sha2.o.json",
        "green": GREEN / "corecrypto-sha2-production-nova.o",
    },
    "NativeRamBlock": {
        "object": TREE / "build/native-ram-block/NativeRamBlock.o",
        "record": TREE / "build/native-ram-block/compile-command.json",
        "green": GREEN / "NativeRamBlock-production-nova.o",
    },
}
EXPECTED_GREEN = {
    "corecrypto_sha2": "0e5e79e708f7238fc35210077b9ba004ddac77b392aa885e9205c5220b8b05a3",
    "NativeRamBlock": "05304db41b0f6c76e1f9dc6726dde98679418ee3c853f6b0fc28ef4773c19cee",
}
SHIM_SHA = "7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169"


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def argv_and_cwd(name, rec_path):
    text = rec_path.read_text()
    if name == "corecrypto_sha2":
        # This generated file is one JSON record followed by a trailing comma,
        # not a JSON array; decode the first object only.
        row, _end = json.JSONDecoder().raw_decode(text)
        if not row["file"].endswith("/corecrypto_sha2.c"):
            raise SystemExit("corecrypto compile record is for the wrong file")
        return list(row["arguments"]), Path(row["directory"])
    return list(json.loads(text)), NRB_CWD


def main():
    if OUT.exists():
        print("refusing to overwrite existing TU dir: %s" % OUT)
        return 1
    OUT.mkdir()
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    result = {"schema": "nova-r18-tu-v1", "new_file_no_previous_version": True,
              "path_shim_sha256": SHIM_SHA, "cases": {}}
    ok = True
    for name, spec in CASES.items():
        original, cwd = argv_and_cwd(name, spec["record"])
        args = list(original)
        if "-o" not in args:
            print("%s: no -o in argv" % name)
            return 1
        oi = args.index("-o")
        out = OUT / (name + "-nova.o")
        args[oi + 1] = str(out)
        changed = [i for i in range(len(args)) if args[i] != original[i]]
        p = subprocess.run(args, cwd=cwd, env=env, capture_output=True, text=True)
        log = OUT / (name + "-nova.log")
        log.write_text("CWD " + str(cwd) + "\n$ " + " ".join(args) + "\n" +
                       p.stdout + p.stderr)
        got = sha(out) if out.is_file() else None
        full = spec["object"]
        full_hash = sha(full) if full.is_file() else None
        green_hash = sha(spec["green"]) if spec["green"].is_file() else None
        case = {
            "record": str(spec["record"]),
            "record_sha256": sha(spec["record"]),
            "cwd": str(cwd),
            "argv": args,
            "original_argv_sha256": hashlib.sha256(
                json.dumps(original).encode()).hexdigest(),
            "output": str(out),
            "exit": p.returncode,
            "ok": p.returncode == 0 and got is not None,
            "sha256": got,
            "full_build_object": str(full),
            "full_build_sha256": full_hash,
            "exact_full_build_match": got == full_hash,
            "green_tu_object": str(spec["green"]),
            "green_tu_sha256": green_hash,
            "matches_green_tu": got == green_hash,
            "expected_green_sha256": EXPECTED_GREEN[name],
            "green_expected_matches_acceptance": green_hash == EXPECTED_GREEN[name],
            "argv_output_rewritten_only": changed == [oi + 1],
            "changed_argv_indices": changed,
            "debug_flags_preserved": "-g" in args,
            "debug_argv_present": [a for a in args if a == "-g" or a.startswith("-g")],
            "log": str(log),
        }
        result["cases"][name] = case
        ok = ok and case["ok"] and case["exact_full_build_match"] and \
            case["matches_green_tu"] and case["argv_output_rewritten_only"]
    result["pass"] = ok
    (HERE / "tu-result-r18-nova.json").write_text(json.dumps(result, indent=2) + "\n")
    for n, v in result["cases"].items():
        print(n, "exit=%s" % v["exit"], "exact_full=%s" % v["exact_full_build_match"],
              "green=%s" % v["matches_green_tu"], v["sha256"])
    print("TU RESULT pass=%s" % result["pass"])
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
