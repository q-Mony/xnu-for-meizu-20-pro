/*
 * R18 host harness for the production native SHA-256 streaming adapter.
 *
 * This is a new file for the round-18 task; there is no prior version.
 * It compiles the real production corecrypto_sha2.c adapter (sliced from the
 * unchanged production marker) against the real scalar corecrypto sources and
 * the real ARM64 SHA-256 assembly.  Only the privileged ID_AA64ISAR0_EL1 read
 * and the preemption primitives are replaced by instrumented host stubs.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <corecrypto/ccsha2.h>
#include <corecrypto/ccdigest.h>

/* Match libkern's fixed-size SHA256 context; the production adapter consumes
 * this exact layout through ccdigest_ctx_t. */
typedef struct { ccdigest_ctx_decl(CCSHA256_STATE_SIZE, CCSHA256_BLOCK_SIZE, ctx); } SHA256_CTX;

/* The production header is checked separately by the header probe and by the
 * WarmLoad fixture.  This harness deliberately declares the interface itself
 * so the RED run can link the old adapter and report the missing symbols
 * instead of failing only on a header diagnostic. */
extern void native_sha256_stream_init(SHA256_CTX *ctx);
extern void native_sha256_stream_update(SHA256_CTX *ctx, const void *data, size_t len);
extern void native_sha256_stream_final(void *digest, SHA256_CTX *ctx);
extern void native_sha256_digest(const void *data, size_t len, void *digest);

static unsigned fallback_calls[3];
void cc_clear(size_t len, void *dst) { volatile unsigned char *p = dst; while (len--) *p++ = 0; }
#define panic(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)

struct trace { unsigned feature; size_t blocks; const void *data; int hardware; };
static struct trace trace[262144];
static size_t trace_count, feature_reads, disable_calls, enable_calls;
static unsigned features[16] = {1};
static size_t feature_count = 1;
static int depth, original_depth;
extern void native_sha256_compress_arm64(ccdigest_state_t, size_t, const void *);
extern void preserve_vectors_nova(ccdigest_state_t, size_t, const void *, const void *, void *);

static void disable_preemption(void) { assert(depth == original_depth); depth++; disable_calls++; }
static void enable_preemption(void) { assert(depth == original_depth + 1); depth--; enable_calls++; }
static uint64_t test_read_isar0(void) {
    assert(depth == original_depth + 1);
    unsigned f = features[feature_reads++ % feature_count];
    return UINT64_C(0xabc00000) | ((uint64_t)f << 12) | UINT64_C(0x7a5);
}
static void record(size_t nblocks, const void *data, int hardware) {
    assert(trace_count < sizeof(trace)/sizeof(trace[0]));
    assert(nblocks > 0 && nblocks <= 256);
    assert(depth == original_depth + hardware);
    trace[trace_count++] = (struct trace){features[(feature_reads - 1) % feature_count],nblocks,data,hardware};
}
static void test_scalar_compress(ccdigest_state_t state, size_t nblocks, const void *data) {
    record(nblocks,data,0);
    ccsha256_ltc_di.compress(state,nblocks,data);
}
static void test_hardware_compress(ccdigest_state_t state, size_t nblocks, const void *data) {
    record(nblocks,data,1);
    native_sha256_compress_arm64(state,nblocks,data);
}
const struct ccdigest_info *ccsha256_di(void) {
    static struct ccdigest_info di;
    di = ccsha256_ltc_di;
    di.compress = test_scalar_compress;
    return &di;
}
/* Public KPI fallbacks: the production adapter's non-hardware build calls
 * these, so the fallback copy of the adapter exercises them for real. */
void SHA256_Init(SHA256_CTX *ctx) { fallback_calls[0]++; ccdigest_init(&ccsha256_ltc_di,ctx->ctx); }
void SHA256_Update(SHA256_CTX *ctx,const void *data,size_t len) { fallback_calls[1]++; ccdigest_update(&ccsha256_ltc_di,ctx->ctx,len,data); }
void SHA256_Final(void *out,SHA256_CTX *ctx) { fallback_calls[2]++; ccdigest_final(&ccsha256_ltc_di,ctx->ctx,out); }

#define XNU_SOURCE_CORECRYPTO14 1
#define CRYPTO_SHA2 1
#include "adapter-hardware-nova.inc"
#undef XNU_SOURCE_CORECRYPTO14
#undef CRYPTO_SHA2
/* The second inclusion models a production build without the source provider
 * and without CRYPTO_SHA2: every public entry point falls back to SHA256_*. */
