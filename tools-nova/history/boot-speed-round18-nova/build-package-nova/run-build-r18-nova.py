#!/usr/bin/env python3
"""Run the R18 full Nova kernel build with the reviewed R14 PATH shim.

New file for the R18 build/package pass; no previous version. The shim only
removes the sandboxed dSYM stderr process-substitution wrapper; sources and
flags are unchanged. The build runs only in Nova and does not touch devices.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
NOVA = HERE.parents[2]
SHIM_DIR = NOVA / "artifacts-nova/boot-speed-round14-nova/build-audit-nova/build-helpers-nova"
SHIM = SHIM_DIR / "bash"
SHIM_SHA = "7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169"
BUILD = HERE / "build"
OUT_LOG = BUILD / "build-output-nova.log"
ERR_LOG = BUILD / "build-stderr-nova.log"
EXIT = BUILD / "exit-code-nova.txt"
FREEZE = HERE / "source-freeze-r18-nova.json"
PREBUILD = (NOVA / "snapshot-nova/boot-speed-round18-nova/build-package-nova/"
            "prebuild-hashes-nova.json")
SOURCES = {
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h":
        "faa8a909cf7b5ec353d87b2e1de1258fa33d212906b2617b2a838009d03dc742",
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c":
        "a5996632c820928896bd9b2f8790ce703cbde838925343c0c809b193ecb56172",
    "unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp":
        "08437deda6354ff660a41af7f2ea56ada41c90183a78f1935f0ae326402cbacb",
}


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def competing():
    patterns = [
        "build-xnu-4k-baseline.sh",
        "unixnu-nova build kernel",
        "build-meizu-watchdog-policy.py",
        r"clang.*corecrypto_sha2\.c",
        r"clang.*NativeRamBlock\.cpp",
    ]
    found = []
    for pattern in patterns:
        p = subprocess.run(["/usr/bin/pgrep", "-f", pattern], text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if p.returncode not in (0, 1):
            raise SystemExit("process inspection failed for %s" % pattern)
        found.extend(p.stdout.split())
    return sorted(set(found))


def main():
    if BUILD.exists():
        print("refusing to overwrite existing build dir: %s" % BUILD)
        return 1
    if sha(SHIM) != SHIM_SHA:
        print("reviewed shim hash mismatch")
        return 1
    freeze = json.loads(FREEZE.read_text())
    if freeze.get("pass") is not True or freeze.get("count") != 6186:
        print("source freeze is not passing")
        return 1
    pre = json.loads(PREBUILD.read_text())
    if pre["raw_kernel"]["sha256"] != "658c301b39597f2879ab1a0ac0730a93f5d4677592d490deac581dfa25273946":
        print("prebuild raw is not the R17 output")
        return 1
    for rel, want in SOURCES.items():
        if sha(NOVA / rel) != want:
            print("source changed before build: %s" % rel)
            return 1
    pids = competing()
    if pids:
        print("competing build processes: %s" % ",".join(pids))
        return 1
    BUILD.mkdir()
    env = os.environ.copy()
    env["PATH"] = str(SHIM_DIR) + os.pathsep + env.get("PATH", "")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    for key in ("PYTHONPATH", "PYTHONHOME", "BASH_ENV", "ENV", "MAKEFLAGS"):
        env.pop(key, None)
    argv = ["./unixnu-nova", "build", "kernel", "-j", "8"]
    started = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    with OUT_LOG.open("wb") as out, ERR_LOG.open("wb") as err:
        p = subprocess.run(argv, cwd=NOVA, env=env, stdout=out, stderr=err)
    finished = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    EXIT.write_text(str(p.returncode) + "\n")
    record = {
        "schema": "nova-r18-build-command-v1",
        "new_file_no_previous_version": True,
        "argv": argv,
        "cwd": str(NOVA),
        "started_utc": started,
        "finished_utc": finished,
        "exit_code": p.returncode,
        "path_shim": str(SHIM),
        "path_shim_sha256": sha(SHIM),
        "path_prepend": str(SHIM_DIR),
        "python_bytecode_disabled": True,
        "stdout": str(OUT_LOG),
        "stderr": str(ERR_LOG),
        "source_freeze_sha256": sha(FREEZE),
        "prebuild_baseline_sha256": sha(PREBUILD),
    }
    (BUILD / "build-command-nova.json").write_text(json.dumps(record, indent=1) + "\n")
    print(json.dumps(record, indent=1))
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
