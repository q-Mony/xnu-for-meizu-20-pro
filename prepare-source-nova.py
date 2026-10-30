#!/usr/bin/env python3
"""Prepare THIS source-only checkout for a local kernel build.

Safety rules:
  * writes only inside this checkout;
  * never runs a device, compiler, or package-manager command;
  * never invokes git clean/reset or deletes a dirty cache silently;
  * network is opt-in via --fetch-references.

The actual XNU work tree in the original Nova workspace is a modified source tree,
not a disposable cache.  This helper rehydrates it from sources-nova/xnu on top of
a clean checkout of the pinned upstream commit, so the build script sees a normal
Git work tree with the Nova changes present.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MAIN = ROOT / "unixnu-main-nova"
OUT_ROOT = MAIN / "out"
OUT = OUT_ROOT / "meizu20pro"
TREE = OUT / "tree"
FEEDS = OUT_ROOT / "feeds"
PINS_PATH = ROOT / "provenance" / "upstream-pins-nova.json"
INVENTORY = ROOT / "provenance" / "inventory-nova.json"
CONFIG = ROOT / "configs-nova" / "meizu20pro-r18.config"
OWNER = ROOT / "configs-nova" / "owner-data-volume.h"

WORK_ITEMS = {
    "xnu": ("sources-nova/xnu", "tree/.cache/xnu-work"),
    "libdispatch": ("sources-nova/libdispatch", "tree/.cache/libdispatch-work"),
    "libpthread": ("sources-nova/libpthread", "tree/.cache/libpthread-work"),
}
CRITICAL = {
    "sources-nova/xnu/libkern/libkern/crypto/native_sha256.h":
        "faa8a909cf7b5ec353d87b2e1de1258fa33d212906b2617b2a838009d03dc742",
    "sources-nova/xnu/libkern/crypto/corecrypto_sha2.c":
        "a5996632c820928896bd9b2f8790ce703cbde838925343c0c809b193ecb56172",
    "unixnu-main-nova/drivers/NativeRamBlock/NativeRamBlock.cpp":
        "08437deda6354ff660a41af7f2ea56ada41c90183a78f1935f0ae326402cbacb",
}


class PrepareError(RuntimeError):
    pass


def run(cmd, cwd=None, check=True):
    result = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)
    if check and result.returncode != 0:
        raise PrepareError(f"command failed ({result.returncode}): {' '.join(map(str, cmd))}\n"
                           f"{result.stderr.strip()[:2000]}")
    return result


def git(directory, *args, check=True):
    return run(["git", "-C", str(directory), *args], check=check)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_json(path: Path):
    if not path.is_file():
        raise PrepareError(f"missing {path.relative_to(ROOT)}")
    return json.loads(path.read_text())


def inside(path: Path) -> bool:
    try:
        return path.resolve().is_relative_to(ROOT.resolve())
    except (OSError, RuntimeError):
        return False


def ensure_inside(path: Path) -> Path:
    """Reject a mutation destination unless it is a real path inside ROOT.

    The check is deliberately lexical as well as resolved: a symlink that points
    back inside the checkout is still rejected, because this helper must never
    follow or remove a link to decide where to write.
    """
    try:
        rel = path.relative_to(ROOT)
    except ValueError:
        raise PrepareError(f"refusing to write outside checkout: {path}") from None
    if not rel.parts or ".." in rel.parts:
        raise PrepareError(f"refusing ambiguous mutation destination: {path}")
    current = ROOT
    for part in rel.parts:
        current = current / part
        if current.is_symlink():
            raise PrepareError(
                f"refusing to write through symlink: {current.relative_to(ROOT)}")
    if not inside(path):
        raise PrepareError(f"refusing to write outside checkout: {path}")
    return path


def guard_tree_destinations(pins: dict) -> None:
    """Guard every mutation destination/ancestor before any mkdir/copy/move/clone."""
    destinations = [
        OUT_ROOT,
        OUT,
        TREE,
        TREE / ".cache",
        TREE / "build",
        TREE / "build" / "xnu",
        OUT_ROOT / "feeds",
        OUT / ".config",
        TREE / "build" / "xnu" / "owner-data-volume.h",
    ]
    for destination in destinations:
        ensure_inside(destination)
    for name in WORK_ITEMS:
        if name not in pins:
            raise PrepareError(f"missing upstream pin for {name}")
        _, cache_rel = WORK_ITEMS[name]
        ensure_inside(TREE / Path(cache_rel).relative_to("tree"))
        ensure_inside(feed_dir(pins[name]))


def feed_dir(pin: dict) -> Path:
    key = hashlib.sha1(f"{pin['url']}\n{pin.get('ref', '')}".encode()).hexdigest()[:8]
    return FEEDS / f"{Path(pin['url']).stem}-{key}"


def head(directory: Path) -> str | None:
    if not (directory / ".git").exists():
        return None
    try:
        return git(directory, "rev-parse", "HEAD").stdout.strip()
    except PrepareError:
        return None


def is_dirty(directory: Path) -> bool:
    result = git(directory, "status", "--porcelain", check=False)
    return bool(result.stdout.strip())


def snapshot_files(source: Path) -> list[Path]:
    result = []
    for root, dirs, files in os.walk(source):
        dirs[:] = sorted(d for d in dirs if d != ".git")
        for name in sorted(files):
            p = Path(root) / name
            if p.is_symlink():
                raise PrepareError(f"snapshot contains a symlink: {p.relative_to(ROOT)}")
            result.append(p)
    return result


def snapshot_matches(cache: Path, source: Path) -> bool:
    if not (cache / ".git").exists():
        return False
    for path in snapshot_files(source):
        rel = path.relative_to(source)
        target = cache / rel
        if not target.is_file() or sha256(target) != sha256(path):
            return False
    return True


def backup_path(path: Path) -> Path:
    stamp = time.strftime("%Y%m%dT%H%M%S%z")
    candidate = path.with_name(path.name + f".before-rehydrate-{stamp}")
    while candidate.exists():
        candidate = candidate.with_name(candidate.name + "-1")
    ensure_inside(candidate)
    return candidate


def clone_at(source: Path, destination: Path, commit: str) -> None:
    ensure_inside(destination)
    ensure_inside(destination.parent)
    if destination.exists():
        raise PrepareError(f"refusing to clone over existing path: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", "--no-hardlinks", "--no-tags", str(source), str(destination)])
    git(destination, "checkout", "--detach", commit)
    if head(destination) != commit:
        raise PrepareError(f"clone did not land on {commit}: {destination}")


def fetch_at(url: str, ref: str, destination: Path, commit: str) -> None:
    ensure_inside(destination)
    ensure_inside(destination.parent)
    destination.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", "--depth", "1", "--branch", ref, "--no-tags", url, str(destination)])
    if head(destination) != commit:
        git(destination, "fetch", "--depth", "1", "origin", commit)
        git(destination, "checkout", "--detach", commit)
    if head(destination) != commit:
        raise PrepareError(f"fetched checkout is not at {commit}: {destination}")


def ensure_feed(name: str, pin: dict, reference_root: Path | None, fetch: bool) -> Path | None:
    destination = feed_dir(pin)
    ensure_inside(destination)
    if head(destination) == pin["commit"]:
        return destination
    if destination.exists():
        raise PrepareError(
            f"feed checkout {destination.relative_to(ROOT)} is not at pinned commit {pin['commit']}; "
            "move it aside before preparing")
    reference = reference_root / name if reference_root else None
    if reference and head(reference) == pin["commit"]:
        clone_at(reference, destination, pin["commit"])
        return destination
    if reference and reference.exists():
        raise PrepareError(f"reference {reference} is not at pinned commit {pin['commit']}")
    if fetch:
        fetch_at(pin["url"], pin["ref"], destination, pin["commit"])
        return destination
    return None


def overlay(source: Path, destination: Path, prune: bool) -> dict:
    ensure_inside(destination)
    files = snapshot_files(source)
    written = 0
    unchanged = 0
    for path in files:
        rel = path.relative_to(source)
        target = destination / rel
        ensure_inside(target)
        if target.is_file() and target.stat().st_size == path.stat().st_size and sha256(target) == sha256(path):
            unchanged += 1
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        written += 1
    removed = 0
    if prune and (destination / ".git").exists():
        source_set = {p.relative_to(source).as_posix() for p in files}
        tracked = git(destination, "ls-files", "-z").stdout.split("\x00")
        for rel in tracked:
            if not rel or rel in source_set:
                continue
            target = destination / rel
            if target.is_file() or target.is_symlink():
                ensure_inside(target)
                target.unlink()
                removed += 1
    return {"written": written, "unchanged": unchanged, "removed_tracked": removed}


def prepare_item(name: str, pin: dict, reference_root: Path | None, fetch: bool,
                 replace_cache: bool, dry_run: bool) -> dict:
    snapshot_rel, cache_rel = WORK_ITEMS[name]
    source = ROOT / snapshot_rel
    cache = TREE / Path(cache_rel).relative_to("tree")
    ensure_inside(cache)
    ensure_inside(cache.parent)
    if not source.is_dir():
        raise PrepareError(f"missing snapshot: {snapshot_rel}")
    feed = ensure_feed(name, pin, reference_root, fetch) if not dry_run else feed_dir(pin)
    record = {
        "name": name,
        "snapshot": snapshot_rel,
        "cache": str(cache.relative_to(ROOT)),
        "snapshot_files": len(snapshot_files(source)),
        "feed": str(feed.relative_to(ROOT)),
        "feed_ready": bool(head(feed) == pin["commit"]),
        "cache_present": (cache / ".git").exists(),
    }
    if dry_run:
        record["plan"] = ("restore snapshot over clean pinned clone" if not (cache / ".git").exists()
                          else "overlay snapshot (idempotent)")
        return record

    if (cache / ".git").exists():
        current = head(cache)
        if current != pin["commit"]:
            raise PrepareError(
                f"{cache.relative_to(ROOT)} is not at pinned base {pin['commit']} (HEAD {current}); "
                "move it away or pass --replace-cache")
        if snapshot_matches(cache, source):
            record["action"] = "already-current"
            return record
        if is_dirty(cache) and not replace_cache:
            raise PrepareError(
                f"{cache.relative_to(ROOT)} has local changes that do not match the exported snapshot; "
                "refusing to overwrite. Re-run with --replace-cache (old cache is moved, not deleted).")
        if replace_cache:
            backup = backup_path(cache)
            shutil.move(str(cache), str(backup))
            record["backup"] = str(backup.relative_to(ROOT))
    if not (cache / ".git").exists():
        if not feed or head(feed) != pin["commit"]:
            raise PrepareError(
                f"need a pinned reference for {name}; pass --reference-root DIR or --fetch-references")
        clone_at(feed, cache, pin["commit"])
    record.update(overlay(source, cache, prune=True))
    record["action"] = "restored"
    return record


def prepare_config(dry_run: bool) -> dict:
    if not CONFIG.is_file():
        raise PrepareError("missing configs-nova/meizu20pro-r18.config")
    target = OUT / ".config"
    ensure_inside(target)
    ensure_inside(target.parent)
    record = {"config": str(target.relative_to(ROOT)),
              "source": str(CONFIG.relative_to(ROOT)),
              "sha256": sha256(CONFIG)}
    if dry_run:
        record["plan"] = "select R18 config"
        return record
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file() and sha256(target) == record["sha256"]:
        record["action"] = "already-current"
        return record
    if target.exists():
        backup = target.with_name(target.name + ".before-r18-" + time.strftime("%Y%m%dT%H%M%S%z"))
        ensure_inside(backup)
        shutil.move(str(target), str(backup))
        record["backup"] = str(backup.relative_to(ROOT))
    shutil.copy2(CONFIG, target)
    record["action"] = "selected"
    return record


def prepare_owner(dry_run: bool) -> dict:
    if not OWNER.is_file():
        raise PrepareError("missing configs-nova/owner-data-volume.h")
    target = TREE / "build" / "xnu" / "owner-data-volume.h"
    ensure_inside(target)
    ensure_inside(target.parent)
    record = {"owner_header": str(target.relative_to(ROOT)),
              "source": str(OWNER.relative_to(ROOT)),
              "sha256": sha256(OWNER)}
    if dry_run:
        record["plan"] = "install generated source input"
        return record
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file() and sha256(target) == record["sha256"]:
        record["action"] = "already-current"
        return record
    if target.exists():
        backup = target.with_name(target.name + ".before-r18-" + time.strftime("%Y%m%dT%H%M%S%z"))
        ensure_inside(backup)
        shutil.move(str(target), str(backup))
        record["backup"] = str(backup.relative_to(ROOT))
    shutil.copy2(OWNER, target)
    record["action"] = "installed"
    return record


def verify_only(all_files: bool = False) -> int:
    inventory = load_json(INVENTORY)
    files = inventory["files"]
    if not all_files:
        files = [e for e in files
                 if e["path"].startswith(("sources-nova/", "configs-nova/"))]
    bad = []
    for entry in files:
        path = ROOT / entry["path"]
        if not path.is_file():
            bad.append({"path": entry["path"], "label": "missing"})
        elif sha256(path) != entry["sha256"]:
            bad.append({"path": entry["path"], "label": "hash-mismatch"})
    for rel, expected in CRITICAL.items():
        path = ROOT / rel
        if not path.is_file() or sha256(path) != expected:
            bad.append({"path": rel, "label": "critical-mismatch"})
    symlinks = []
    for base in (ROOT / "sources-nova", ROOT / "configs-nova"):
        for root, dirs, names in os.walk(base):
            for name in names:
                p = Path(root) / name
                if p.is_symlink():
                    symlinks.append(str(p.relative_to(ROOT)))
    result = {"schema": "nova-prepare-verify-v1", "checked": len(files), "bad": bad,
              "symlinks": symlinks, "pass": not bad and not symlinks}
    print(json.dumps(result, indent=2))
    return 0 if result["pass"] else 3


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="print the plan; write/network nothing")
    parser.add_argument("--verify-only", action="store_true", help="verify exported snapshot hashes")
    parser.add_argument("--all", action="store_true", help="with --verify-only, check every inventory file")
    parser.add_argument("--reference-root", type=Path, help="directory containing pinned xnu/libdispatch/libpthread checkouts")
    parser.add_argument("--fetch-references", action="store_true", help="explicitly clone pinned public upstreams")
    parser.add_argument("--replace-cache", action="store_true", help="move a mismatched cache aside before restoring")
    args = parser.parse_args()

    if args.verify_only:
        return verify_only(args.all)

    pins = load_json(PINS_PATH)
    reference_root = args.reference_root.resolve() if args.reference_root else None
    if reference_root and not reference_root.is_dir():
        raise PrepareError(f"--reference-root is not a directory: {reference_root}")
    for required in (CONFIG, OWNER, MAIN, PINS_PATH, INVENTORY):
        if not required.exists():
            raise PrepareError(f"not a complete export checkout: missing {required}")

    # Guard every mutation destination/ancestor before the first mkdir/copy/move.
    # This must happen before prepare_config/prepare_owner, which previously wrote
    # .config/owner-data-volume.h through an out/tree symlink before any check.
    guard_tree_destinations(pins)

    if not args.dry_run:
        (TREE / ".cache").mkdir(parents=True, exist_ok=True)
        (TREE / "build" / "xnu").mkdir(parents=True, exist_ok=True)

    result = {"schema": "nova-prepare-v1", "root": str(ROOT), "dry_run": args.dry_run,
              "network": bool(args.fetch_references and not args.dry_run),
              "items": [], "config": prepare_config(args.dry_run),
              "owner": prepare_owner(args.dry_run)}
    for name in WORK_ITEMS:
        result["items"].append(prepare_item(
            name, pins[name], reference_root, args.fetch_references, args.replace_cache, args.dry_run))
    result["next"] = ("run ./unixnu-nova build kernel -j8" if not args.dry_run
                      else "re-run without --dry-run after providing a reference root or --fetch-references")
    result["limitations"] = [
        "No build was run by this helper.",
        "Original kernelcache/firmware/DTB/signed-root inputs and warm Data are user-supplied.",
        "UEFI Mu_Basecore/OpenSSL are pinned manifests; Binaries/meizu20pro is excluded.",
        "The helper never deletes a mismatched cache; --replace-cache moves it aside with a timestamp.",
    ]
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PrepareError as exc:
        print(f"prepare-source-nova: {exc}", file=sys.stderr)
        sys.exit(2)
