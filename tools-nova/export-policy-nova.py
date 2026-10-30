#!/usr/bin/env python3
"""Export policy for the Nova source-only repository.

This is the auditable, testable form of the policy used to build and validate the
export.  It rejects device images, generated binaries, backups, private keys and
high-confidence credential material while allowing ordinary source/docs files.

Run `python3 tools-nova/export-policy-nova.py --test` for the representative
fixture test (writes only to a system temporary directory).
"""
from __future__ import annotations

import argparse
import json
import re
import tempfile
from pathlib import Path

DENY_EXT = {
    ".img", ".ipsw", ".dmg", ".im4p", ".macho", ".o", ".a", ".dylib", ".efi",
    ".fd", ".fv", ".bin", ".mbn", ".fw", ".dtb", ".dbr", ".bc", ".spv",
    ".metallib", ".zip", ".tar", ".gz", ".xz", ".zst", ".zstd", ".pyc",
    ".pdb", ".obj", ".lib", ".dll", ".exe", ".so", ".pem", ".key", ".p12",
    ".pfx", ".jks", ".keystore",
}
IMAGE_EXT = {".svg", ".png", ".jpg", ".jpeg", ".gif", ".webp"}
SKIP_DIRS = {".git", "__pycache__", "node_modules", ".pytest_cache", ".mypy_cache",
             ".ruff_cache", ".tox", ".venv", "DerivedData"}
BACKUP_RE = re.compile(r"\.(bak|pre|old|orig|rej)([-._].*)?$", re.I)
MAX_FILE = 50 * 1024 * 1024
MAX_IMAGE = 8 * 1024 * 1024
PRIVATE_KEY_NAMES = {"id_rsa", "id_dsa", "id_ecdsa", "id_ed25519"}
# Build the private-key marker from pieces so this policy file does not itself
# contain the literal marker that the secret scanner looks for.
_PRIVATE_KEY_MARKER = b"-----BEGIN " + b"[A-Z0-9 ]*" + b"PRIVATE KEY-----"
SECRET_PATTERNS = {
    "private-key": re.compile(_PRIVATE_KEY_MARKER),
    "aws-access-key": re.compile(rb"\bAKIA[0-9A-Z]{16}\b"),
    "github-token": re.compile(rb"\b(?:ghp|gho|ghu|ghs)_[A-Za-z0-9]{20,}\b"),
    "github-pat": re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{20,}\b"),
    "slack-token": re.compile(rb"\bxox[baprs]-[A-Za-z0-9-]{10,}\b"),
}


def _text_head(path: Path) -> bytes:
    try:
        with path.open("rb") as f:
            return f.read(16384)
    except OSError:
        return b""


def is_text(path: Path) -> bool:
    data = _text_head(path)
    if not data:
        return True
    if b"\x00" in data:
        return False
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return False
    controls = sum(1 for b in data if b < 9 or 13 < b < 32)
    return controls * 100 < len(data) * 2


def classify(path: Path, rel: str | None = None) -> tuple[bool, str]:
    rel = rel or path.name
    parts = set(Path(rel).parts)
    name = path.name
    if parts & SKIP_DIRS or name == ".git":
        return False, "vcs-cache"
    if name == ".DS_Store" or name in {".env", "local.toml"} or name.startswith(".env."):
        return False, "local-secret-or-config"
    if BACKUP_RE.search(name) or name.endswith("~"):
        return False, "backup"
    if name in PRIVATE_KEY_NAMES or name.startswith("id_") and not name.endswith(".pub"):
        return False, "ssh-private-key-name"
    try:
        size = path.stat().st_size
    except OSError:
        return False, "unreadable"
    if size >= MAX_FILE:
        return False, "file-too-large"
    ext = path.suffix.lower()
    if ext in DENY_EXT:
        return False, f"deny-extension:{ext}"
    if ext in IMAGE_EXT:
        return (True, "image-doc-asset") if size <= MAX_IMAGE else (False, "image-too-large")
    if ext in {".1", ".2", ".3", ".4", ".5", ".6", ".7", ".8", ".9"}:
        return True, "man-page-text"
    if not is_text(path):
        return False, "binary-content"
    head = _text_head(path)
    for label, pattern in SECRET_PATTERNS.items():
        if pattern.search(head):
            return False, f"secret-content:{label}"
    return True, "source-text-or-text-asset"


def run_test() -> int:
    cases = [
        ("code/main.c", b"int main(void) { return 0; }\n", True),
        ("docs/README.md", b"# docs\n", True),
        ("docs/diagram.svg", b"<svg xmlns='http://www.w3.org/2000/svg'></svg>\n", True),
        ("phone.img", b"not really an image\n", False),
        ("boot.macho", b"binary-ish\n", False),
        ("backup.bak-20260101", b"backup\n", False),
        ("id_ed25519", b"-----BEGIN " + b"OPENSSH PRIVATE KEY-----" + b"\n", False),
        ("token.txt", b"ghp_" + b"A" * 30 + b"\n", False),
        ("blob", b"\x00\x01\x02", False),
        (".env", b"TOKEN=x\n", False),
    ]
    results = []
    with tempfile.TemporaryDirectory(prefix="nova-policy-") as tmp:
        base = Path(tmp)
        for rel, body, expected in cases:
            path = base / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body)
            allowed, reason = classify(path, rel)
            results.append({"case": rel, "expected_allowed": expected,
                            "allowed": allowed, "reason": reason,
                            "pass": allowed == expected})
    passed = all(r["pass"] for r in results)
    print(json.dumps({"schema": "nova-export-policy-test-v1", "pass": passed, "results": results}, indent=2))
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", action="store_true", help="run the representative fixture test")
    parser.add_argument("paths", nargs="*", help="classify these paths (for manual checks)")
    args = parser.parse_args()
    if args.test:
        return run_test()
    failed = 0
    for value in args.paths:
        path = Path(value)
        allowed, reason = classify(path, value)
        print(f"{value}: allowed={allowed} reason={reason}")
        failed += 0 if allowed else 1
    return 0 if not args.paths or failed < len(args.paths) else 1


if __name__ == "__main__":
    raise SystemExit(main())
