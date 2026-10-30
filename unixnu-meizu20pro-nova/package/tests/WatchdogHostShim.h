#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#define mkdir test_mkdir
#define open test_open
#define write test_write
#define read test_read
#define openat test_openat
#define fstatat64 test_fstatat64
#define fchown test_fchown
#define fchmod test_fchmod
#define fsync test_fsync

typedef void *proc_t;
struct mkdir_args { uintptr_t path; int mode; };
struct open_args { uintptr_t path; int flags; int mode; };
struct write_args { int fd; uintptr_t cbuf; size_t nbyte; };
struct fchown_args { int fd; int uid; int gid; };
struct fchmod_args { int fd; int mode; };
struct fsync_args { int fd; };
struct fstat64_args { int fd; uintptr_t ub; };
struct close_args { int fd; };
#define stat64 stat
int PE_parse_boot_argn(const char *, void *, int);
int copyout(const void *, uintptr_t, size_t);
int copyin(uintptr_t, void *, size_t);
int mkdir(proc_t, struct mkdir_args *, int32_t *);
int open(proc_t, struct open_args *, int32_t *);
int write(proc_t, struct write_args *, user_ssize_t *);
int fchown(proc_t, struct fchown_args *, int32_t *);
int fchmod(proc_t, struct fchmod_args *, int32_t *);
int fsync(proc_t, struct fsync_args *, int32_t *);
int sys_fstat64(proc_t, struct fstat64_args *, int32_t *);
int sys_close(proc_t, struct close_args *, int32_t *);

struct read_args { int fd; uintptr_t cbuf; size_t nbyte; };
struct openat_args { int fd; uintptr_t path; int flags; int mode; };
struct fstatat64_args { int fd; uintptr_t path; uintptr_t ub; int flag; };
int read(proc_t, struct read_args *, user_ssize_t *);
int openat(proc_t, struct openat_args *, int32_t *);
int fstatat64(proc_t, struct fstatat64_args *, int32_t *);
const char *meizu_watchdog_policy_diagnostic(int *, int *, int *, int *);
