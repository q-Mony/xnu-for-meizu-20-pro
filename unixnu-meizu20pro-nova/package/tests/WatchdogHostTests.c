// Exercises the actual policy, including its unchanged cold path and strict
// warm replay. Optional argv[1] supplies the independently extracted R8 file.
#define MEIZU_WATCHDOG_HOST_TEST 1
#include "WatchdogHostShim.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

static char diagnostic_output[4096];
static size_t diagnostic_used;
static int watchdog_test_printf(const char *format, ...) {
    va_list args;va_start(args,format);
    int length=vsnprintf(diagnostic_output+diagnostic_used,sizeof(diagnostic_output)-diagnostic_used,format,args);
    va_end(args);assert(length>=0&&(size_t)length<sizeof(diagnostic_output)-diagnostic_used);
    fputs(diagnostic_output+diagnostic_used,stdout);diagnostic_used+=(size_t)length;return length;
}
#define printf watchdog_test_printf
#include "../BootWatchdogPolicy.c"
#undef printf

volatile uint32_t native_data_persist_state = 0;
static unsigned store_mode;
static int bootarg, warmarg, calls, fail_at, fail_errno, total_cases;
static int fail_at_second, first_failed_op, operations[128];
static int writes, chowns, chmods, syncs, existing_mutations, read_calls, short_write, short_read, extra_eof;
static int extra_eof_target;
static int file_read_opens[2];
static int stat_calls[32], path_stats[2], alter_fd_stat, alter_fd_target, alter_path_stat, alter_path_target, corrupt_stat;
static int directory_present, created_dir_uid, created_dir_gid;
static char scratch[4096];
static const char captured_policy[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n<dict>\n"
    "\t<key>com.apple.watchdogd</key>\n\t<true/>\n"
    "\t<key>com.apple.dhcp6d</key>\n\t<true/>\n"
    "\t<key>com.apple.bootpd</key>\n\t<true/>\n"
    "\t<key>com.apple.ftp-proxy-embedded</key>\n\t<false/>\n"
    "</dict>\n</plist>\n";
struct Node { int present, preexisting; struct stat st; char data[1024]; };
static struct Node dir, files[2];
static int opened[32];
static size_t offset[32];
static int fault(void) {
    assert(++calls < (int)(sizeof(operations) / sizeof(operations[0])));
    operations[calls] = policy_diag_op;
    if (calls == fail_at || calls == fail_at_second) {
        if (first_failed_op < 0) first_failed_op = policy_diag_op;
        return calls == fail_at ? fail_errno : EACCES;
    }
    return 0;
}
// Match the real XNU vn_open_auth guard, not only each flag's numeric value.
static int xnu_open_flags_error(int flags) {
    return (flags & O_NOFOLLOW_ANY) && (flags & (O_SYMLINK | O_NOFOLLOW)) ? EINVAL : 0;
}
unsigned native_ufs_store_mode(void) { return store_mode; }
int PE_parse_boot_argn(const char *key, void *v, int n) {
    assert(n == (int)sizeof(int));
    int value = !strcmp(key, "meizu-disable-watchdogd") ? bootarg : warmarg;
    assert(!strcmp(key,"meizu-disable-watchdogd") || !strcmp(key,"nova-data-warm"));
    if (value < 0) return 0;
    *(int *)v = value; return 1;
}
int copyout(const void *src, uintptr_t addr, size_t size) {
    int error = fault(); if (error) return error;
    assert(addr >= (uintptr_t)scratch && addr + size <= (uintptr_t)scratch + sizeof(scratch));
    memcpy((void *)addr, src, size); return 0;
}
int copyin(uintptr_t addr, void *dst, size_t size) {
    int error = fault(); if (error) return error;
    assert(addr >= (uintptr_t)scratch && addr + size <= (uintptr_t)scratch + sizeof(scratch));
    memcpy(dst, (void *)addr, size); return 0;
}
int mkdir(proc_t p, struct mkdir_args *a, int32_t *r) {
    (void)p; (void)r; int error = fault(); if (error) return error;
    assert(!strcmp((char *)a->path, directory) && a->mode == 0755);
    if (directory_present) return EEXIST;
    directory_present = 1; dir.st.st_mode = S_IFDIR | 0755; dir.st.st_uid = created_dir_uid; dir.st.st_gid = created_dir_gid;
    return 0;
}
static int index_for(const char *name) {
    if (!strcmp(name,"disabled.plist") || !strcmp(name,path)) return 0;
    assert(!strcmp(name,"disabled.migrated") || !strcmp(name,marker)); return 1;
}
static int open_file(int index, int flags, int mode, int32_t *result) {
    struct Node *n = &files[index]; int fd = 10 + index;
    if (!S_ISDIR(dir.st.st_mode)) return ENOTDIR;
    assert((flags & O_NOFOLLOW_ANY) ? !(flags & O_NOFOLLOW) : (flags & O_NOFOLLOW));
    if (flags & O_CREAT) {
        assert((flags & (O_WRONLY | O_EXCL)) == (O_WRONLY | O_EXCL) && mode == 0644);
        if (n->present) return EEXIST;
        n->present=1; n->preexisting=0; n->st.st_uid=0; n->st.st_gid=dir.st.st_gid;
        n->st.st_mode=S_IFREG | 0644; n->st.st_nlink=1; n->st.st_size=0;
    } else {
        assert((flags & O_ACCMODE) == O_RDONLY && (flags & O_NONBLOCK) && (flags & O_NOFOLLOW_ANY));
        if (!n->present) return ENOENT;
        if (S_ISLNK(n->st.st_mode)) return ELOOP;
        assert(S_ISREG(n->st.st_mode)); // pre-stat must reject devices/FIFOs/directories first.
        file_read_opens[index]++;
    }
    assert(!opened[fd]); opened[fd]=1; offset[fd]=0; *result=fd; return 0;
}
int open(proc_t p, struct open_args *a, int32_t *r) {
    (void)p; int error=fault(); if(error)return error;
    if ((error=xnu_open_flags_error(a->flags))) return error;
    if(!strcmp((char *)a->path,directory)) {
        assert((a->flags & (O_DIRECTORY | O_NOFOLLOW_ANY | O_NOFOLLOW)) == (O_DIRECTORY | O_NOFOLLOW_ANY));
        if(!directory_present)return ENOENT;
        if(S_ISLNK(dir.st.st_mode))return ELOOP;
        if(!S_ISDIR(dir.st.st_mode))return ENOTDIR;
        assert(!opened[20]);opened[20]=1;*r=20;return 0;
    }
    return open_file(index_for((char *)a->path),a->flags,a->mode,r);
}
int openat(proc_t p, struct openat_args *a, int32_t *r) {
    (void)p;int error=fault();if(error)return error;
    if ((error=xnu_open_flags_error(a->flags))) return error;
    assert(a->fd==20 && opened[20] && !strchr((char *)a->path,'/') && (a->flags&O_NOFOLLOW_ANY));
    return open_file(index_for((char *)a->path),a->flags,a->mode,r);
}
static struct Node *node_for_fd(int fd){assert(fd==10||fd==11||fd==20);assert(opened[fd]);return fd==20?&dir:&files[fd-10];}
int write(proc_t p, struct write_args *a, user_ssize_t *n) {
    (void)p;int error=fault();if(error)return error;struct Node *node=node_for_fd(a->fd);
    writes++;existing_mutations+=node->preexisting;assert(a->nbyte<sizeof(node->data));
    memcpy(node->data,(void *)a->cbuf,a->nbyte);node->st.st_size=a->nbyte;*n=short_write?(user_ssize_t)a->nbyte-1:(user_ssize_t)a->nbyte;return 0;
}
int read(proc_t p, struct read_args *a, user_ssize_t *n) {
    (void)p;int error=fault();if(error)return error;struct Node *node=node_for_fd(a->fd);read_calls++;
    size_t left=(size_t)node->st.st_size-offset[a->fd], count=left<a->nbyte?left:a->nbyte;
    if(short_read&&count)count--;if(extra_eof&&!left&&a->fd==10+extra_eof_target){*(char *)a->cbuf='x';*n=1;return 0;}
    memcpy((void *)a->cbuf,node->data+offset[a->fd],count);offset[a->fd]+=count;*n=(user_ssize_t)count;return 0;
}
int fchown(proc_t p, struct fchown_args *a, int32_t *r) {
    (void)p;(void)r;int error=fault();if(error)return error;struct Node *n=node_for_fd(a->fd);chowns++;existing_mutations+=n->preexisting;
    assert(a->uid==0&&a->gid==0);n->st.st_uid=a->uid;n->st.st_gid=a->gid;return 0;
}
int fchmod(proc_t p, struct fchmod_args *a, int32_t *r) {
    (void)p;(void)r;int error=fault();if(error)return error;struct Node *n=node_for_fd(a->fd);chmods++;existing_mutations+=n->preexisting;
    assert(a->mode==0644);n->st.st_mode=S_IFREG|a->mode;return 0;
}
int fsync(proc_t p, struct fsync_args *a, int32_t *r) {
    (void)p;(void)r;int error=fault();if(error)return error;(void)node_for_fd(a->fd);syncs++;return 0;
}
int sys_fstat64(proc_t p, struct fstat64_args *a, int32_t *r) {
    (void)p;(void)r;int error=fault();if(error)return error;struct stat st=node_for_fd(a->fd)->st;
    stat_calls[a->fd]++;if(corrupt_stat&&a->fd==10)st.st_size++;
    if(alter_fd_stat&&a->fd==10+alter_fd_target&&stat_calls[a->fd]==alter_fd_stat)st.st_ino++;
    memcpy((void *)a->ub,&st,sizeof(st));return 0;
}
int fstatat64(proc_t p, struct fstatat64_args *a, int32_t *r) {
    (void)p;(void)r;int error=fault();if(error)return error;assert(a->fd==20&&opened[20]&&a->flag==AT_SYMLINK_NOFOLLOW_ANY);
    int index=index_for((char *)a->path);struct Node *node=&files[index];if(!node->present)return ENOENT;
    if(S_ISLNK(node->st.st_mode))return ELOOP;
    struct stat st=node->st;path_stats[index]++;if(alter_path_stat&&index==alter_path_target&&path_stats[index]==alter_path_stat)st.st_ino++;
    memcpy((void *)a->ub,&st,sizeof(st));return 0;
}
int sys_close(proc_t p, struct close_args *a, int32_t *r) {
    (void)p;(void)r;(void)node_for_fd(a->fd);opened[a->fd]=0;return fault();
}
static void reset(void) {
    diagnostic_used=0;diagnostic_output[0]='\0';
    memset(scratch,0xa5,sizeof(scratch));memset(files,0,sizeof(files));memset(&dir,0,sizeof(dir));memset(opened,0,sizeof(opened));
    memset(offset,0,sizeof(offset));memset(stat_calls,0,sizeof(stat_calls));memset(path_stats,0,sizeof(path_stats));
    bootarg=1;warmarg=-1;store_mode=0;native_data_persist_state=0;calls=fail_at=0;fail_errno=EIO;
    fail_at_second=0;first_failed_op=-1;memset(operations,0,sizeof(operations));
    writes=chowns=chmods=syncs=existing_mutations=read_calls=short_write=short_read=extra_eof=0;extra_eof_target=0;
    file_read_opens[0]=file_read_opens[1]=0;
    alter_fd_stat=alter_path_stat=corrupt_stat=0;alter_fd_target=alter_path_target=0;directory_present=0;created_dir_uid=created_dir_gid=0;
    dir.st.st_dev=123;dir.st.st_ino=100;dir.st.st_nlink=2;
    for(int i=0;i<2;i++){files[i].st.st_dev=123;files[i].st.st_ino=101+i;}
    meizu_watchdog_policy_status=-1;
}
static void warm_fixture(void){
    reset();warmarg=1;directory_present=1;dir.st.st_mode=S_IFDIR|0755;dir.st.st_uid=501;dir.st.st_gid=20;dir.st.st_nlink=5;
    for(int i=0;i<2;i++){files[i].present=files[i].preexisting=1;files[i].st.st_mode=S_IFREG|0644;files[i].st.st_nlink=1;}
    memcpy(files[0].data,captured_policy,sizeof(captured_policy)-1);files[0].st.st_size=sizeof(captured_policy)-1;files[1].st.st_gid=20;
}
static void anonymous_fixture(void){warm_fixture();dir.st.st_uid=99;dir.st.st_gid=99;}
// R14 replay of the R13 runtime marker. The parent 040755 99:99 dev/ino and
// mkdir EEXIST are observed. The marker's dev/ino were not logged, so dev is
// set equal to the observed parent only to satisfy device equality and ino is
// an explicitly synthetic value.
static void observed_marker_fixture(void){
    warm_fixture();dir.st.st_uid=99;dir.st.st_gid=99;dir.st.st_dev=16777221;dir.st.st_ino=100;
    files[0].st.st_dev=16777221;files[0].st.st_ino=101;
    files[1].st.st_dev=16777221;files[1].st.st_ino=102;files[1].st.st_gid=99;
}
static int compat_count(void){
    const char *text=diagnostic_output;
    const char *line="nova: policy-owner-compat version=13 anonymous_existing=1 uid=99 gid=99\n";
    int count=0;
    while((text=strstr(text,line))){count++;text+=strlen(line);}
    return count;
}
static int marker_compat_count(void){
    const char *text=diagnostic_output;
    const char *line="nova: policy-marker-compat version=14 active=1 kind=disabled.migrated uid=0 gid=99\n";
    int count=0;
    while((text=strstr(text,line))){count++;text+=strlen(line);}
    return count;
}
static int run(int data_error){int result=meizu_boot_watchdog_policy(NULL,(uintptr_t)scratch,data_error);for(int i=0;i<32;i++)assert(!opened[i]);
    int found,value,branch,op;
    const char *stage=meizu_watchdog_policy_diagnostic(&found,&value,&branch,&op);
    assert(stage && stage==meizu_watchdog_policy_diagnostic(NULL,NULL,NULL,NULL));
    if(first_failed_op>=0 && result && fail_errno!=EEXIST) assert(op==first_failed_op);
    assert(scratch[0x400]==(char)0xa5&&scratch[0x500]==(char)0xa5&&scratch[0x600]==(char)0xa5);assert(!existing_mutations);total_cases++;return result;}
// R14 isolated replay: one actual policy invocation over the R13-observed
// directory and marker tuple. It expects success; the pre-fix policy fails at
// CHECK_METADATA/EINVAL for the marker gid99 and this mode returns nonzero.
static int r14_repro(void){
    observed_marker_fixture();
    int result=meizu_boot_watchdog_policy(NULL,(uintptr_t)scratch,0);
    int found,value,branch,op;
    const char *stage=meizu_watchdog_policy_diagnostic(&found,&value,&branch,&op);
    int open_fds=0;for(int i=0;i<32;i++)open_fds+=opened[i];
    printf("R14-REPRO: fixture parent=040755 uid=99 gid=99 mkdir=EEXIST created=0 dev=16777221 ino=100; marker=0100644 uid=0 gid=99 size=0 nlink=1 file_dev/ino synthetic\n");
    printf("R14-REPRO: result=%d expected=0 branch=%d op=%d stage=%s open_fds=%d existing_mutations=%d writes=%d\n",
        result,branch,op,stage,open_fds,existing_mutations,writes);
    if(result){printf("R14-REPRO: RED errno=%d branch=%d op=%d stage=%s\n",result,branch,op,stage);return 1;}
    printf("R14-REPRO: GREEN errno=0 branch=%d op=%d stage=%s\n",branch,op,stage);
    return 0;
}
int main(int argc,char **argv){
    assert(sizeof(captured_policy)==357 && !memcmp(captured_policy,warm_policy,sizeof(captured_policy)));
    if(argc==2&&!strcmp(argv[1],"--r14-repro"))return r14_repro();
    if(argc==2){FILE *f=fopen(argv[1],"rb");assert(f);char bytes[512];size_t n=fread(bytes,1,sizeof(bytes),f);assert(n==356&&!memcmp(bytes,captured_policy,n));assert(!ferror(f)&&feof(f));fclose(f);}
    // R10's invalid flags must fail in the model exactly as in real XNU.
    reset();int32_t ignored=0;
    struct open_args invalid_directory={.path=(uintptr_t)directory,.flags=O_RDONLY|O_DIRECTORY|O_NOFOLLOW_ANY|O_NOFOLLOW};
    assert(open(NULL,&invalid_directory,&ignored)==EINVAL);
    struct openat_args invalid_create={.fd=20,.path=(uintptr_t)"disabled.plist",.flags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW_ANY|O_NOFOLLOW,.mode=0644};
    assert(openat(NULL,&invalid_create,&ignored)==EINVAL);
    invalid_create.flags=O_RDONLY|O_NOFOLLOW_ANY|O_NOFOLLOW|O_NONBLOCK;
    assert(openat(NULL,&invalid_create,&ignored)==EINVAL);
    assert(xnu_open_flags_error(O_NOFOLLOW_ANY|O_SYMLINK)==EINVAL);
    // Preserve old opt-in, Data ownership, write verification and failure coverage.
    for(int arg=-1;arg<=2;arg++)if(arg!=1){reset();bootarg=arg;assert(!run(0)&&!calls);}
    reset();assert(run(EIO)==EIO&&!calls);reset();store_mode=2;assert(run(0)==EBUSY&&!calls);
    reset();native_data_persist_state=3;assert(run(0)==EBUSY&&!calls);
    reset();assert(!run(0)&&writes==1&&files[0].st.st_size==(off_t)sizeof(policy)-1&&!memcmp(files[0].data,policy,sizeof(policy)-1));
    int cold_calls=calls;for(int i=1;i<=cold_calls;i++){reset();fail_at=i;assert(run(0)!=0);}
    reset();short_write=1;assert(run(0)==EIO);reset();corrupt_stat=1;assert(run(0)==EINVAL);
    for(int arg=-1;arg<=2;arg++)if(arg!=1){warm_fixture();warmarg=arg;assert(run(0)==EEXIST&&!writes);}
    // The real R8 bytes and metadata, including gid20 marker and uid501 directory.
    warm_fixture();assert(!run(0)&&!writes&&!chowns&&!chmods&&!syncs&&!compat_count());int existing_calls=calls;
    int read_failure_call=0;for(int i=1;i<=existing_calls;i++)if(operations[i]==WATCHDOG_OP_READ_CONTENT){read_failure_call=i;break;}
    assert(read_failure_call>0);
    assert(!memcmp(files[0].data,captured_policy,356)&&files[1].st.st_gid==20);
    warm_fixture();memcpy(files[0].data,policy,sizeof(policy)-1);files[0].st.st_size=sizeof(policy)-1;assert(!run(0));
    warm_fixture();dir.st.st_uid=0;dir.st.st_gid=0;files[1].st.st_gid=0;assert(!run(0));
    for(int group=0;group<=20;group+=20){reset();warmarg=1;created_dir_gid=group;assert(!run(0)&&writes==1&&files[1].st.st_gid==0);}
    reset();warmarg=1;created_dir_gid=21;assert(run(0)==EINVAL&&!writes);
    warm_fixture();files[1].present=files[1].preexisting=0;assert(!run(0)&&!writes&&files[1].st.st_gid==0);
    warm_fixture();files[0].present=files[0].preexisting=0;assert(!run(0)&&writes==1&&files[1].st.st_gid==20);
    // Every byte of each allowed existing policy is authenticated exactly.
    for(size_t i=0;i<356;i++){warm_fixture();files[0].data[i]^=1;assert(run(0)==EINVAL&&!writes);}
    for(size_t i=0;i<sizeof(policy)-1;i++){warm_fixture();memcpy(files[0].data,policy,sizeof(policy)-1);files[0].st.st_size=sizeof(policy)-1;files[0].data[i]^=1;assert(run(0)==EINVAL);}
    for(int i=1;i<=existing_calls;i++){warm_fixture();fail_at=i;assert(run(0)!=0&&!writes&&!chowns&&!chmods);}
    reset();warmarg=1;assert(!run(0));int fresh_calls=calls;
    for(int i=1;i<=fresh_calls;i++){reset();warmarg=1;fail_at=i;assert(run(0)!=0);}
    // Existing policy/marker object types, ownership, links, mode and sizes.
    for(int index=0;index<2;index++)for(int variant=0;variant<15;variant++){
        warm_fixture();struct stat *st=&files[index].st;
        switch(variant){case 0:st->st_uid=501;break;case 1:st->st_gid=21;break;case 2:st->st_mode|=0002;break;
        case 3:st->st_mode|=0020;break;case 4:st->st_mode|=0100;break;case 5:st->st_mode|=04000;break;
        case 6:st->st_nlink=2;break;case 7:st->st_nlink=0;break;case 8:st->st_mode=S_IFDIR|0644;break;
        case 9:st->st_mode=S_IFIFO|0644;break;case 10:st->st_mode=S_IFCHR|0644;break;case 11:st->st_mode=S_IFLNK|0644;break;
        case 12:st->st_dev++;break;case 13:st->st_size++;break;case 14:st->st_mode=S_IFREG|0600;break;}
        assert(run(0)!=0&&!writes);
    }
    for(int variant=0;variant<8;variant++){warm_fixture();switch(variant){case 0:dir.st.st_uid=502;break;case 1:dir.st.st_gid=21;break;
        case 2:dir.st.st_mode|=0020;break;case 3:dir.st.st_mode|=0002;break;case 4:dir.st.st_mode|=04000;break;
        case 5:dir.st.st_mode=S_IFREG|0755;break;case 6:dir.st.st_mode=S_IFLNK|0755;break;case 7:dir.st.st_uid=0;break;}
        assert(run(0)!=0&&!writes);
    }
    warm_fixture();short_read=1;assert(run(0)==EIO);warm_fixture();extra_eof=1;assert(run(0)==EINVAL);
    warm_fixture();alter_fd_stat=1;assert(run(0)==EINVAL);warm_fixture();alter_fd_stat=2;assert(run(0)==EINVAL);
    warm_fixture();alter_path_stat=2;assert(run(0)==EINVAL);
    // Other errors are not mistaken for a reuse request, including open/create.
    for(int err=EACCES;err<=EFAULT;err++){warm_fixture();fail_at=1;fail_errno=err;assert(run(0)==err);}
    // R13: only an already-existing 99:99 directory passes the new branch,
    // and only after type/mode checks. Both files still receive the full exact
    // content and identity verification; no existing inode is normalized.
    anonymous_fixture();assert(!run(0)&&!writes&&!chowns&&!chmods&&!syncs);
    assert(compat_count()==1&&!memcmp(files[0].data,captured_policy,356)&&files[1].st.st_gid==20);
    anonymous_fixture();files[0].data[0]^=1;assert(run(0)==EINVAL&&!writes&&!existing_mutations);
    assert(compat_count()==1);
    anonymous_fixture();files[1].st.st_size=1;assert(run(0)==EINVAL&&!writes&&!existing_mutations&&compat_count()==1);
    anonymous_fixture();files[0].st.st_nlink=2;assert(run(0)==EINVAL&&!writes&&compat_count()==1);
    int mixed_owner[4][2]={{99,0},{0,99},{98,99},{99,98}};
    for(int i=0;i<4;i++){anonymous_fixture();dir.st.st_uid=mixed_owner[i][0];dir.st.st_gid=mixed_owner[i][1];
        assert(run(0)==EINVAL&&!writes&&!compat_count());}
    // A directory created in this invocation is never accepted as 99:99.
    reset();warmarg=1;created_dir_uid=99;created_dir_gid=99;assert(run(0)==EINVAL&&!writes&&!compat_count());
    // Existing 99:99 still requires exactly 0755 without setuid/setgid/sticky.
    int bad_modes[5]={S_IFDIR|0777,S_IFDIR|0700,S_IFDIR|04755,S_IFDIR|02755,S_IFDIR|01755};
    for(int i=0;i<5;i++){anonymous_fixture();dir.st.st_mode=bad_modes[i];assert(run(0)==EINVAL&&!writes&&!compat_count());}
    // Cold branch never evaluates the warm directory ownership.
    anonymous_fixture();warmarg=-1;assert(run(0)==EEXIST&&!writes&&!compat_count());
    // The compatibility marker is emitted only on the measured tuple.
    warm_fixture();assert(!run(0)&&!writes&&!compat_count());
    anonymous_fixture();assert(!run(0));int compat_calls=calls;
    for(int i=1;i<=compat_calls;i++){anonymous_fixture();fail_at=i;assert(run(0)!=0&&!writes&&!chowns&&!chmods);}
    // Exported diagnostics identify branch, failure primitive and post-return
    // stage; cleanup failures must not erase the earlier read failure.
    int found,value,branch,op;const char *stage;
    reset();bootarg=0;assert(!run(0));stage=meizu_watchdog_policy_diagnostic(&found,&value,&branch,&op);
    assert(found==-1&&value==0&&branch==0&&op==WATCHDOG_OP_ENABLE_ARG&&!strcmp(stage,"disabled"));
    warm_fixture();warmarg=-1;assert(run(0)==EEXIST);stage=meizu_watchdog_policy_diagnostic(&found,&value,&branch,&op);
    assert(found==0&&value==0&&branch==1&&op==WATCHDOG_OP_OPEN_CREATE&&!strcmp(stage,"open disabled.plist"));
    warm_fixture();assert(!run(0));stage=meizu_watchdog_policy_diagnostic(&found,&value,&branch,&op);
    assert(found==1&&value==1&&branch==2&&op==WATCHDOG_OP_CLOSE_DIRECTORY&&!strcmp(stage,"warm disabled.migrated"));
    warm_fixture();fail_at=read_failure_call;fail_at_second=read_failure_call+1;assert(run(0)==EIO);
    stage=meizu_watchdog_policy_diagnostic(&found,&value,&branch,&op);
    assert(found==1&&value==1&&branch==2&&op==WATCHDOG_OP_READ_CONTENT&&!strcmp(stage,"warm disabled.plist"));
    assert(strstr(diagnostic_output,"branch=2 op=15 stage=warm disabled.plist errno=5\n"));
    reset();assert(run(EIO)==EIO);stage=meizu_watchdog_policy_diagnostic(&found,&value,&branch,&op);
    assert(found==-1&&value==0&&branch==-1&&op==WATCHDOG_OP_PREFLIGHT&&!strcmp(stage,"preflight"));
    // R12 observes the exact metadata without changing accepted tuples or errno.
    char expected_directory[256];
    snprintf(expected_directory,sizeof(expected_directory),
        "nova: policy-metadata probe=12 kind=directory mkdir_errno=%d created_directory=0 mode=040755 uid=501 gid=20 nlink=5 dev=123 ino=100\n",EEXIST);
    warm_fixture();assert(!run(0));char *directory_line=strstr(diagnostic_output,expected_directory);
    assert(directory_line&&!strstr(directory_line+strlen(expected_directory),"kind=directory")&&!strstr(diagnostic_output,"kind=disabled."));
    reset();warmarg=1;created_dir_gid=20;assert(!run(0));
    assert(strstr(diagnostic_output,"probe=12 kind=directory mkdir_errno=0 created_directory=1 mode=040755 uid=0 gid=20 nlink=2 dev=123 ino=100\n"));
    warm_fixture();dir.st.st_uid=0;assert(run(0)==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_DIRECTORY);
    assert(strstr(diagnostic_output,"created_directory=0 mode=040755 uid=0 gid=20 nlink=5 dev=123 ino=100\n"));
    assert(strstr(diagnostic_output,"branch=2 op=9 stage=warm directory errno=22\n"));
    warm_fixture();files[0].st.st_uid=501;assert(run(0)==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_METADATA);
    assert(strstr(diagnostic_output,"probe=12 kind=disabled.plist mode=0100644 uid=501 gid=0 size=356 nlink=1\n"));
    warm_fixture();files[1].st.st_nlink=2;assert(run(0)==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_METADATA);
    assert(strstr(diagnostic_output,"probe=12 kind=disabled.migrated mode=0100644 uid=0 gid=20 size=0 nlink=2\n"));
    // R14: only the observed existing 99:99 directory plus an existing regular
    // marker owned 0:99 may use the marker gid compatibility. Both files still
    // pass exact metadata, device, identity, content and EOF checks with no
    // writes or ownership normalization.
    observed_marker_fixture();
    assert(!run(0)&&!writes&&!chowns&&!chmods&&!syncs&&!existing_mutations);
    assert(marker_compat_count()==1&&compat_count()==1);
    assert(files[1].st.st_gid==99&&files[1].st.st_size==0&&files[1].st.st_nlink==1);
    assert(file_read_opens[0]==1&&file_read_opens[1]==1);
    assert(!memcmp(files[0].data,captured_policy,356));
    assert(strstr(diagnostic_output,"nova: policy-marker-compat version=14 active=1 kind=disabled.migrated uid=0 gid=99\n"));
    // Two consecutive actual invocations must reset their per-call diagnostics.
    observed_marker_fixture();
    assert(!run(0)&&!run(0)&&marker_compat_count()==2&&!writes&&!existing_mutations);
    // Existing gid0/gid20 markers remain accepted with no compatibility line.
    anonymous_fixture();files[1].st.st_gid=0;assert(!run(0)&&!marker_compat_count()&&files[1].st.st_gid==0);
    anonymous_fixture();files[1].st.st_gid=20;assert(!run(0)&&!marker_compat_count()&&files[1].st.st_gid==20);
    // gid99 is refused for every other accepted directory context.
    observed_marker_fixture();dir.st.st_uid=501;dir.st.st_gid=20;
    assert(run(0)==EINVAL&&!writes&&!marker_compat_count()&&policy_diag_op==WATCHDOG_OP_CHECK_METADATA);
    observed_marker_fixture();dir.st.st_uid=0;dir.st.st_gid=0;
    assert(run(0)==EINVAL&&!writes&&!marker_compat_count()&&policy_diag_op==WATCHDOG_OP_CHECK_METADATA);
    reset();warmarg=1;directory_present=0;created_dir_uid=0;created_dir_gid=20;
    for(int i=0;i<2;i++){files[i].present=files[i].preexisting=1;files[i].st.st_mode=S_IFREG|0644;files[i].st.st_nlink=1;}
    memcpy(files[0].data,captured_policy,sizeof(captured_policy)-1);files[0].st.st_size=sizeof(captured_policy)-1;files[1].st.st_gid=99;
    assert(run(0)==EINVAL&&!writes&&!marker_compat_count()&&policy_diag_op==WATCHDOG_OP_CHECK_METADATA);
    for(int i=0;i<4;i++){observed_marker_fixture();dir.st.st_uid=mixed_owner[i][0];dir.st.st_gid=mixed_owner[i][1];
        assert(run(0)==EINVAL&&!writes&&!marker_compat_count()&&policy_diag_op==WATCHDOG_OP_CHECK_DIRECTORY);}
    observed_marker_fixture();warmarg=-1;assert(run(0)==EEXIST&&!writes&&!marker_compat_count());
    // The plist never receives the marker gid exception.
    observed_marker_fixture();files[0].st.st_gid=99;
    assert(run(0)==EINVAL&&!writes&&!marker_compat_count()&&policy_diag_op==WATCHDOG_OP_CHECK_METADATA&&file_read_opens[0]==0);
    // Marker object type, ownership, mode, link, size and device failures all
    // stop before the marker read open.
    for(int variant=0;variant<13;variant++){
        observed_marker_fixture();struct stat *st=&files[1].st;int expected=EINVAL,expected_op=WATCHDOG_OP_CHECK_METADATA;
        switch(variant){case 0:st->st_uid=99;break;case 1:st->st_size=1;break;
        case 2:st->st_mode=S_IFREG|0600;break;case 3:st->st_mode=S_IFREG|0664;break;
        case 4:st->st_mode=S_IFREG|01644;break;case 5:st->st_mode=S_IFREG|04644;break;
        case 6:st->st_mode=S_IFREG|02644;break;case 7:st->st_mode=S_IFLNK|0644;expected=ELOOP;expected_op=WATCHDOG_OP_FSTATAT;break;
        case 8:st->st_mode=S_IFIFO|0644;break;case 9:st->st_mode=S_IFCHR|0644;break;
        case 10:st->st_mode=S_IFDIR|0644;break;case 11:st->st_nlink=0;break;case 12:st->st_nlink=2;break;}
        assert(run(0)==expected&&policy_diag_op==expected_op&&!writes&&!existing_mutations&&!marker_compat_count()&&file_read_opens[1]==0);
    }
    // Once gid passes, an unseen device mismatch is both rejected and reported.
    observed_marker_fixture();files[1].st.st_dev++;int dev_result=run(0);
    assert(dev_result==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_METADATA&&!marker_compat_count()&&file_read_opens[1]==0);
    assert(strstr(diagnostic_output,"nova: policy-metadata-reject version=14 kind=disabled.migrated allow_marker_gid99=1 file_dev=16777222 parent_dev=16777221 file_ino=102 parent_ino=100\n"));
    // Opened-fd/path metadata races on the marker itself reach identity checks.
    observed_marker_fixture();alter_fd_target=1;alter_fd_stat=1;
    assert(run(0)==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_IDENTITY&&!marker_compat_count()&&file_read_opens[1]==1);
    observed_marker_fixture();alter_fd_target=1;alter_fd_stat=2;
    assert(run(0)==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_IDENTITY&&!marker_compat_count());
    observed_marker_fixture();alter_path_target=1;alter_path_stat=2;
    assert(run(0)==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_IDENTITY&&!marker_compat_count());
    // The empty marker still rejects a byte after EOF.
    observed_marker_fixture();extra_eof=1;extra_eof_target=1;
    assert(run(0)==EINVAL&&policy_diag_op==WATCHDOG_OP_CHECK_EOF&&!marker_compat_count()&&file_read_opens[1]==1);
    // Every marker syscall fault preserves its errno/op, closes descriptors and
    // never writes an existing inode; a targeted marker EOF fault is exact.
    observed_marker_fixture();assert(!run(0));int observed_calls=calls;
    int marker_eof_call=0,eof_seen=0;
    for(int i=1;i<=observed_calls;i++)if(operations[i]==WATCHDOG_OP_READ_EOF&&++eof_seen==2){marker_eof_call=i;break;}
    assert(marker_eof_call>0);
    for(int i=1;i<=observed_calls;i++){observed_marker_fixture();fail_at=i;fail_errno=EIO;
        // The final directory close follows a successful marker verification,
        // so the compatibility notice is already legitimately emitted by then.
        assert(run(0)==EIO&&policy_diag_op==first_failed_op&&!writes&&!chowns&&!chmods&&!syncs&&!existing_mutations&&marker_compat_count()==(i==observed_calls));}
    observed_marker_fixture();fail_at=marker_eof_call;
    assert(run(0)==EIO&&policy_diag_op==WATCHDOG_OP_READ_EOF&&!marker_compat_count()&&!writes);
    // A newly created 0:20 directory still normalizes fresh files to 0:0 and
    // never enables the marker gid exception.
    reset();warmarg=1;created_dir_gid=20;assert(!run(0)&&writes==1&&files[1].st.st_gid==0&&!marker_compat_count());
    printf("PASS: %d actual-function cases; cold_calls=%d warm_existing_calls=%d warm_fresh_calls=%d; full R8 fixture preserved; all descriptors closed; existing inodes unmodified\n",total_cases,cold_calls,existing_calls,fresh_calls);
    return 0;
}
