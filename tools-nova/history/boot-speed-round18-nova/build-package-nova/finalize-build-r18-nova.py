#!/usr/bin/env python3
"""Bind the R18 full build, TU replay, protected objects and source freeze.

New file for the R18 build/package pass; no previous version. Requires a
zero-exit build, the exact three accepted sources, TU objects equal to both the
full-build objects and the Sol-accepted GREEN objects, and all protected
objects byte-identical to the pre-build baseline.
"""
import datetime
import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NOVA = HERE.parents[2]
TREE = NOVA / "unixnu-main-nova/out/meizu20pro/tree"
OBJ = TREE / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837"
RAW = NOVA / "unixnu-main-nova/out/meizu20pro/kernel/kernel.macho"
EXPORT_HEADER = (TREE / "build/xnu/ios-obj/EXPORT_HDRS/libkern/libkern/crypto/"
                 "native_sha256.h")
SNAP = NOVA / "snapshot-nova/boot-speed-round18-nova/build-package-nova"
PREBUILD = SNAP / "prebuild-hashes-nova.json"
SOURCES = {
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h":
        "faa8a909cf7b5ec353d87b2e1de1258fa33d212906b2617b2a838009d03dc742",
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c":
        "a5996632c820928896bd9b2f8790ce703cbde838925343c0c809b193ecb56172",
    "unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp":
        "08437deda6354ff660a41af7f2ea56ada41c90183a78f1935f0ae326402cbacb",
}
CHANGED = {
    "corecrypto_sha2": OBJ / "libkern/DEVELOPMENT/corecrypto_sha2.o",
    "NativeRamBlock": TREE / "build/native-ram-block/NativeRamBlock.o",
}
PROTECTED = {
    "BootLog": TREE / "build/native-meizu-bootlog/BootLog.o",
    "BootWatchdogPolicy": TREE / "build/meizu-watchdog-policy/BootWatchdogPolicy.o",
    "NativeUfs": TREE / "build/native-ufs/NativeUfs.o",
    "kern_exec": OBJ / "bsd/DEVELOPMENT/kern_exec.o",
    "mach_msg": OBJ / "osfmk/DEVELOPMENT/mach_msg.o",
    "sleh": OBJ / "osfmk/DEVELOPMENT/sleh.o",
    "NegroniFramebuffer": TREE / "build/negroni-display/NegroniFramebuffer.o",
    "MeizuHidProbe": TREE / "build/native-meizu-hid/MeizuHidProbe.o",
    "native_sha256_arm64": OBJ / "osfmk/DEVELOPMENT/native_sha256_arm64.o",
}
R17_RAW = "658c301b39597f2879ab1a0ac0730a93f5d4677592d490deac581dfa25273946"
R17_FREEZE_SHA = "1ebeb2bb4ece61019c7a4567ffe0455c1dd4174d23207d68957a08408f32b69b"
SHIM_SHA = "7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169"


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main():
    code = int((HERE / "build/exit-code-nova.txt").read_text().strip())
    if code != 0:
        print("BUILD EXIT %d" % code)
        return 1
    pre = json.loads(PREBUILD.read_text())
    tu = json.loads((HERE / "tu-result-r18-nova.json").read_text())
    freeze = json.loads((HERE / "source-freeze-r18-nova.json").read_text())
    start = json.loads((HERE / "build/build-command-nova.json").read_text())
    started = datetime.datetime.strptime(start["started_utc"], "%Y-%m-%dT%H:%M:%SZ").replace(
        tzinfo=datetime.timezone.utc).timestamp()
    result = {
        "schema": "nova-r18-build-v1",
        "new_file_no_previous_version": True,
        "status": "BUILD_PASS_AWAIT_PACKAGE",
        "exit_code": code,
        "build_command": "./unixnu-nova build kernel -j8 with reviewed PATH shim preset",
        "path_shim_sha256": SHIM_SHA,
        "build_started_utc": start["started_utc"],
        "build_finished_utc": start["finished_utc"],
        "source_hashes": {},
        "source_hashes_before": freeze["old_source_sha256"],
        "source_freeze": str(HERE / "source-freeze-r18-nova.json"),
        "source_freeze_sha256": sha(HERE / "source-freeze-r18-nova.json"),
        "source_verify": str(HERE / "source-verify-r18-nova.json"),
        "source_verify_sha256": sha(HERE / "source-verify-r18-nova.json"),
        "r17_source_freeze": str(NOVA / "artifacts-nova/boot-speed-round17-nova/"
                                "log-encode-completion-nova/source-freeze-r17-nova.json"),
        "r17_source_freeze_sha256": R17_FREEZE_SHA,
        "objects": {},
        "raw_kernel": str(RAW),
        "raw_kernel_sha256": sha(RAW),
        "raw_kernel_bytes": RAW.stat().st_size,
        "r17_raw_kernel_sha256": R17_RAW,
        "raw_kernel_changed": sha(RAW) != R17_RAW,
        "build_log": str(HERE / "build/build-output-nova.log"),
        "build_log_sha256": sha(HERE / "build/build-output-nova.log"),
        "build_stderr": str(HERE / "build/build-stderr-nova.log"),
        "build_stderr_sha256": sha(HERE / "build/build-stderr-nova.log"),
        "build_command_record": str(HERE / "build/build-command-nova.json"),
        "build_command_record_sha256": sha(HERE / "build/build-command-nova.json"),
        "tu_result": str(HERE / "tu-result-r18-nova.json"),
        "tu_result_sha256": sha(HERE / "tu-result-r18-nova.json"),
        "generated_export_header": {
            "path": str(EXPORT_HEADER),
            "sha256": sha(EXPORT_HEADER),
            "bytes": EXPORT_HEADER.stat().st_size,
            "source_path": str(TREE / ".cache/xnu-work/libkern/libkern/crypto/native_sha256.h"),
            "source_sha256": sha(TREE / ".cache/xnu-work/libkern/libkern/crypto/native_sha256.h"),
            "equals_source": (sha(EXPORT_HEADER) ==
                              sha(TREE / ".cache/xnu-work/libkern/libkern/crypto/native_sha256.h")),
        },
        "protected_objects": {},
        "prebuild_backups": {
            "raw": str(SNAP / "kernel.macho.pre"),
            "corecrypto_sha2": str(SNAP / "corecrypto_sha2.o.pre"),
            "NativeRamBlock": str(SNAP / "NativeRamBlock.o.pre"),
        },
        "notes": [
            "Full local kernel build; no device/install/reboot/slot/mount action.",
            "Only the accepted R18 streaming-SHA production files differ from R17.",
            "No performance or boot-time claim; the warm Data SHA saving needs the handset.",
            "The 36 s pre-receive display wait and the post-Hello black screen are not fixed.",
        ],
    }
    ok = True
    for rel, want in SOURCES.items():
        got = sha(NOVA / rel)
        result["source_hashes"][rel] = got
        ok = ok and got == want
    for name, p in CHANGED.items():
        h = sha(p)
        case = tu["cases"][name]
        same_tu = h == case["sha256"] and case["exact_full_build_match"]
        same_green = h == case["green_tu_sha256"] and case["matches_green_tu"]
        before = pre["changed_objects"][name]["sha256"]
        recompiled = p.stat().st_mtime >= started - 1
        result["objects"][name] = {
            "path": str(p), "sha256": h, "bytes": p.stat().st_size,
            "tu_object": case["output"], "tu_sha256": case["sha256"],
            "exact_tu_match": same_tu,
            "green_tu_object": case["green_tu_object"],
            "green_tu_sha256": case["green_tu_sha256"],
            "matches_green_tu": same_green,
            "prebuild_sha256": before,
            "changed_from_prebuild": h != before,
            "recompiled": recompiled,
        }
        ok = ok and same_tu and same_green and h != before
    for name, p in PROTECTED.items():
        h = sha(p)
        before = pre["protected_objects"][name]["sha256"]
        same = h == before
        result["protected_objects"][name] = {
            "path": str(p), "sha256": h, "bytes": p.stat().st_size,
            "prebuild_sha256": before,
            "unchanged": same,
            "recompiled": p.stat().st_mtime >= started - 1,
        }
        ok = ok and same
    ok = ok and freeze.get("pass") is True and freeze.get("count") == 6186
    ok = ok and result["generated_export_header"]["equals_source"]
    ok = ok and result["raw_kernel_sha256"] != R17_RAW
    result["pass"] = ok
    (HERE / "build-result-r18-nova.json").write_text(json.dumps(result, indent=2) + "\n")
    print("sources", {k: v[:16] for k, v in result["source_hashes"].items()})
    print("objects", {k: (v["sha256"], v["exact_tu_match"], v["matches_green_tu"])
                       for k, v in result["objects"].items()})
    print("raw=%s bytes=%d changed=%s" % (
        result["raw_kernel_sha256"], result["raw_kernel_bytes"],
        result["raw_kernel_changed"]))
    print("protected unchanged=", {k: v["unchanged"] for k, v in result["protected_objects"].items()})
    print("BUILD RESULT pass=%s" % result["pass"])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
