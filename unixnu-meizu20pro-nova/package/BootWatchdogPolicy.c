// SPDX-License-Identifier: MIT
// Meizu 20 Pro bring-up only. Called after the owner RAM Data mount succeeds.
#ifdef MEIZU_WATCHDOG_HOST_TEST
#include "tests/WatchdogHostShim.h"
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <pexpert/pexpert.h>
#endif

// NativeRamBlock's persistent overlay and NativeUfs's store selection. A store
// request is refused even when opening it failed: this policy requires an
// explicitly volatile boot, with no writes to the original persistent Data.
extern volatile uint32_t native_data_persist_state;
extern unsigned native_ufs_store_mode(void);

static const char policy[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict><key>com.apple.watchdogd</key><true/></dict></plist>\n";
static const char directory[] = "/private/var/db/com.apple.xpc.launchd";
static const char path[] = "/private/var/db/com.apple.xpc.launchd/disabled.plist";
static const char marker[] = "/private/var/db/com.apple.xpc.launchd/disabled.migrated";

volatile int meizu_watchdog_policy_status = -1;

// R11 observation ABI. Numbers are stable so a short panic can identify the
// failing primitive even when the periodic log has not saved the last second.
enum watchdog_operation {
    WATCHDOG_OP_NONE = 0,
    WATCHDOG_OP_ENABLE_ARG = 1,
    WATCHDOG_OP_PREFLIGHT = 2,
    WATCHDOG_OP_WARM_ARG = 3,
    WATCHDOG_OP_COPYOUT_PATH = 4,
    WATCHDOG_OP_MKDIR = 5,
    WATCHDOG_OP_OPEN_DIRECTORY = 6,
    WATCHDOG_OP_FSTAT = 7,
    WATCHDOG_OP_COPYIN_STAT = 8,
    WATCHDOG_OP_CHECK_DIRECTORY = 9,
    WATCHDOG_OP_OPEN_CREATE = 10,
    WATCHDOG_OP_FSTATAT = 11,
    WATCHDOG_OP_CHECK_METADATA = 12,
    WATCHDOG_OP_OPEN_READ = 13,
    WATCHDOG_OP_CHECK_IDENTITY = 14,
    WATCHDOG_OP_READ_CONTENT = 15,
    WATCHDOG_OP_COPYIN_CONTENT = 16,
    WATCHDOG_OP_CHECK_CONTENT = 17,
    WATCHDOG_OP_READ_EOF = 18,
    WATCHDOG_OP_CHECK_EOF = 19,
    WATCHDOG_OP_COPYOUT_CONTENT = 20,
    WATCHDOG_OP_WRITE_CONTENT = 21,
    WATCHDOG_OP_FCHOWN = 22,
    WATCHDOG_OP_FCHMOD = 23,
    WATCHDOG_OP_FSYNC = 24,
    WATCHDOG_OP_CLOSE_FILE = 25,
    WATCHDOG_OP_CLOSE_DIRECTORY = 26,
};
static int policy_diag_warm_found = -1;
static int policy_diag_warm_value;
static int policy_diag_branch = -1; // -1 unselected, 0 disabled, 1 cold, 2 warm.
static int policy_diag_op;
static unsigned policy_diag_calls;
static const char *policy_diag_stage = "not called";

const char *
meizu_watchdog_policy_diagnostic(int *warm_found, int *warm_value, int *branch, int *op)
{
    if (warm_found) *warm_found = policy_diag_warm_found;
    if (warm_value) *warm_value = policy_diag_warm_value;
    if (branch) *branch = policy_diag_branch;
    if (op) *op = policy_diag_op;
    return policy_diag_stage;
}

static void
watchdog_diag_report(int error)
{
    printf("nova: policy-diag version=11 event=result call=%u warm_found=%d warm_value=%d branch=%d op=%d stage=%s errno=%d\n",
        policy_diag_calls, policy_diag_warm_found, policy_diag_warm_value,
        policy_diag_branch, policy_diag_op, policy_diag_stage, error);
}

// Cleanup must not replace the operation that first failed. A close failure
// becomes the diagnostic operation only if every earlier operation succeeded.
static int
watchdog_close(proc_t p, int fd, int error, int operation)
{
    int32_t result = 0;
    const int previous_op = policy_diag_op;
    struct close_args cl = {.fd = fd};
    policy_diag_op = operation;
    int close_error = sys_close(p, &cl, &result);
    if (error) policy_diag_op = previous_op;
    return error ? error : close_error;
}

// Exact R8 launchd serialization, reviewed from the pinned warm Data capture.
// Preserve all four decisions; do not replace this with the one-label cold
// policy or accept arbitrary plists containing a watchdogd substring.
static const char warm_policy[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n<dict>\n"
    "\t<key>com.apple.watchdogd</key>\n\t<true/>\n"
    "\t<key>com.apple.dhcp6d</key>\n\t<true/>\n"
    "\t<key>com.apple.bootpd</key>\n\t<true/>\n"
    "\t<key>com.apple.ftp-proxy-embedded</key>\n\t<false/>\n"
    "</dict>\n</plist>\n";

static int
watchdog_stat(proc_t p, int fd, uint64_t scratch, struct stat64 *st)
{
    int32_t result = 0;
    struct fstat64_args request = {.fd = fd, .ub = scratch + 0x800};
    policy_diag_op = WATCHDOG_OP_FSTAT;
    int error = sys_fstat64(p, &request, &result);
    if (error) return error;
    policy_diag_op = WATCHDOG_OP_COPYIN_STAT;
    return copyin(request.ub, st, sizeof(*st));
}

static int
watchdog_stat_at(proc_t p, int dirfd, uint64_t scratch, struct stat64 *st)
{
    int32_t result = 0;
    struct fstatat64_args request = {.fd = dirfd, .path = scratch, .ub = scratch + 0x800,
        .flag = AT_SYMLINK_NOFOLLOW_ANY};
    policy_diag_op = WATCHDOG_OP_FSTATAT;
    int error = fstatat64(p, &request, &result);
    if (error) return error;
    policy_diag_op = WATCHDOG_OP_COPYIN_STAT;
    return copyin(request.ub, st, sizeof(*st));
}

static int
watchdog_file_metadata(const struct stat64 *st, int is_marker, int allow_marker_gid99)
{
    if (!S_ISREG(st->st_mode) || st->st_uid != 0 || st->st_nlink != 1 ||
        (st->st_mode & 07777) != 0644) {
        return 0;
    }
    if (is_marker) {
        return st->st_size == 0 &&
            (st->st_gid == 0 || st->st_gid == 20 ||
             (allow_marker_gid99 && st->st_gid == 99));
    }
    return st->st_gid == 0 && (st->st_size == (off_t)(sizeof(policy) - 1) ||
                               st->st_size == (off_t)(sizeof(warm_policy) - 1));
}

static void
watchdog_metadata_report(const struct stat64 *st, int is_marker)
{
    printf("nova: policy-metadata probe=12 kind=%s mode=0%o uid=%u gid=%u size=%lld nlink=%u\n",
        is_marker ? "disabled.migrated" : "disabled.plist", (unsigned)st->st_mode,
        (unsigned)st->st_uid, (unsigned)st->st_gid, (long long)st->st_size,
        (unsigned)st->st_nlink);
}

static int
watchdog_same_file(const struct stat64 *a, const struct stat64 *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_mode == b->st_mode &&
        a->st_uid == b->st_uid && a->st_gid == b->st_gid && a->st_nlink == b->st_nlink &&
        a->st_size == b->st_size;
}

// scratch holds a basename under the retained, checked directory. Reject
// special objects before opening and recheck the opened descriptor and path.
// No existing inode is written, chowned, chmodded, truncated or replaced.
static int
watchdog_verify_existing(proc_t p, int dirfd, uint64_t scratch, int is_marker,
    int allow_marker_gid99, const struct stat64 *parent)
{
    int32_t result = 0;
    int fd = -1, error;
    struct stat64 before = {0}, opened = {0}, after = {0};
    char bytes[512];
    error = watchdog_stat_at(p, dirfd, scratch, &before);
    if (error) goto out;
    policy_diag_op = WATCHDOG_OP_CHECK_METADATA;
    if (!watchdog_file_metadata(&before, is_marker, allow_marker_gid99) || before.st_dev != parent->st_dev) {
        watchdog_metadata_report(&before, is_marker);
        printf("nova: policy-metadata-reject version=14 kind=%s allow_marker_gid99=%d file_dev=%u parent_dev=%u file_ino=%llu parent_ino=%llu\n",
            is_marker ? "disabled.migrated" : "disabled.plist", is_marker && allow_marker_gid99,
            (unsigned)before.st_dev, (unsigned)parent->st_dev,
            (unsigned long long)before.st_ino, (unsigned long long)parent->st_ino);
        error = EINVAL; goto out;
    }
    struct openat_args op = {.fd = dirfd, .path = scratch,
        .flags = O_RDONLY | O_NOFOLLOW_ANY | O_NONBLOCK, .mode = 0};
    policy_diag_op = WATCHDOG_OP_OPEN_READ;
    error = openat(p, &op, &result);
    if (error) goto out;
    fd = result;
    if ((error = watchdog_stat(p, fd, scratch, &opened))) goto out;
    policy_diag_op = WATCHDOG_OP_CHECK_IDENTITY;
    if (!watchdog_same_file(&before, &opened)) { error = EINVAL; goto out; }
    user_ssize_t got = 0;
    struct read_args read_request = {.fd = fd, .cbuf = scratch + 0x200, .nbyte = (size_t)opened.st_size};
    if (opened.st_size) {
        policy_diag_op = WATCHDOG_OP_READ_CONTENT;
        if ((error = read(p, &read_request, &got))) goto out;
        if (got != opened.st_size) { error = EIO; goto out; }
        policy_diag_op = WATCHDOG_OP_COPYIN_CONTENT;
        if ((error = copyin(read_request.cbuf, bytes, (size_t)got))) goto out;
        policy_diag_op = WATCHDOG_OP_CHECK_CONTENT;
        if (!((got == (user_ssize_t)(sizeof(policy) - 1) && !memcmp(bytes, policy, sizeof(policy) - 1)) ||
              (got == (user_ssize_t)(sizeof(warm_policy) - 1) && !memcmp(bytes, warm_policy, sizeof(warm_policy) - 1)))) {
            error = EINVAL; goto out;
        }
    }
    read_request.nbyte = 1;
    policy_diag_op = WATCHDOG_OP_READ_EOF;
    if ((error = read(p, &read_request, &got))) goto out;
    policy_diag_op = WATCHDOG_OP_CHECK_EOF;
    if (got) { error = EINVAL; goto out; }
    if ((error = watchdog_stat(p, fd, scratch, &after))) goto out;
    policy_diag_op = WATCHDOG_OP_CHECK_IDENTITY;
    if (!watchdog_same_file(&opened, &after)) { error = EINVAL; goto out; }
    if ((error = watchdog_stat_at(p, dirfd, scratch, &after))) goto out;
    policy_diag_op = WATCHDOG_OP_CHECK_IDENTITY;
    if (!watchdog_same_file(&opened, &after)) error = EINVAL;
out:
    if (fd >= 0) error = watchdog_close(p, fd, error, WATCHDOG_OP_CLOSE_FILE);
    // The compatibility notice is emitted only after every existing-file check
    // and cleanup succeeded, and only when the gid99 exception was required.
    if (!error && allow_marker_gid99 && is_marker && before.st_gid == 99) {
        printf("nova: policy-marker-compat version=14 active=1 kind=disabled.migrated uid=%u gid=%u\n",
            (unsigned)before.st_uid, (unsigned)before.st_gid);
    }
    return error;
}

static int
watchdog_warm_file(proc_t p, int dirfd, uint64_t scratch, const char *name,
    int is_marker, int allow_marker_gid99, const struct stat64 *parent)
{
    int32_t result = 0;
    policy_diag_op = WATCHDOG_OP_COPYOUT_PATH;
    int error = copyout(name, scratch, strlen(name) + 1);
    if (error) return error;
    struct openat_args create = {.fd = dirfd, .path = scratch,
        .flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW_ANY, .mode = 0644};
    policy_diag_op = WATCHDOG_OP_OPEN_CREATE;
    error = openat(p, &create, &result);
    if (error == EEXIST) {
        // Only an existing marker may use the directory-context gid exception.
        return watchdog_verify_existing(p, dirfd, scratch, is_marker,
            is_marker && allow_marker_gid99, parent);
    }
    if (error) return error;
    const int fd = result;
    if (!is_marker) {
        policy_diag_op = WATCHDOG_OP_COPYOUT_CONTENT;
        error = copyout(policy, scratch + 0x200, sizeof(policy) - 1);
        if (!error) {
            struct write_args wr = {.fd = fd, .cbuf = scratch + 0x200, .nbyte = sizeof(policy) - 1};
            user_ssize_t written = 0;
            policy_diag_op = WATCHDOG_OP_WRITE_CONTENT;
            error = write(p, &wr, &written);
            if (!error && written != (user_ssize_t)(sizeof(policy) - 1)) error = EIO;
        }
    }
    // Only a newly created inode is normalized, including the empty marker.
    struct fchown_args owner = {.fd = fd, .uid = 0, .gid = 0};
    struct fchmod_args mode = {.fd = fd, .mode = 0644};
    struct fsync_args sync = {.fd = fd};
    if (!error) { policy_diag_op = WATCHDOG_OP_FCHOWN; error = fchown(p, &owner, &result); }
    if (!error) { policy_diag_op = WATCHDOG_OP_FCHMOD; error = fchmod(p, &mode, &result); }
    if (!error) { policy_diag_op = WATCHDOG_OP_FSYNC; error = fsync(p, &sync, &result); }
    error = watchdog_close(p, fd, error, WATCHDOG_OP_CLOSE_FILE);
    // A newly created inode was normalized above and is verified without the
    // marker gid exception; only EEXIST verification may inherit the context.
    if (!error) error = watchdog_verify_existing(p, dirfd, scratch, is_marker, 0, parent);
    return error;
}

static int
watchdog_warm_policy(proc_t p, uint64_t scratch, const char **stage)
{
    int32_t result = 0;
    int dirfd = -1, error, created_directory = 0;
    struct stat64 parent = {0};
    *stage = "warm directory";
    policy_diag_op = WATCHDOG_OP_COPYOUT_PATH;
    error = copyout(directory, scratch, sizeof(directory));
    if (error) goto out;
    struct mkdir_args md = {.path = scratch, .mode = 0755};
    policy_diag_op = WATCHDOG_OP_MKDIR;
    error = mkdir(p, &md, &result);
    const int mkdir_error = error;
    if (error && error != EEXIST) goto out;
    created_directory = error == 0;
    struct open_args op = {.path = scratch,
        .flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW_ANY, .mode = 0};
    policy_diag_op = WATCHDOG_OP_OPEN_DIRECTORY;
    error = open(p, &op, &result);
    if (error) goto out;
    dirfd = result;
    if ((error = watchdog_stat(p, dirfd, scratch, &parent))) goto out;
    policy_diag_op = WATCHDOG_OP_CHECK_DIRECTORY;
    printf("nova: policy-metadata probe=12 kind=directory mkdir_errno=%d created_directory=%d mode=0%o uid=%u gid=%u nlink=%u dev=%u ino=%llu\n",
        mkdir_error, created_directory, (unsigned)parent.st_mode,
        (unsigned)parent.st_uid, (unsigned)parent.st_gid, (unsigned)parent.st_nlink,
        (unsigned)parent.st_dev, (unsigned long long)parent.st_ino);
    // The old host 501:20 view is not a proven target runtime owner. R12
    // measured this already-existing warm Data directory as anonymous 99:99
    // (created_directory=0). Accept only that exact tuple, after the type and
    // permission checks, and never normalize or rewrite an existing inode.
    // The original root-owned and this-boot-created 0:20 forms stay unchanged.
    if (!S_ISDIR(parent.st_mode) || (parent.st_mode & 07777) != 0755 ||
        !((parent.st_uid == 501 && parent.st_gid == 20) || (parent.st_uid == 0 && parent.st_gid == 0) ||
          (created_directory && parent.st_uid == 0 && parent.st_gid == 20) ||
          (!created_directory && parent.st_uid == 99 && parent.st_gid == 99))) {
        error = EINVAL; goto out;
    }
    if (!created_directory && parent.st_uid == 99 && parent.st_gid == 99) {
        printf("nova: policy-owner-compat version=13 anonymous_existing=1 uid=99 gid=99\n");
    }
    // R14: the R13 runtime measured this exact pre-existing directory and a
    // marker owned 0:99. Scope the marker gid exception to that context only;
    // it is never derived from a directory created in this invocation.
    const int allow_marker_gid99 = !created_directory && parent.st_uid == 99 && parent.st_gid == 99;
    *stage = "warm disabled.plist";
    error = watchdog_warm_file(p, dirfd, scratch, "disabled.plist", 0, allow_marker_gid99, &parent);
    if (error) goto out;
    *stage = "warm disabled.migrated";
    error = watchdog_warm_file(p, dirfd, scratch, "disabled.migrated", 1, allow_marker_gid99, &parent);
out:
    if (dirfd >= 0) error = watchdog_close(p, dirfd, error, WATCHDOG_OP_CLOSE_DIRECTORY);
    return error;
}

// scratch belongs to PID 1: paths at 0, file bytes at 0x200..0x3ff, stat at
// 0x800. Owner-mount environment entries at 0x400, 0x500 and 0x600 stay intact.
int
meizu_boot_watchdog_policy(void *process, uint64_t scratch, int data_error)
{
    proc_t p = process;
    int enabled = 0, error = 0, fd = -1, warm = 0;
    int32_t result = 0;
    policy_diag_calls++;
    policy_diag_warm_found = -1;
    policy_diag_warm_value = 0;
    policy_diag_branch = -1;
    policy_diag_op = WATCHDOG_OP_NONE;
    policy_diag_stage = "entry";
    printf("nova: policy-diag version=11 event=entry call=%u\n", policy_diag_calls);
    policy_diag_op = WATCHDOG_OP_ENABLE_ARG;
    int enabled_found = PE_parse_boot_argn("meizu-disable-watchdogd", &enabled, sizeof(enabled));
    printf("nova: policy-diag version=11 event=enable-arg call=%u found=%d value=%d\n",
        policy_diag_calls, enabled_found, enabled);
    if (!enabled_found || enabled != 1) {
        policy_diag_branch = 0;
        policy_diag_stage = "disabled";
        watchdog_diag_report(0);
        return 0;
    }
    // native_owner_data_prepare verified the mounted APFS volume is the
    // expected writable RAM block, or returned an error. Require both that
    // successful return and the absence of a persistent store selection.
    policy_diag_stage = "preflight";
    policy_diag_op = WATCHDOG_OP_PREFLIGHT;
    if (data_error || native_ufs_store_mode() != 0 || native_data_persist_state != 0) {
        error = data_error ? data_error : EBUSY;
        printf("meizu watchdog policy: refused: Data mount=%d store_mode=%u persist_state=%u\n",
            data_error, native_ufs_store_mode(), native_data_persist_state);
        goto done;
    }
    policy_diag_op = WATCHDOG_OP_WARM_ARG;
    policy_diag_warm_found = PE_parse_boot_argn("nova-data-warm", &warm, sizeof(warm));
    policy_diag_warm_value = warm;
    policy_diag_branch = policy_diag_warm_found && warm == 1 ? 2 : 1;
    printf("nova: policy-diag version=11 event=warm-arg call=%u warm_found=%d warm_value=%d branch=%d\n",
        policy_diag_calls, policy_diag_warm_found, policy_diag_warm_value, policy_diag_branch);
    if (policy_diag_branch == 2) {
        error = watchdog_warm_policy(p, scratch, &policy_diag_stage);
        goto report;
    }
    policy_diag_stage = "mkdir";
    policy_diag_op = WATCHDOG_OP_COPYOUT_PATH;
    error = copyout(directory, scratch, sizeof(directory));
    if (error) goto done;
    struct mkdir_args md = {.path = scratch, .mode = 0755};
    policy_diag_op = WATCHDOG_OP_MKDIR;
    error = mkdir(p, &md, &result);
    if (error && error != EEXIST) goto done;
    policy_diag_stage = "open disabled.plist";
    policy_diag_op = WATCHDOG_OP_COPYOUT_PATH;
    error = copyout(path, scratch, sizeof(path));
    if (error) goto done;
    // O_EXCL avoids truncating an existing launchd policy. A reused RAM Data
    // seed with an existing policy is intentionally refused, not overwritten.
    struct open_args op = {.path = scratch, .flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, .mode = 0644};
    policy_diag_op = WATCHDOG_OP_OPEN_CREATE;
    error = open(p, &op, &result);
    if (error) goto done;
    fd = result;
    policy_diag_stage = "write disabled.plist";
    policy_diag_op = WATCHDOG_OP_COPYOUT_CONTENT;
    error = copyout(policy, scratch + 0x200, sizeof(policy) - 1);
    if (error) goto done;
    struct write_args wr = {.fd = fd, .cbuf = scratch + 0x200, .nbyte = sizeof(policy) - 1};
    user_ssize_t written = 0;
    policy_diag_op = WATCHDOG_OP_WRITE_CONTENT;
    error = write(p, &wr, &written);
    if (error) goto done;
    if (written != (user_ssize_t)(sizeof(policy) - 1)) { error = EIO; goto done; }
    policy_diag_stage = "verify disabled.plist";
    struct fchown_args owner = {.fd = fd, .uid = 0, .gid = 0};
    policy_diag_op = WATCHDOG_OP_FCHOWN;
    if ((error = fchown(p, &owner, &result))) goto done;
    struct fchmod_args mode = {.fd = fd, .mode = 0644};
    policy_diag_op = WATCHDOG_OP_FCHMOD;
    if ((error = fchmod(p, &mode, &result))) goto done;
    struct fsync_args sync = {.fd = fd};
    policy_diag_op = WATCHDOG_OP_FSYNC;
    if ((error = fsync(p, &sync, &result))) goto done;
    struct stat64 st = {0};
    if ((error = watchdog_stat(p, fd, scratch, &st))) goto done;
    policy_diag_op = WATCHDOG_OP_CHECK_METADATA;
    if (st.st_uid != 0 || st.st_gid != 0 || !S_ISREG(st.st_mode) ||
        (st.st_mode & 0777) != 0644 || st.st_size != (off_t)(sizeof(policy) - 1)) {
        watchdog_metadata_report(&st, 0);
        error = EINVAL;
    }
done:
    if (fd >= 0) error = watchdog_close(p, fd, error, WATCHDOG_OP_CLOSE_FILE);
    if (!error) {
        policy_diag_stage = "create disabled.migrated";
        policy_diag_op = WATCHDOG_OP_COPYOUT_PATH;
        error = copyout(marker, scratch, sizeof(marker));
        if (!error) {
            struct open_args mark = {.path = scratch, .flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, .mode = 0644};
            policy_diag_op = WATCHDOG_OP_OPEN_CREATE;
            error = open(p, &mark, &result);
            if (!error) error = watchdog_close(p, result, 0, WATCHDOG_OP_CLOSE_FILE);
        }
    }
report:
    meizu_watchdog_policy_status = error;
    watchdog_diag_report(error);
    if (error) printf("meizu watchdog policy: FAILED stage=%s errno=%d\n", policy_diag_stage, error);
    else if (warm == 1) printf("meizu watchdog policy: ready (volatile warm Data; verified policy entries preserved)\n");
    else printf("meizu watchdog policy: ready (volatile Data; com.apple.watchdogd only)\n");
    return error;
}
