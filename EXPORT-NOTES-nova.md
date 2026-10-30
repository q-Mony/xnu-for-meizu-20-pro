# Export notes

- Source-only; local commits only in this dispatch, no push, no build, no device access.
- R18 critical source kept byte-identical; see `docs-nova/SOURCE-MAP.md` (6185 match +
  one explained backup exclusion).
- `sources-nova/xnu` is the actual modified XNU work tree, not cache.
- Vendor `Binaries`, generated outputs, device images, logs, private inputs excluded.
- UEFI Mu_Basecore/OpenSSL are pinned dependencies; `prepare-source-nova.py` does not
  initialize them. See `docs-nova/BUILD.md` for manual pinned setup and Mu patch.
- Historical docs are a broad authored archive, not a complete raw-evidence bundle;
  see `docs-nova/history/README.md` and `docs-nova/LOCAL-ONLY-EVIDENCE.md`.
- Agent rules, including fixed model/executor configuration and no-install-without-a-new-
  explicit-command, are in `AGENTS.md`; the project-root AGENTS source is archived at
  `docs-nova/AGENTS-source-nova.md`.
