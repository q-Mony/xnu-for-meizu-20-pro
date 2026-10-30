#!/usr/bin/env python3
"""Independent R18 package validation against the accepted R14/R15/R17 envelope.

New file for the R18 build/package pass; no previous version. Recomputes the
Android v0 SHA1, checks the FAT/non-kernel EFI set, DTB/AVB/TOML byte ranges,
R17 byte equality outside the changed kernel, entry patch, PAN evidence,
streaming symbols, logger markers and build/source bindings. Reads the R14
packer/helpers read-only and never writes outside this R18 directory.
"""
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
NOVA = HERE.parents[2]
PKG = HERE / "package/boot-warm-r18-streamsha-candidate-nova"
IMG = PKG / "boot-warm-r18-streamsha-candidate-nova.img"
PAYLOAD = PKG / "kernel-payload-nova.macho"
RAW = PKG / "raw-kernel-before-patch-nova.macho"
EXTRACTED = PKG / "final-extracted-nova/EFI"
MANIFEST = PKG / "evidence-nova/verification-nova.json"
PAN = PKG / "evidence-nova/payload-pan-nova.json"
BUILD = HERE / "build-result-r18-nova.json"
FREEZE = HERE / "source-freeze-r18-nova.json"
R14PACK = NOVA / "artifacts-nova/boot-speed-round14-nova/packaging-nova/pack-watchdog-r14-candidate-nova.py"
R14_FINAL = NOVA / "artifacts-nova/boot-speed-round14-nova/packaging-nova/watchdog-r14-candidate-final-nova"
R14_IMG = R14_FINAL / "boot-warm-watchdog-r14-candidate-nova.img"
R14_VER = R14_FINAL / "evidence-nova/verification-nova.json"
R15_FINAL = NOVA / "artifacts-nova/boot-speed-round15-nova/arm-lock-review-nova/package/sblong-r15-arm-lock-final-nova"
R15_IMG = R15_FINAL / "boot-warm-sblong-r15-candidate-nova.img"
R17_FINAL = (NOVA / "artifacts-nova/boot-speed-round17-nova/log-encode-completion-nova/"
             "package/boot-warm-r17-logfix-candidate-nova")
