#!/usr/bin/env python3
"""R18 pre-build backup and protected-object hash baseline.

New file for the R18 build/package pass; no previous version. Copies the
current raw kernel and the two accepted changed objects into snapshot-nova,
copies every protected object plus the compile recipes/generated export header,
and writes a machine-readable baseline. No build, device, or production edit.
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NOVA = HERE.parents[2]
SNAP = NOVA / "snapshot-nova/boot-speed-round18-nova/build-package-nova"
TREE = NOVA / "unixnu-main-nova/out/meizu20pro/tree"
BUILD = TREE / "build"
RAW = NOVA / "unixnu-main-nova/out/meizu20pro/kernel/kernel.macho"
CC_DIR = BUILD / "xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837"
NATIVE_SHA_HEADER = (BUILD / "xnu/ios-obj/EXPORT_HDRS/libkern/libkern/crypto/"
                     "native_sha256.h")
SOURCE_HEADER = (TREE / ".cache/xnu-work/libkern/libkern/crypto/native_sha256.h")

CHANGED = {
    "corecrypto_sha2": CC_DIR / "libkern/DEVELOPMENT/corecrypto_sha2.o",
    "NativeRamBlock": BUILD / "native-ram-block/NativeRamBlock.o",
}
PROTECTED = {
    "BootLog": BUILD / "native-meizu-bootlog/BootLog.o",
    "BootWatchdogPolicy": BUILD / "meizu-watchdog-policy/BootWatchdogPolicy.o",
    "NativeUfs": BUILD / "native-ufs/NativeUfs.o",
    "kern_exec": CC_DIR / "bsd/DEVELOPMENT/kern_exec.o",
    "mach_msg": CC_DIR / "osfmk/DEVELOPMENT/mach_msg.o",
    "sleh": CC_DIR / "osfmk/DEVELOPMENT/sleh.o",
    "NegroniFramebuffer": BUILD / "negroni-display/NegroniFramebuffer.o",
    "MeizuHidProbe": BUILD / "native-meizu-hid/MeizuHidProbe.o",
    "native_sha256_arm64": CC_DIR / "osfmk/DEVELOPMENT/native_sha256_arm64.o",
}
RECIPES = {
    "corecrypto_sha2": CC_DIR / "libkern/DEVELOPMENT/corecrypto_sha2.o.json",
    "NativeRamBlock": BUILD / "native-ram-block/compile-command.json",
}
R17_RAW = "658c301b39597f2879ab1a0ac0730a93f5d4677592d490deac581dfa25273946"
R17_OBJECTS = {
    "corecrypto_sha2": "923c5d528a341f3c3dc0f48759f6475a7537481e8a5b1e4b30ee02d749b309d7",
    "NativeRamBlock": "e0f468571c28bbb60e1b3df2bf74272ea374ee467627ebad672fd0ce87ffcb10",
    "sleh": "9fdf2e13943402dc866b828c227d325705bd6ad68bb9818f84c7bf64572b90d1",
    "mach_msg": "04b5e54efddf4f62fcd6f8e2ccb2843abc703d693d0fd724aefe07292760537d",
    "BootWatchdogPolicy": "5bfde100b6e66ef0a2da8fc2f10b1bf1dd846fb211775b274096164ed318fa72",
    "BootLog": "f60e1644f7c3ece260f88ae820f06279a015f19e6f5c4ee8b3c2230246ad1228",
    "NativeUfs": "49154bbeb3c74300353d57686905b7f008eff06dd1d1293fe4e51d9b54e412d7",
    "kern_exec": "70e14124c8dc5c2a16fca6a4fc163dc42599d02c4db15723ba6929055a073ad4",
}


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main():
    if SNAP.exists():
        print("refusing to overwrite existing snapshot: %s" % SNAP)
        return 1
    SNAP.mkdir(parents=True)
    objects = SNAP / "objects"
    objects.mkdir()
    recipes = SNAP / "recipes"
    recipes.mkdir()
    generated = SNAP / "generated-header"
    generated.mkdir()

    record = {
        "schema": "nova-r18-prebuild-v1",
        "new_file_no_previous_version": True,
        "raw_kernel": {"path": str(RAW), "sha256": sha(RAW), "bytes": RAW.stat().st_size},
        "changed_objects": {},
        "protected_objects": {},
        "recipes": {},
        "generated_export_header": {},
        "r17_expected": {"raw_kernel_sha256": R17_RAW, "objects": R17_OBJECTS},
    }
    if record["raw_kernel"]["sha256"] != R17_RAW:
        print("current raw kernel is not the R17 output: %s" % record["raw_kernel"]["sha256"])
        return 1
    shutil.copy2(RAW, SNAP / "kernel.macho.pre")
    for name, p in CHANGED.items():
        h = sha(p)
        shutil.copy2(p, SNAP / ("%s.o.pre" % name))
        record["changed_objects"][name] = {
            "path": str(p), "sha256": h, "bytes": p.stat().st_size,
            "r17_sha256": R17_OBJECTS[name],
            "matches_r17": h == R17_OBJECTS[name],
            "backup": str(SNAP / ("%s.o.pre" % name)),
        }
        if h != R17_OBJECTS[name]:
            print("changed object %s is not the R17 value" % name)
            return 1
    for name, p in PROTECTED.items():
        h = sha(p)
        shutil.copy2(p, objects / (name + ".o.pre"))
        record["protected_objects"][name] = {
            "path": str(p), "sha256": h, "bytes": p.stat().st_size,
            "r17_sha256": R17_OBJECTS.get(name),
            "matches_r17": (h == R17_OBJECTS[name]) if name in R17_OBJECTS else None,
            "backup": str(objects / (name + ".o.pre")),
        }
        if name in R17_OBJECTS and h != R17_OBJECTS[name]:
            print("protected object %s differs from the R17 value" % name)
            return 1
    for name, p in RECIPES.items():
        shutil.copy2(p, recipes / (name + ".json.pre"))
        record["recipes"][name] = {"path": str(p), "sha256": sha(p),
                                   "backup": str(recipes / (name + ".json.pre"))}
    record["generated_export_header"] = {
        "path": str(NATIVE_SHA_HEADER),
        "sha256": sha(NATIVE_SHA_HEADER),
        "bytes": NATIVE_SHA_HEADER.stat().st_size,
        "source_path": str(SOURCE_HEADER),
        "source_sha256": sha(SOURCE_HEADER),
        "equals_source": sha(NATIVE_SHA_HEADER) == sha(SOURCE_HEADER),
    }
    shutil.copy2(NATIVE_SHA_HEADER, generated / "native_sha256.h.pre")
    (SNAP / "prebuild-hashes-nova.json").write_text(json.dumps(record, indent=1) + "\n")
    lines = []
    for label, p in [("kernel.macho.pre", SNAP / "kernel.macho.pre"),
                     ("corecrypto_sha2.o.pre", SNAP / "corecrypto_sha2.o.pre"),
                     ("NativeRamBlock.o.pre", SNAP / "NativeRamBlock.o.pre")]:
        lines.append("%s  %s" % (sha(p), label))
    for name in sorted(PROTECTED):
        lines.append("%s  objects/%s.o.pre" % (sha(objects / (name + ".o.pre")), name))
    (SNAP / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n")
    print(json.dumps({
        "raw": record["raw_kernel"]["sha256"],
        "changed_objects": {k: v["sha256"] for k, v in record["changed_objects"].items()},
        "protected": {k: v["sha256"] for k, v in record["protected_objects"].items()},
        "generated_header_matches_source": record["generated_export_header"]["equals_source"],
        "snapshot": str(SNAP),
    }, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
