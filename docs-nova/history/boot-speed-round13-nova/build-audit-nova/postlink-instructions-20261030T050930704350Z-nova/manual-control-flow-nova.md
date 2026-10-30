# R13 warm-policy manual control-flow review

Scope: only `_watchdog_warm_policy` changed. The frozen object body starts at
`0xfffffff00895acf0` and is
`664` bytes. All assertions in
`audit-nova.json` are function-relative words checked in both raw and payload;
named BL targets and literal addresses are resolved independently.

1. mkdir result (0x0cc-0x0dc): `cmp w0,#17` + `b.eq` accepts only EEXIST and
   falls through; any other nonzero mkdir error takes the immediate return at
   0x0d4. `mkdir_errno==0` becomes `created_directory` in w26.
2. Directory open uses flags 0x20100000; stat/probe report the actual inode
   fields. The probe printf arguments are the reviewed 8-byte stack slots
   (mkdir_errno/created, mode/uid, gid/nlink, dev/ino) and x0 points at the
   exact R13 source literal.
3. Type and mode (0x1d0-0x1dc): the actual `st_mode` is reloaded and compared
   against 0x41ed = `S_IFDIR|0755`; any non-directory or any other mode
   (including 0777, 0700, setuid, setgid, sticky) branches to EINVAL. A bad
   type/mode never enters the owner predicate.
4. Prior owner cases (0x1e0-0x208): 501:20, 0:0 and this-boot-created 0:20 keep
   their original acceptance paths. The created form is selected by
   `uid|mkdir_errno==0`, so only a successful fresh mkdir enables 0:20.
5. Exact R13 compatibility (0x20c-0x228): the bad-tuple default is EINVAL. The
   code then requires uid==99, then `mkdir_errno!=0` (`cbz` rejects a created
   directory), then gid==99. Only that exact existing 99:99 tuple falls through
   to the marker printf; the created-only rejection is therefore explicit, not
   inferred from the source comment.
6. Marker (0x224-0x230): ADRP/ADD x0 resolves to the unique
   `nova: policy-owner-compat version=13 anonymous_existing=1 uid=99 gid=99`
   literal and BL targets `_printf`; after the call the next instruction is the
   accepted warm-file stage at 0x230. A bad tuple or created 99:99 branches to
   the shared EINVAL/close path at 0x158, which restores the previous diagnostic
   op and returns without reaching the marker.
7. Accepted path (0x230-0x28c): the two `watchdog_warm_file` calls remain the
   original plist/marker calls. R12/R11 file/cold/open checks are retained and
   their function bodies are byte-identical to R12; no file policy was relaxed,
   and no directory write/chown/chmod/normalization was added.

This is static machine-code evidence only. It does not prove the device's
runtime directory tuple, the APFS origin of 99:99, file success, Hello or speed.