R17_IMG = R17_FINAL / "boot-warm-r17-logfix-candidate-nova.img"
R17_VER = R17_FINAL / "evidence-nova/verification-nova.json"
R14_IMG_SHA = "6ea4078227980d8c1f0a98982fa6dd37e5a91b251384674dca53699b0f264641"
R15_IMG_SHA = "53470de0d676da3e41cb9c6a894b62b10686e0ef0480799535db0d1e331fe0ed"
R17_IMG_SHA = "8796d6c5824ce563fda8b4d0e1a429f235749254648cc160534e5807283338d6"
R17_VER_SHA = "275fa8644d118453cc23dc3ae1dfc22ecc0448b9ed47e3526995416bbe8fa7ab"
R14_VER_SHA = "036d3dee4a7fc3f506a155b4f1ef88ba110d54fe4e672cc9c2f16920b3810ed6"
SHIM_SHA = "7bf8733a9ae09c886d180b17d49919138c1bcff191063b2b92549c8f7fc3d169"
KERNEL_REL = "UniXNU/Payloads/kernel.macho"


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main():
    spec = importlib.util.spec_from_file_location("r14_pack_readonly", R14PACK)
    r14 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(r14)
    _, (r9, r7, r6, base) = r14.helpers()
    man = json.loads(MANIFEST.read_text())
    pan = json.loads(PAN.read_text())
    build = json.loads(BUILD.read_text())
    freeze = json.loads(FREEZE.read_text())
    report14 = json.loads(R14_VER.read_text())
    report17 = json.loads(R17_VER.read_text())
    baseline = R14_IMG.read_bytes()
    r15 = R15_IMG.read_bytes()
    r17 = R17_IMG.read_bytes()
    candidate = IMG.read_bytes()
    layout = base.boot_layout(baseline)
    start, size, page, ks = (layout[k] for k in ("ramstart", "ramdisk_size", "page", "kernel_size"))
    checks = {}
    checks["r14_pinned"] = sha(R14_IMG) == R14_IMG_SHA and sha(R14_VER) == R14_VER_SHA
    checks["r15_pinned"] = sha(R15_IMG) == R15_IMG_SHA
    checks["r17_pinned"] = sha(R17_IMG) == R17_IMG_SHA and sha(R17_VER) == R17_VER_SHA
    checks["candidate_image_sha256"] = sha(IMG) == man["candidate_image_sha256"]
    checks["payload_sha256"] = sha(PAYLOAD) == man["payload_kernel_sha256"]
    checks["raw_sha256"] = sha(RAW) == man["raw_kernel_sha256"]
    checks["raw_equals_build_raw"] = sha(RAW) == build["raw_kernel_sha256"]
    checks["image_length_96mib"] = len(candidate) == 96 * 1024 * 1024
    checks["layout_ramdisk_90mib"] = layout["ramdisk_size"] == 90 * 1024 * 1024
    checks["candidate_layout_equals_r14"] = base.boot_layout(candidate) == layout
    checks["header_prefix_bytes_equal_r14"] = candidate[:576] == baseline[:576]
    checks["pre_fat_bytes_equal_r14"] = candidate[596:start] == baseline[596:start]
    checks["post_fat_bytes_equal_r14"] = candidate[start + size:] == baseline[start + size:]
    checks["header_prefix_bytes_equal_r17"] = candidate[:576] == r17[:576]
    checks["pre_fat_bytes_equal_r17"] = candidate[596:start] == r17[596:start]
    checks["post_fat_bytes_equal_r17"] = candidate[start + size:] == r17[start + size:]
    fat = candidate[start:start + size]
    recomputed = hashlib.sha1(
        candidate[page:page + ks] + struct.pack("<I", ks) + fat +
        struct.pack("<I", size) + struct.pack("<I", 0)).digest()
    checks["android_v0_sha1_recomputed"] = recomputed == candidate[576:596]
    r14_efi = dict(report14["efi_file_hashes"])
    r17_efi = base.file_hashes(R17_FINAL / "final-extracted-nova/EFI")
    expected = dict(r14_efi)
    expected[KERNEL_REL] = man["payload_kernel_sha256"]
    actual = base.file_hashes(EXTRACTED)
    checks["efi_file_set_and_hashes"] = actual == expected
    preserved = sorted(set(expected) - {KERNEL_REL})
    checks["preserved_efi_files_byte_identical_r14"] = all(
        actual.get(rel) == r14_efi[rel] for rel in preserved)
    checks["preserved_efi_files_byte_identical_r17"] = all(
        actual.get(rel) == r17_efi.get(rel) for rel in preserved)
    checks["toml_byte_identical"] = (
        actual.get("UniXNU/unixnu.toml") == r14_efi["UniXNU/unixnu.toml"] ==
        r17_efi["UniXNU/unixnu.toml"])
    checks["scratch_data_byte_identical_r17"] = (
        actual.get("UniXNU/Payloads/scratch-data.img") ==
        r17_efi["UniXNU/Payloads/scratch-data.img"])
    checks["trustcache_byte_identical_r17"] = (
        actual.get("UniXNU/Payloads/kernel.macho.trustcache") ==
        r17_efi["UniXNU/Payloads/kernel.macho.trustcache"])
    checks["only_kernel_changed_in_efi"] = man["changed_efi_files"] == [KERNEL_REL]
    checks["r15_envelope_equality"] = (
        r15[:576] == baseline[:576] and r15[596:start] == baseline[596:start]
        and r15[start + size:] == baseline[start + size:])
    checks["r17_envelope_equality"] = (
        r17[:576] == baseline[:576] and r17[596:start] == baseline[596:start]
        and r17[start + size:] == baseline[start + size:])
    raw_entry = base.entry_info(RAW)
    patched_entry = base.entry_info(PAYLOAD)
    checks["raw_entry_unpatched"] = raw_entry["branch_target"] == raw_entry["start_first_cpu"]
    checks["payload_entry_patched_once"] = (
        patched_entry["entry"] == raw_entry["entry"] and
        patched_entry["branch_target"] == raw_entry["entry"] + 0x78)
    raw_bytes = RAW.read_bytes()
    payload_bytes = PAYLOAD.read_bytes()
    markers = {m: m.encode() in payload_bytes for m in (
        "nova: life v=16", "meizu: sbfault-regs", "nova: sblong v=15",
        "meizu: selection attempt=")}
    checks["payload_markers"] = all(markers.values())
    nm = subprocess.run(["/usr/bin/nm", "-g", str(PAYLOAD)],
                        capture_output=True, text=True)
    symbols = {line.split()[-1] for line in nm.stdout.splitlines() if line.strip()}
    streaming = {s: s in symbols for s in (
        "_native_sha256_stream_init", "_native_sha256_stream_update",
        "_native_sha256_stream_final", "_native_sha256_digest")}
    checks["streaming_symbols_present"] = all(streaming.values())
    checks["pan_passed"] = bool(pan.get("passed")) and pan.get("sha256") == man["payload_kernel_sha256"]
    checks["pan_tool_pinned"] = man["pan_tool_sha256"] == r9.PAN_TOOL_SHA
    checks["path_shim_pinned"] = man["path_shim_sha256"] == SHIM_SHA
    checks["packer_self_hash"] = man["packer_sha256"] == sha(HERE / "pack-r18-nova.py")
    checks["source_hashes_match"] = all(
        sha(NOVA / rel) == h for rel, h in man["source_hashes"].items())
    checks["source_freeze_pass"] = (
        freeze.get("pass") is True and freeze.get("count") == 6186 and
        freeze.get("new_source_sha256") == man["source_hashes"])
    object_checks = []
    for name in ("corecrypto_sha2", "NativeRamBlock"):
        o = man["objects"][name]
        object_checks.append(
            o["sha256"] == build["objects"][name]["sha256"] == sha(Path(o["path"])) and
            o["exact_tu_match"] is True and o["matches_green_tu"] is True)
    checks["changed_objects_match"] = all(object_checks)
    checks["build_result_hash"] = man["build_result_sha256"] == sha(BUILD)
    checks["flags"] = (
        man["status"] == "BUILT_AWAIT_NEW_USER_INSTALL_COMMAND" and
        man["installation_allowed"] is False and man["install_authorized"] is False and
        man["do_not_flash"] is True and man["hardware_tested"] is False and
        man["device_accessed"] is False and man["new_avb_signature_generated"] is False and
        man["performance_claim"] is False and man["r17_logger_fix_retained"] is True)
    checks["payload_differs_from_raw"] = payload_bytes != raw_bytes
    checks["payload_size_equals_raw"] = len(payload_bytes) == len(raw_bytes)
    result = {
        "schema": "nova-r18-package-validation-v1",
        "new_file_no_previous_version": True,
        "package": str(PKG),
        "manifest": str(MANIFEST),
        "manifest_sha256": sha(MANIFEST),
        "checks": checks,
        "entries": {"raw": raw_entry, "patched": patched_entry},
        "layout": {
            "ramstart": layout["ramstart"],
            "ramdisk_size": layout["ramdisk_size"],
            "page": layout["page"],
            "kernel_size": layout["kernel_size"],
            "footer_magic": layout["footer"][0].decode("ascii", "replace"),
        },
        "hashes": {
            "candidate_image": sha(IMG),
            "payload_kernel": sha(PAYLOAD),
            "raw_kernel": sha(RAW),
            "r14_envelope": sha(R14_IMG),
            "r15_image": sha(R15_IMG),
            "r17_image": sha(R17_IMG),
        },
        "markers": markers,
        "streaming_symbols": streaming,
        "pass": all(checks.values()),
    }
    (HERE / "package-validation-r18-nova.json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({"pass": result["pass"], "checks": checks,
                      "entries": result["entries"], "layout": result["layout"],
                      "hashes": result["hashes"],
                      "streaming_symbols": streaming}, indent=1))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
