# R14 marker-compat manual control-flow review

Scope: `_watchdog_warm_policy`, `_watchdog_warm_file` and
`_watchdog_verify_existing` are the only Policy functions changed from R13.
The other Policy bodies are byte-identical to R13, and the cold entry is unchanged.
Frozen R14 addresses: warm_policy `0xfffffff00895acf0`
(732 bytes), warm_file
`0xfffffff00895b034` (540 bytes),
verify_existing `0xfffffff00895b250`
(1576 bytes).
Every assertion is a function-relative 32-bit word checked in both raw and
payload. Branch entries below give the exact condition code/word and target;
named calls and literal addresses are resolved independently.

1. warm_policy directory and context (0x0cc-0x2d8)
- 0x0cc `cmp w0,#17`; 0x0d0 `b.eq` (EQ) to +0xd8; 0x0d4 `cbnz w22` (NE) to
  +0x84 returns any other mkdir error. 0x0d8 `cmp w22,#0`; 0x0dc `cset w26,eq`
  records created_directory.
- Directory open is 0x20100000 at 0x0e0. Probe loads/stores report the actual
  mode/nlink/ino/uid/gid/dev at 0x1a0-0x1c8.
- 0x1d4 `mov w9,#0x41ed`; 0x1d8 `cmp w8,w9`; 0x1dc `b.ne` (NE) to +0x200
  rejects every non-040755 type/mode before any owner predicate.
- Original tuples: 0x1e4 `cmp w8,#501`; 0x1e8 `b.ne` to 0x208; 0x1ec
  `cmp w9,#20`; 0x1f0 `b.ne` to 0x208; 0x1f4/0x1f8 accept 501:20 and 0x1fc
  branches to +0x264. 0x208 `orr w10,w8,w9`; 0x20c `cbnz` to 0x21c; 0x218
  branches to +0x264 for 0:0. 0x21c `orr w10,w8,w22`; 0x220 `cbnz` to 0x234;
  0x224 `cmp w9,#20`; 0x228 `b.ne`; 0x230 branches to +0x264 for created 0:20.
- Exact 99:99: 0x238 `cmp w8,#99`; 0x23c `b.ne` to +0x158; 0x240 `cbz w22`
  (mkdir_errno==0) to +0x158 rejects a created 99:99; 0x244 `cmp w9,#99`;
  0x248 `b.ne` to +0x158. 0x24c/0x250 resolve the R13 owner marker literal and
  0x254 calls `_printf`.
- Local allow context is computed only after that validation: 0x258 reloads
  uid/gid; 0x25c `cmp w9,#99`; 0x260 `cset w26,eq` (gid99); 0x264 `cmp w22,#0`;
  0x268 `mov w9,#99`; 0x26c `ccmp w8,w9,#0,ne`; 0x270 `cset w22,eq`
  (!created && uid99); 0x288 `and w5,w22,w26` yields the exact conjunction.
- 0x29c `mov w4,#0` (plist is_marker=false), 0x2a0 BL `_watchdog_warm_file`;
  0x2bc reuses the same gate, 0x2d0 `mov w4,#1` (marker), 0x2d4 BL
  `_watchdog_warm_file`. The plist cannot gain allow because the callee masks
  the flag with is_marker.
- Error/close precedence 0x158-0x194 is unchanged: negative dirfd skips close,
  a close success restores the prior diagnostic op, and the error path returns
  without reaching either warm_file call.

2. warm_file EEXIST/new-create split (0x084-0x218)
- 0x084/0x088 build create flags 0xa01 | 0x20000000 (O_WRONLY|O_CREAT|O_EXCL|
  O_NOFOLLOW_ANY); 0x090 sets mode 0644; 0x0ac calls `_openat`.
- 0x0b4 `cmp w0,#17`; 0x0b8 `b.ne` to +0x64 returns non-EEXIST; only EEXIST
  falls through. 0x0bc `cmp w20,#0`; 0x0c0 `ccmp w24,#0,#4,ne`; 0x0c4
  `cset w4,ne` computes effective compatibility `is_marker && allow`; 0x0dc
  BL `_watchdog_verify_existing`.
