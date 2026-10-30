.text
.p2align 2
.globl _preserve_vectors_nova
_preserve_vectors_nova:
    stp x29,x30,[sp,#-160]!
    stp x19,x20,[sp,#16]
    stp q8,q9,[sp,#32]
    stp q10,q11,[sp,#64]
    stp q12,q13,[sp,#96]
    stp q14,q15,[sp,#128]
    mov x19,x3
    mov x20,x4
    mov x5,x19
    ld1.16b {v0,v1,v2,v3},[x5],#64
    ld1.16b {v4,v5,v6,v7},[x5],#64
    ld1.16b {v8,v9,v10,v11},[x5],#64
    ld1.16b {v12,v13,v14,v15},[x5],#64
    ld1.16b {v16,v17,v18,v19},[x5],#64
    ld1.16b {v20,v21,v22,v23},[x5],#64
    ld1.16b {v24,v25,v26,v27},[x5],#64
    ld1.16b {v28,v29,v30,v31},[x5],#64
    bl _native_sha256_compress_arm64
    mov x5,x20
    st1.16b {v0,v1,v2,v3},[x5],#64
    st1.16b {v4,v5,v6,v7},[x5],#64
    st1.16b {v8,v9,v10,v11},[x5],#64
    st1.16b {v12,v13,v14,v15},[x5],#64
    st1.16b {v16,v17,v18,v19},[x5],#64
    st1.16b {v20,v21,v22,v23},[x5],#64
    st1.16b {v24,v25,v26,v27},[x5],#64
    st1.16b {v28,v29,v30,v31},[x5],#64
    ldp q8,q9,[sp,#32]
    ldp q10,q11,[sp,#64]
    ldp q12,q13,[sp,#96]
    ldp q14,q15,[sp,#128]
    ldp x19,x20,[sp,#16]
    ldp x29,x30,[sp],#160
    ret