#define native_sha256_digest native_sha256_digest_fallback
#define native_sha256_stream_init native_sha256_stream_init_fallback
#define native_sha256_stream_update native_sha256_stream_update_fallback
#define native_sha256_stream_final native_sha256_stream_final_fallback
#include "adapter-hardware-nova.inc"
#undef native_sha256_digest
#undef native_sha256_stream_init
#undef native_sha256_stream_update
#undef native_sha256_stream_final
extern void native_sha256_stream_init_fallback(SHA256_CTX *ctx);
extern void native_sha256_stream_update_fallback(SHA256_CTX *ctx, const void *data, size_t len);
extern void native_sha256_stream_final_fallback(void *digest, SHA256_CTX *ctx);
extern void native_sha256_digest_fallback(const void *data, size_t len, void *digest);

static void reset_trace(unsigned f, int nesting) {
    memset(trace,0,sizeof(trace)); trace_count = feature_reads = disable_calls = enable_calls = 0;
    features[0] = f; feature_count = 1; depth = original_depth = nesting;
}
static void verify_trace(void) {
    assert(depth == original_depth);
    assert(disable_calls == enable_calls && disable_calls == feature_reads && feature_reads == trace_count);
    for (size_t i=0;i<trace_count;i++) assert(trace[i].hardware == (trace[i].feature==1 || trace[i].feature==2));
}
static void digest(const struct ccdigest_info *di,const void *data,size_t len,unsigned char *out) {
    SHA256_CTX ctx;
    ccdigest_init(di,ctx.ctx); ccdigest_update(di,ctx.ctx,len,data); ccdigest_final(di,ctx.ctx,out);
}
static size_t stream_cases;
static void stream_reset(unsigned feature) { reset_trace(feature,0); }
static void stream_finish(SHA256_CTX *ctx, unsigned char out[32]) { native_sha256_stream_final(out,ctx); }

/* Update a context with a supplied chunk pattern.  A zero chunk is a genuine
 * zero-length update and must not reach the compressor. */