- New-create path 0x214 `mov w4,#0` followed by 0x218 B
  `_watchdog_verify_existing`, so a newly created inode is verified with
  compatibility disabled. 0x0e8 skips the plist content write for the empty
  marker. 0x1ec calls `_sys_close`; 0x1f0/0x200 preserve the earlier error over
  a close failure and avoid re-verification after a close failure.

3. verify_existing metadata, gid gate, dev, identity and EOF (0x130-0x484)
- 0x130-0x15c: mode type mask 0xf000 vs S_IFREG 0x8000, uid==0, mode 07777==0644,
  nlink==1; every failure uses a NE/conditional branch to the reject context.
- Marker: 0x160 `cbz w20` to +0x190 selects the plist path. 0x164 size load;
  0x168 `cbnz` rejects nonzero size; 0x16c gid load; 0x170 `cbz` accepts gid0;
  0x174 `cmp w11,#20`; 0x178 `b.eq` accepts gid20; 0x17c sets the gid99
  candidate; 0x180 `cbz w19` rejects gid99 when allow=false; 0x184
  `cmp w11,#99`; 0x188 `b.eq` accepts gid99 only after that gate; 0x18c
  otherwise rejects. Thus gid99 is not a blanket relaxation.
- Plist: 0x190 gid load; 0x194 `cbnz` rejects nonzero gid; 0x198 size load;
  0x19c/0x1a0 accept 356; 0x1a4/0x1a8 accept 222 for the warm policy; all other
  sizes reject.
- Device equality is checked at 0x1ac/0x1b0/0x1b4 and 0x1b8 branches to reject
  context +0x388 BEFORE 0x1e4 calls `_openat` for the read descriptor.
- Read-open flags are 0x4 | 0x20000000 at 0x1c0/0x1c4 with the checked argument
  setup at 0x1c8. Identity op14 is set at 0x238; 0x240-0x24c compare the before
  and opened device and branch to EINVAL on mismatch. The fd re-stat and path
  re-stat calls are at 0x4e4 and 0x574, so both identity domains remain on the
  accepted gid99 path.
- EOF: 0x2c0 skips the content read only for size0; 0x360 sets read length 1;
  0x36c sets op18; 0x37c calls `_read`; 0x380 accepts zero bytes; 0x474 sets
  op19; 0x47c loads the byte count; 0x480 continues only on zero; 0x484 returns
  EINVAL for an extra byte or identity failure.
- Reject diagnostics: 0x38c/0x390 resolve `disabled.migrated`, 0x394/0x398
  resolve `disabled.plist`, 0x39c/0x3a0 select by is_marker. The existing
  metadata report is built at 0x3a4-0x3c4. The R14 reject context sets allow
  at 0x3c0/0x3c8/0x3cc, file_ino x9 at 0x3d0, parent_ino x10 at 0x3d4,
  file_dev w11 at 0x3d8, parent_dev w12 at 0x3dc, and stores slots at
  0x3e0/0x3e4/0x3e8. 0x3ec/0x3f0 bind the R14 reject literal, 0x3f4 BL
  `_printf`, and 0x3f8 returns EINVAL.
- Success log: 0x3fc-0x410 require is_marker, allow, error==0 and gid99.
  0x414/0x418/0x41c place uid and gid99 into stack argument slots; 0x420/0x424
  resolve the unique R14 success literal; 0x430 BL `_printf`. The only entries
  to this block follow close: 0x488 (negative fd) , 0x4c4 (earlier error) and
  0x4c8 (close success) all target +0x3fc. The close itself is 0x4b4, error
  precedence is 0x4b8/0x4c0. The log can be emitted before a later directory
  close and therefore does not imply final boot success.

4. Reused R11/R13 evidence
- 36 pinned R11 caller instruction checks are replayed against the unchanged
  frozen caller object with named targets/relocated offsets.
- The unchanged cold entry keeps its R13 function-relative words, including the
  warm predicate and cold metadata varargs. Its R14 body is byte-identical to R13.
- The linked audit independently requires no direct existing-file mutation calls
  in verify_existing and binds object/raw/payload modulo individually recorded
  relocation words.

This is static machine-code evidence only. It does not prove hardware success,
Hello, runtime directory ownership or speedup.
