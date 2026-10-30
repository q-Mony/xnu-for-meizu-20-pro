#!/usr/bin/env python3
"""Build the 96 MiB R18 warm-Data streaming-SHA candidate in the accepted envelope.

New file for the R18 build/package pass; adapted from the reviewed R17 packer
without replacing it. No device, mount, install, reboot, slot change, installer
creation or execution. Only EFI/UniXNU/Payloads/kernel.macho is replaced; the
R17 logger fix and every non-kernel EFI/envelope byte are retained.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
NOVA = HERE.parents[2]
R14PACK = NOVA / "artifacts-nova/boot-speed-round14-nova/packaging-nova/pack-watchdog-r14-candidate-nova.py"
R14_FINAL = NOVA / "artifacts-nova/boot-speed-round14-nova/packaging-nova/watchdog-r14-candidate-final-nova"
R14_IMG = R14_FINAL / "boot-warm-watchdog-r14-candidate-nova.img"
R14_VER = R14_FINAL / "evidence-nova/verification-nova.json"
R14_IMG_SHA = "6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641"
R14_VER_SHA = "036d3dee4a7fc3f506a155b4f1ef88ba110d54fe4e672cc9c2f16920b3810ed6"
R15_FINAL = NOVA / "artifacts-nova/boot-speed-round15-nova/arm-lock-review-nova/package/sblong-r15-arm-lock-final-nova"
R15_IMG = R15_FINAL / "boot-warm-sblong-r15-candidate-nova.img"
R15_IMG_SHA = "53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed"
R17_FINAL = (NOVA / "artifacts-nova/boot-speed-round17-nova/log-encode-completion-nova/"
             "package/boot-warm-r17-logfix-candidate-nova")
R17_IMG = R17_FINAL / "boot-warm-r17-logfix-candidate-nova.img"
R17_VER = R17_FINAL / "evidence-nova/verification-nova.json"
R17_IMG_SHA = "8796d6c5824ce563fda8b4d0e1a429f235749254648cc160534e5807283338d6"
R17_VER_SHA = "275fa8644d118453cc23dc3ae1dfc22ecc0448b9ed47e3526995416bbe8fa7ab"
KERNEL_REL = "UniXNU/Payloads/kernel.macho"
SOURCE_COUNT = 3
SOURCE_PATHS = {
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/libkern/crypto/native_sha256.h",
    "unixnu-main-nova/out/meizu20pro/tree/.cache/xnu-work/libkern/crypto/corecrypto_sha2.c",
    "unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp",
}
IMAGE_NAME = "boot-warm-r18-streamsha-candidate-nova.img"


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-result", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    build = json.loads(args.build_result.read_text())
    assert build["status"] == "BUILD_PASS_AWAIT_PACKAGE" and build["exit_code"] == 0
    assert build["pass"] is True

    assert sha(R14_IMG) == R14_IMG_SHA
    assert sha(R14_VER) == R14_VER_SHA
    assert sha(R15_IMG) == R15_IMG_SHA
    assert sha(R17_IMG) == R17_IMG_SHA
    assert sha(R17_VER) == R17_VER_SHA
    report = json.loads(R14_VER.read_text())
    assert report["status"] == "PASS" and report["final_image_sha256"] == R14_IMG_SHA
    report15 = json.loads((R15_FINAL / "evidence-nova/verification-nova.json").read_text())
    assert report15["status"] == "BUILT_AWAIT_NEW_USER_INSTALL_COMMAND"
    assert report15["candidate_image_sha256"] == R15_IMG_SHA
    assert report15["changed_efi_files"] == [KERNEL_REL]
    report17 = json.loads(R17_VER.read_text())
    assert report17["status"] == "BUILT_AWAIT_NEW_USER_INSTALL_COMMAND"
    assert report17["candidate_image_sha256"] == R17_IMG_SHA
    assert report17["changed_efi_files"] == [KERNEL_REL]

    # Exactly the three accepted sources, both changed objects bound to the
    # Sol-accepted GREEN TU objects and to the full-build objects.
    sources = build["source_hashes"]
    assert len(sources) == SOURCE_COUNT
    assert set(sources) == SOURCE_PATHS
    for rel, want in sources.items():
        assert sha((NOVA / rel).resolve(strict=True)) == want
    objects = build["objects"]
    assert set(objects) == {"corecrypto_sha2", "NativeRamBlock"}
    for name in ("corecrypto_sha2", "NativeRamBlock"):
        assert objects[name]["exact_tu_match"] is True
        assert objects[name]["matches_green_tu"] is True
        assert sha(Path(objects[name]["path"])) == objects[name]["sha256"]

    spec = importlib.util.spec_from_file_location("r14_pack_readonly", R14PACK)
    r14 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(r14)
    _, (r9, r7, r6, base) = r14.helpers()
    assert sha(base.PATCHER) == base.PATCHER_SHA
    assert sha(base.PATCHER.parent / "xnu-exception-screen.c") == base.EXCEPTION_SHA
    assert sha(base.PAN_TOOL) == r9.PAN_TOOL_SHA
    raw = Path(build["raw_kernel"]).resolve(strict=True)
    assert sha(raw) == build["raw_kernel_sha256"]
    baseline = R14_IMG.read_bytes()
    layout = base.boot_layout(baseline)
    assert len(baseline) == 96 * 1024 * 1024 and layout["ramdisk_size"] == 90 * 1024 * 1024
    start, size, page, ks = (layout[k] for k in ("ramstart", "ramdisk_size", "page", "kernel_size"))
    entry = base.entry_info(raw)
    assert entry["branch_target"] == entry["start_first_cpu"], "raw kernel is not unpatched"

    # The accepted R14 envelope still matches the accepted R15 and R17 images
    # outside the Android v0 SHA1 slot and the FAT-backed data.
    r15_bytes = R15_IMG.read_bytes()
    r17_bytes = R17_IMG.read_bytes()
    for name, data in (("r15", r15_bytes), ("r17", r17_bytes)):
        assert len(data) == len(baseline), name + " length differs"
        assert data[:576] == baseline[:576], name + " header prefix differs"
        assert data[596:start] == baseline[596:start], name + " pre-FAT differs"
        assert data[start + size:] == baseline[start + size:], name + " post-FAT differs"
    r14_efi = dict(report["efi_file_hashes"])
    r15_efi = base.file_hashes(R15_FINAL / "final-extracted-nova/EFI")
    r17_efi = base.file_hashes(R17_FINAL / "final-extracted-nova/EFI")
    r14_nonkernel = {k: v for k, v in r14_efi.items() if k != KERNEL_REL}
    r15_nonkernel = {k: v for k, v in r15_efi.items() if k != KERNEL_REL}
    r17_nonkernel = {k: v for k, v in r17_efi.items() if k != KERNEL_REL}
    assert set(r15_efi) == set(r14_efi) == set(r17_efi)
    assert r14_nonkernel == r15_nonkernel == r17_nonkernel
    assert r15_efi[KERNEL_REL] != r14_efi[KERNEL_REL]
    assert r17_efi[KERNEL_REL] != r15_efi[KERNEL_REL]
    assert sorted(report17["preserved_efi_files"]) == sorted(r14_nonkernel)
    assert report17["payload_kernel_sha256"] == r17_efi[KERNEL_REL]

    out = args.output.resolve()
    assert out.is_relative_to(HERE) and not out.exists()
    out.mkdir(parents=True)
    ev = out / "evidence-nova"
    ev.mkdir()
    tmp = out / "tmp-nova"
    tmp.mkdir()
    env = dict(os.environ, PATH="/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin",
               TMPDIR=str(tmp), TZ="UTC", MTOOLS_SKIP_CHECK="1",
               PYTHONDONTWRITEBYTECODE="1")
    for k in ("PYTHONPATH", "PYTHONHOME", "BASH_ENV", "ENV"):
        env.pop(k, None)
    raw_before = out / "raw-kernel-before-patch-nova.macho"
    shutil.copy2(raw, raw_before)
    assert sha(raw_before) == build["raw_kernel_sha256"]
    payload = out / "kernel-payload-nova.macho"
    shutil.copy2(raw, payload)
    base.command([sys.executable, base.PATCHER, payload, *base.PATCH_ARGS],
                 env=env, log=ev / "entry-patch-nova.log")
    patched = base.entry_info(payload)
    assert patched["entry"] == entry["entry"]
    assert patched["branch_target"] == entry["entry"] + 0x78
    payload_hash = sha(payload)
    pan_path = ev / "payload-pan-nova.json"
    base.command([sys.executable, base.PAN_TOOL, payload, "--output", pan_path],
                 env=env, log=ev / "pan-check-nova.log")
    pan = json.loads(pan_path.read_text())
    assert pan["passed"] and pan["sha256"] == payload_hash
    payload_bytes = payload.read_bytes()
    assert len(payload_bytes) == raw.stat().st_size
    assert payload_bytes != raw.read_bytes()
    markers = {m: m.encode() in payload_bytes for m in (
        "nova: life v=16", "meizu: sbfault-regs", "nova: sblong v=15",
        "meizu: selection attempt=")}
    assert all(markers.values()), markers
    nm = subprocess.run(["/usr/bin/nm", "-g", str(payload)],
                        capture_output=True, text=True)
    symbols = {line.split()[-1] for line in nm.stdout.splitlines() if line.strip()}
    streaming_symbols = {s: s in symbols for s in (
        "_native_sha256_stream_init", "_native_sha256_stream_update",
        "_native_sha256_stream_final", "_native_sha256_digest")}
    assert all(streaming_symbols.values()), streaming_symbols

    fat = out / "payload-nova.img"
    fat.write_bytes(baseline[start:start + size])
    os.utime(payload, (base.EPOCH, base.EPOCH))
    base.command([base.MCOPY, "-o", "-m", "-i", fat, payload, "::/EFI/" + KERNEL_REL],
                 env=env, log=ev / "fat-replace-kernel-nova.log")
    fat_bytes = fat.read_bytes()
    assert len(fat_bytes) == size == 90 * 1024 * 1024
    final = bytearray(baseline)
    final[start:start + size] = fat_bytes
    final[576:596] = hashlib.sha1(final[page:page + ks] + struct.pack("<I", ks) +
                                  fat_bytes + struct.pack("<I", size) +
                                  struct.pack("<I", 0)).digest()
    image = out / IMAGE_NAME
    image.write_bytes(final)
    checked = image.read_bytes()
    assert base.boot_layout(checked) == layout
    assert checked[:576] == baseline[:576] == r17_bytes[:576]
    assert checked[596:start] == baseline[596:start] == r17_bytes[596:start]
    assert checked[start + size:] == baseline[start + size:] == r17_bytes[start + size:]

    extracted_fat = out / "final-extracted-payload-nova.img"
    extracted_fat.write_bytes(checked[start:start + size])
    extracted = out / "final-extracted-nova"
    extracted.mkdir()
    base.command([base.MCOPY, "-i", extracted_fat, "-s", "::/EFI", extracted],
                 env=env, log=ev / "final-extract-nova.log")
    expected = dict(r14_efi)
    expected[KERNEL_REL] = payload_hash
    actual = base.file_hashes(extracted / "EFI")
    assert actual == expected, "EFI changed beyond kernel"
    preserved = sorted(set(expected) - {KERNEL_REL})
    for rel in preserved:
        assert r7.same_bytes(extracted / "EFI" / rel,
                             R14_FINAL / "final-extracted-nova/EFI" / rel), rel
        assert r7.same_bytes(extracted / "EFI" / rel,
                             R17_FINAL / "final-extracted-nova/EFI" / rel), rel
    assert sha(extracted / "EFI/UniXNU/Payloads/scratch-data.img") == r14_efi["UniXNU/Payloads/scratch-data.img"]
    assert sha(extracted / "EFI/UniXNU/Payloads/kernel.macho.trustcache") == r14_efi["UniXNU/Payloads/kernel.macho.trustcache"]
    assert sha(raw) == build["raw_kernel_sha256"]
    manifest = {
        "schema": "nova-r18-streamsha-package-manifest-v1",
        "new_file_no_previous_version": True,
        "status": "BUILT_AWAIT_NEW_USER_INSTALL_COMMAND",
        "installation_allowed": False,
        "install_authorized": False,
        "do_not_flash": True,
        "hardware_tested": False,
        "device_accessed": False,
        "diagnostic_only": False,
        "streaming_sha_adapter_only": True,
        "kernel_built": True,
        "kernel_only_replacement": True,
        "envelope": {
            "source": str(R14_IMG), "sha256": R14_IMG_SHA,
            "accepted_report": str(R14_VER), "accepted_report_sha256": R14_VER_SHA,
            "r15_equality_checked": True, "r15_image": str(R15_IMG),
            "r15_image_sha256": R15_IMG_SHA,
            "r17_baseline": str(R17_IMG), "r17_baseline_sha256": R17_IMG_SHA,
            "r17_report": str(R17_VER), "r17_report_sha256": R17_VER_SHA,
            "r17_equality_checked": True,
        },
        "build_result": str(args.build_result), "build_result_sha256": sha(args.build_result),
        "source_hashes": sources,
        "objects": {name: {"path": objects[name]["path"], "sha256": objects[name]["sha256"],
                           "exact_tu_match": True, "matches_green_tu": True}
                    for name in objects},
        "raw_kernel": str(raw), "raw_kernel_sha256": build["raw_kernel_sha256"],
        "raw_before_patch": str(raw_before), "raw_before_patch_sha256": sha(raw_before),
        "payload_kernel": str(payload), "payload_kernel_sha256": payload_hash,
        "payload_markers": sorted(markers),
        "streaming_symbols": streaming_symbols,
        "streaming_symbols_present": all(streaming_symbols.values()),
        "payload_differs_from_raw": payload_bytes != raw.read_bytes(),
        "payload_size_equals_raw": len(payload_bytes) == raw.stat().st_size,
        "candidate_image": str(image), "candidate_image_sha256": sha(image),
        "candidate_image_bytes": len(checked),
        "fat_bytes": size,
        "changed_efi_files": [KERNEL_REL],
        "preserved_efi_files": preserved,
        "non_kernel_efi_byte_identical": True,
        "r17_nonkernel_efi_equality_verified": True,
        "firmware_header_dtb_avb_byte_identical": True,
        "toml_byte_identical": True,
        "scratch_data_trustcache_byte_identical": True,
        "android_v0_sha1_recomputed": True,
        "new_avb_signature_generated": False,
        "pan_verified": True,
        "pan_tool_sha256": r9.PAN_TOOL_SHA,
        "packer_sha256": sha(Path(__file__)),
        "path_shim_sha256": build["path_shim_sha256"],
        "r17_logger_fix_retained": True,
        "performance_claim": False,
        "unresolved": [
            "36 s pre-receive display wait not fixed",
            "post-Hello black screen not fixed",
            "warm Data SHA saving requires handset verification",
        ],
        "scope": "R18 warm-Data streaming SHA-256 adapter only; accepted R14 envelope "
                 "preserved; only EFI/UniXNU/Payloads/kernel.macho replaced; R17 logger "
                 "fix retained; no installer, no installation permission, no device "
                 "action, hardware_tested=false; no performance claim.",
    }
    (ev / "verification-nova.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(json.dumps({k: manifest[k] for k in (
        "status", "candidate_image", "candidate_image_sha256",
        "payload_kernel_sha256", "installation_allowed", "do_not_flash",
        "diagnostic_only")}, indent=2))


if __name__ == "__main__":
    main()