static void stream_chunks(SHA256_CTX *ctx, const unsigned char *data, size_t len,
                          const size_t *chunks, size_t nchunks) {
    size_t used = 0;
    for (size_t i=0;i<nchunks && used<len;i++) {
        size_t n = chunks[i];
        if (n > len-used) n = len-used;
        native_sha256_stream_update(ctx, data+used, n);
        used += n;
    }
    if (used < len) native_sha256_stream_update(ctx, data+used, len-used);
}
static void compare_stream(const void *data,size_t len,unsigned feature,int kind,int repeat_init) {
    unsigned char expected[32],actual[32];
    digest(&ccsha256_ltc_di,data,len,expected);
    SHA256_CTX ctx;
    stream_reset(feature);
    native_sha256_stream_init(&ctx);
    if (repeat_init) native_sha256_stream_init(&ctx);
    size_t before = trace_count;
    native_sha256_stream_update(&ctx,NULL,0);
    native_sha256_stream_update(&ctx,NULL,0);
    assert(trace_count == before);
    switch (kind) {
    case 0: native_sha256_stream_update(&ctx,data,len); break;
    case 1: {
        size_t used=0;
        while (used<len) { size_t n=1; if (n>len-used) n=len-used;
            native_sha256_stream_update(&ctx,(const unsigned char*)data+used,n); used+=n; }
        break;
    }
    case 2: {
        size_t used=0,step=1;
        while (used<len) { size_t n=(step*73)%197+1; if(n>len-used)n=len-used;
            native_sha256_stream_update(&ctx,(const unsigned char*)data+used,n); used+=n; step++; }
        break;
    }
    case 3: {
        static const size_t pattern[]={1,63,64,65,127,128,129,255,256,257,17,4096};
        stream_chunks(&ctx,data,len,pattern,sizeof(pattern)/sizeof(pattern[0]));
        break;
    }
    default: {
        static const size_t pattern[]={4096,1,8192,3,65536,7};
        stream_chunks(&ctx,data,len,pattern,sizeof(pattern)/sizeof(pattern[0]));
        break;
    }
    }
    native_sha256_stream_update(&ctx,NULL,0);
    stream_finish(&ctx,actual);
    verify_trace();
    assert(memcmp(expected,actual,32)==0);
    /* final must not destroy the context: a second final repeats the digest. */
    unsigned char again[32];
    native_sha256_stream_final(again,&ctx);
    assert(memcmp(expected,again,32)==0);
    stream_cases++;
}
static void known(const void *data,size_t len,const char *hex) {
    unsigned char out[32]; char result[65];
    stream_reset(1); SHA256_CTX ctx; native_sha256_stream_init(&ctx);
    native_sha256_stream_update(&ctx,data,len); native_sha256_stream_final(out,&ctx);
    verify_trace();
    for(size_t i=0;i<32;i++)sprintf(result+i*2,"%02x",out[i]);
    assert(strcmp(hex,result)==0);
    /* One-shot regression: the public one-shot entry point must match. */
    unsigned char one[32];
    stream_reset(1); native_sha256_digest(data,len,one); verify_trace();
    assert(memcmp(out,one,32)==0);
    compare_stream(data,len,1,0,0);
    compare_stream(data,len,1,1,0);
}
static void boundary_cases(const unsigned char *data) {
    static const size_t sizes[]={0,1,2,3,55,56,57,63,64,65,119,120,127,128,129,
                                 191,192,255,256,257,511,512,513,4095,4096,4097,
                                 16383,16384,16385,65535,65536,65537,1048576};
    for (size_t k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++) {
        for (unsigned f=0;f<16;f++) compare_stream(data,sizes[k],f,(int)(k%5),0);
    }
}
static void unaligned_cases(const unsigned char *data) {
    for (unsigned offset=0;offset<32;offset++)
        for (size_t n=0;n<160;n++)
            for (unsigned f=0;f<4;f++) compare_stream(data+offset,n,f,(int)(n%5),0);
}
static void repeat_init_case(const unsigned char *data) {
    unsigned char expected[32],actual[32];
    digest(&ccsha256_ltc_di,data,7777,expected);
    stream_reset(1); SHA256_CTX ctx;
    native_sha256_stream_init(&ctx);
    native_sha256_stream_update(&ctx,data,4000);
    native_sha256_stream_init(&ctx); /* repeat init resets all state */
    native_sha256_stream_update(&ctx,data,7777);
    native_sha256_stream_final(actual,&ctx);
    verify_trace(); assert(memcmp(expected,actual,32)==0);
    stream_cases++;
}
static void chunks(const unsigned char *data) {
    static const size_t counts[]={0,1,255,256,257,511,512,513,1025};
    uint32_t actual[8],expected[8];
    for(unsigned feature=0;feature<16;feature++) for(size_t k=0;k<sizeof(counts)/sizeof(counts[0]);k++) {
        size_t n=counts[k]; memcpy(actual,ccsha256_ltc_di.initial_state,32);memcpy(expected,actual,32);
        reset_trace(feature,2);
        native_sha256_compress((ccdigest_state_t)actual,n,n?data:NULL);
        verify_trace(); ccsha256_ltc_di.compress((ccdigest_state_t)expected,n,n?data:NULL);
        assert(memcmp(expected,actual,32)==0);
        assert(trace_count==(n+255)/256);
        size_t done=0;for(size_t i=0;i<trace_count;i++) { assert(trace[i].data==data+done*64);done+=trace[i].blocks; }
        assert(done==n);
    }
    reset_trace(1,0); features[0]=1;features[1]=0;features[2]=2;features[3]=15;feature_count=4;
    memcpy(actual,ccsha256_ltc_di.initial_state,32);memcpy(expected,actual,32);
    native_sha256_compress((ccdigest_state_t)actual,1025,data);
    verify_trace();ccsha256_ltc_di.compress((ccdigest_state_t)expected,1025,data);
    assert(trace_count==5 && memcmp(actual,expected,32)==0);
}
/* Exercise the batch bound and feature probing through real streaming calls. */
static void stream_batches(const unsigned char *data) {
    unsigned char expected[32],actual[32];
    const size_t bytes=1025*CCSHA256_BLOCK_SIZE;
    digest(&ccsha256_ltc_di,data,bytes,expected);
    stream_reset(1); SHA256_CTX ctx; native_sha256_stream_init(&ctx);
    native_sha256_stream_update(&ctx,data,bytes);
    assert(trace_count==5);
    static const size_t sizes[]={256,256,256,256,1};
    size_t done=0;
    for(size_t i=0;i<trace_count;i++){assert(trace[i].blocks==sizes[i]);assert(trace[i].data==data+done*CCSHA256_BLOCK_SIZE);done+=trace[i].blocks;}
    assert(done==1025);
    native_sha256_stream_final(actual,&ctx); verify_trace();
    assert(memcmp(expected,actual,32)==0);
    assert(trace_count==6 && trace[5].blocks==1);

    /* Feature changes between updates: each batch must read the current CPU
     * feature again and pick hardware or scalar accordingly. */
    stream_reset(1); features[0]=1;features[1]=0;features[2]=2;features[3]=15;feature_count=4;
    native_sha256_stream_init(&ctx);
    native_sha256_stream_update(&ctx,data,200*CCSHA256_BLOCK_SIZE);
    native_sha256_stream_update(&ctx,data+200*CCSHA256_BLOCK_SIZE,300*CCSHA256_BLOCK_SIZE);
    native_sha256_stream_update(&ctx,data+500*CCSHA256_BLOCK_SIZE,525*CCSHA256_BLOCK_SIZE);
    assert(trace_count==6);
    native_sha256_stream_final(actual,&ctx); verify_trace();
    assert(memcmp(expected,actual,32)==0);
    assert(trace_count==7);
    static const unsigned wanted[]={1,0,2,15,1,0,2};
    for(size_t i=0;i<trace_count;i++)assert(trace[i].feature==wanted[i]);
    stream_cases++;
}
static void vector_test(const unsigned char *data) {
    unsigned char before[512] __attribute__((aligned(16))),after[512] __attribute__((aligned(16)));
    uint32_t state[8],expected[8];
    const size_t counts[]={0,1,2,256};
    for(size_t k=0;k<4;k++) {
        for(size_t i=0;i<sizeof(before);i++)before[i]=(unsigned char)(i*73+k*31);
        memset(after,0,sizeof(after));memcpy(state,ccsha256_ltc_di.initial_state,32);memcpy(expected,state,32);
        preserve_vectors_nova(counts[k]?(ccdigest_state_t)state:NULL,counts[k],counts[k]?data:NULL,before,after);
        assert(memcmp(before,after,sizeof(before))==0);
        ccsha256_ltc_di.compress((ccdigest_state_t)expected,counts[k],data);
        assert(memcmp(state,expected,32)==0);
    }
}
static void fallback_cases(const unsigned char *data) {
    unsigned char expected[32],actual[32];
    digest(&ccsha256_ltc_di,data,12345,expected);
    memset(fallback_calls,0,sizeof(fallback_calls));
    SHA256_CTX ctx;
    native_sha256_stream_init_fallback(&ctx);
    native_sha256_stream_update_fallback(&ctx,data,5000);
    native_sha256_stream_update_fallback(&ctx,data+5000,7345);
    native_sha256_stream_final_fallback(actual,&ctx);
    assert(memcmp(expected,actual,32)==0);
    assert(fallback_calls[0]==1 && fallback_calls[1]==2 && fallback_calls[2]==1);

    memset(fallback_calls,0,sizeof(fallback_calls));
    native_sha256_digest_fallback(data,12345,actual);
    assert(memcmp(expected,actual,32)==0);
    assert(fallback_calls[0]==1 && fallback_calls[1]==1 && fallback_calls[2]==1);

    memset(fallback_calls,0,sizeof(fallback_calls));
    native_sha256_digest(data,12345,actual);
    assert(memcmp(expected,actual,32)==0);
    assert(fallback_calls[0]==0 && fallback_calls[1]==0 && fallback_calls[2]==0);
    stream_cases++;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static volatile unsigned sink;
int main(int argc, char **argv) {
    unsigned char *allocation=malloc(1100000);assert(allocation);
    uint32_t rng=0x94ac51f3;for(size_t i=0;i<1100000;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;allocation[i]=(unsigned char)rng;}
    if(argc>1) goto benchmark;
    known(NULL,0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    known("abc",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    known("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",56,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    unsigned char *million=malloc(1000000);assert(million);memset(million,'a',1000000);
    known(million,1000000,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");free(million);
    boundary_cases(allocation+7);
    unaligned_cases(allocation+3);
    repeat_init_case(allocation+11);
    chunks(allocation+1);stream_batches(allocation+5);vector_test(allocation+3);fallback_cases(allocation+9);
    printf("PASS: %zu streaming digest cases (known vectors, padding 55/56/63/64/65, unaligned, zero-length update, multiupdate, repeat init, feature sweep, repeat final)\n",stream_cases);
    printf("PASS: 144 feature/chunk cases plus migrating features; zero blocks; <=256-block batches; nested preemption pairing\n");
    printf("PASS: all q0-q31 unchanged for hardware block counts 0,1,2,256\n");
    printf("PASS: one-shot regression and non-hardware adapter fallback counters\n");
    if(argc==1){free(allocation);return 0;}
benchmark:;
    unsigned char out[32];struct ccdigest_info hardware=ccsha256_ltc_di;hardware.compress=native_sha256_compress_arm64;
    const size_t reps=4096;double t=now();for(size_t i=0;i<reps;i++){digest(&ccsha256_ltc_di,allocation,16384,out);sink+=out[0];}double scalar=now()-t;
    t=now();for(size_t i=0;i<reps;i++){digest(&hardware,allocation,16384,out);sink+=out[0];}double accelerated=now()-t;
    printf("HOST_ONLY_BENCH: %zu x 16384 bytes; scalar %.6f s; assembly %.6f s; ratio %.2f; not a handset measurement\n",reps,scalar,accelerated,scalar/accelerated);
    free(allocation);return 0;
}
