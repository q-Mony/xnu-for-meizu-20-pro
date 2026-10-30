# Export notes

- Source-only; no pushed remote changes, no build, no device access.
- R18 critical source kept byte-identical; see `docs-nova/SOURCE-MAP.md`.
- `sources-nova/xnu` is the actual modified XNU work tree, not cache.
- Vendor `Binaries`, generated outputs, device images, logs, private inputs excluded.
- UEFI Mu_Basecore/OpenSSL provided as pinned manifests (+ local patch for Mu_Basecore).
- Historical docs are curated and privacy-sanitized; see `docs-nova/LOCAL-ONLY-EVIDENCE.md`.
