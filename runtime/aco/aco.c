// Copyright 2018 Sen Han <00hnes@gmail.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "aco.h"

#include <stdio.h>

#include "runtime/runtime.h"

// this header including should be at the last of the `include` directives list

static uint64_t share_stack_base = MMAP_SHARE_STACK_BASE;

void aco_runtime_test(void) {
#if  defined(__x86_64__) || defined(__aarch64__)
    assert(sizeof(void *) == 8 && "require 'sizeof(void*) == 8'");
    assert(sizeof(__uint128_t) == 16 && "require 'sizeof(__uint128_t) == 16'");
#else
#error "platform no support yet"
#endif
    assert(sizeof(int) >= 4);
    assert(sizeof(int) <= sizeof(size_t));
}

// assertptr(dst); assertptr(src);
// assert((((uintptr_t)(src) & 0x0f) == 0) && (((uintptr_t)(dst) & 0x0f) == 0));
// assert((((sz) & 0x0f) == 0x08) && (((sz) >> 4) >= 0) && (((sz) >> 4) <= 8));
// sz = 16*n + 8 ( 0 <= n <= 8)

// Note: dst and src must be valid address already
#define aco_amd64_inline_short_aligned_memcpy_test_ok(dst, src, sz)                                                             \
    ((((uintptr_t) (src) & 0x0f) == 0) && (((uintptr_t) (dst) & 0x0f) == 0) && (((sz) & 0x0f) == 0x08) && (((sz) >> 4) >= 0) && \
     (((sz) >> 4) <= 8))

#define aco_amd64_inline_short_aligned_memcpy(dst, src, sz)                                              \
    do {                                                                                                 \
        __uint128_t xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;                                      \
        switch ((sz) >> 4) {                                                                             \
            case 0:                                                                                      \
                break;                                                                                   \
            case 1:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                break;                                                                                   \
            case 2:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                xmm1 = *((__uint128_t *) (src) + 1);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                *((__uint128_t *) (dst) + 1) = xmm1;                                                     \
                break;                                                                                   \
            case 3:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                xmm1 = *((__uint128_t *) (src) + 1);                                                     \
                xmm2 = *((__uint128_t *) (src) + 2);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                *((__uint128_t *) (dst) + 1) = xmm1;                                                     \
                *((__uint128_t *) (dst) + 2) = xmm2;                                                     \
                break;                                                                                   \
            case 4:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                xmm1 = *((__uint128_t *) (src) + 1);                                                     \
                xmm2 = *((__uint128_t *) (src) + 2);                                                     \
                xmm3 = *((__uint128_t *) (src) + 3);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                *((__uint128_t *) (dst) + 1) = xmm1;                                                     \
                *((__uint128_t *) (dst) + 2) = xmm2;                                                     \
                *((__uint128_t *) (dst) + 3) = xmm3;                                                     \
                break;                                                                                   \
            case 5:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                xmm1 = *((__uint128_t *) (src) + 1);                                                     \
                xmm2 = *((__uint128_t *) (src) + 2);                                                     \
                xmm3 = *((__uint128_t *) (src) + 3);                                                     \
                xmm4 = *((__uint128_t *) (src) + 4);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                *((__uint128_t *) (dst) + 1) = xmm1;                                                     \
                *((__uint128_t *) (dst) + 2) = xmm2;                                                     \
                *((__uint128_t *) (dst) + 3) = xmm3;                                                     \
                *((__uint128_t *) (dst) + 4) = xmm4;                                                     \
                break;                                                                                   \
            case 6:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                xmm1 = *((__uint128_t *) (src) + 1);                                                     \
                xmm2 = *((__uint128_t *) (src) + 2);                                                     \
                xmm3 = *((__uint128_t *) (src) + 3);                                                     \
                xmm4 = *((__uint128_t *) (src) + 4);                                                     \
                xmm5 = *((__uint128_t *) (src) + 5);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                *((__uint128_t *) (dst) + 1) = xmm1;                                                     \
                *((__uint128_t *) (dst) + 2) = xmm2;                                                     \
                *((__uint128_t *) (dst) + 3) = xmm3;                                                     \
                *((__uint128_t *) (dst) + 4) = xmm4;                                                     \
                *((__uint128_t *) (dst) + 5) = xmm5;                                                     \
                break;                                                                                   \
            case 7:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                xmm1 = *((__uint128_t *) (src) + 1);                                                     \
                xmm2 = *((__uint128_t *) (src) + 2);                                                     \
                xmm3 = *((__uint128_t *) (src) + 3);                                                     \
                xmm4 = *((__uint128_t *) (src) + 4);                                                     \
                xmm5 = *((__uint128_t *) (src) + 5);                                                     \
                xmm6 = *((__uint128_t *) (src) + 6);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                *((__uint128_t *) (dst) + 1) = xmm1;                                                     \
                *((__uint128_t *) (dst) + 2) = xmm2;                                                     \
                *((__uint128_t *) (dst) + 3) = xmm3;                                                     \
                *((__uint128_t *) (dst) + 4) = xmm4;                                                     \
                *((__uint128_t *) (dst) + 5) = xmm5;                                                     \
                *((__uint128_t *) (dst) + 6) = xmm6;                                                     \
                break;                                                                                   \
            case 8:                                                                                      \
                xmm0 = *((__uint128_t *) (src) + 0);                                                     \
                xmm1 = *((__uint128_t *) (src) + 1);                                                     \
                xmm2 = *((__uint128_t *) (src) + 2);                                                     \
                xmm3 = *((__uint128_t *) (src) + 3);                                                     \
                xmm4 = *((__uint128_t *) (src) + 4);                                                     \
                xmm5 = *((__uint128_t *) (src) + 5);                                                     \
                xmm6 = *((__uint128_t *) (src) + 6);                                                     \
                xmm7 = *((__uint128_t *) (src) + 7);                                                     \
                *((__uint128_t *) (dst) + 0) = xmm0;                                                     \
                *((__uint128_t *) (dst) + 1) = xmm1;                                                     \
                *((__uint128_t *) (dst) + 2) = xmm2;                                                     \
                *((__uint128_t *) (dst) + 3) = xmm3;                                                     \
                *((__uint128_t *) (dst) + 4) = xmm4;                                                     \
                *((__uint128_t *) (dst) + 5) = xmm5;                                                     \
                *((__uint128_t *) (dst) + 6) = xmm6;                                                     \
                *((__uint128_t *) (dst) + 7) = xmm7;                                                     \
                break;                                                                                   \
        }                                                                                                \
        *((uint64_t *) ((uintptr_t) (dst) + (sz) - 8)) = *((uint64_t *) ((uintptr_t) (src) + (sz) - 8)); \
    } while (0)

// Note: dst and src must be valid address already
#define aco_amd64_optimized_memcpy_drop_in(dst, src, sz)                         \
    do {                                                                         \
        if (aco_amd64_inline_short_aligned_memcpy_test_ok((dst), (src), (sz))) { \
            aco_amd64_inline_short_aligned_memcpy((dst), (src), (sz));           \
        } else {                                                                 \
            memcpy((dst), (src), (sz));                                          \
        }                                                                        \
    } while (0)

static void aco_default_protector_last_word(void) {
    aco_t *co = aco_get_co();
    // do some log about the offending `co`
    fprintf(stderr, "error: aco_default_protector_last_word triggered\n");
    fprintf(stderr,
            "error: co:%p should call `aco_exit()` instead of direct "
            "`return` in co_fp:%p to finish its execution\n",
            co, (void *) co->fp);
    assert(0);
}

// aco's Global Thread Local Storage variable `co`
uv_key_t aco_gtls_co;

uv_key_t aco_gtls_last_word_fp;

uv_key_t aco_gtls_fpucw_mxcsr;
pthread_mutex_t share_stack_lock;

void aco_thread_init(aco_cofuncp_t last_word_co_fp) {
    void *fpucw = uv_key_get(&aco_gtls_fpucw_mxcsr);
    aco_save_fpucw_mxcsr(&fpucw);

    // 必须使用 uv_key_set 写入, 否则指针写入无效
    uv_key_set(&aco_gtls_fpucw_mxcsr, fpucw);

    if ((void *) last_word_co_fp != NULL) {
        uv_key_set(&aco_gtls_last_word_fp, (void *) last_word_co_fp);
    } else {
        uv_key_set(&aco_gtls_last_word_fp, (void *) aco_default_protector_last_word);
    }
}

// This function `aco_funcp_protector` should never be
// called. If it's been called, that means the offending
// `co` didn't call aco_exit(co) instead of `return` to
// finish its execution.
void aco_funcp_protector(void) {
    aco_cofuncp_t fp = uv_key_get(&aco_gtls_last_word_fp);
    if (fp != NULL) {
        fp();
    } else {
        aco_default_protector_last_word();
    }
    assert(0);
}

#define aco_size_t_safe_add_assert(a, b) \
    do {                                 \
        assert((a) + (b) >= (a));        \
    } while (0)

// safe
void *aco_share_stack_init(aco_share_stack_t *p, size_t sz) {
    assert(p);
    memset(p, 0, sizeof(aco_share_stack_t));

#ifdef __LINUX
    pthread_spin_init(&p->owner_lock, PTHREAD_PROCESS_SHARED);
#else
    pthread_mutex_init(&p->owner_lock, NULL);
#endif

    if (sz == 0) {
        sz = 1024 * 1024 * 2;
    }
    if (sz < 4096) {
        sz = 4096;
    }
    assert(sz > 0);

    size_t u_pgsz = 0;

    // although gcc's Built-in Functions to Perform Arithmetic with
    // Overflow Checking is better, but it would require gcc >= 5.0
    long pgsz = sysconf(_SC_PAGESIZE);
    // pgsz must be > 0 && a power of two
    assert(pgsz > 0 && (((pgsz - 1) & pgsz) == 0));
    u_pgsz = (size_t) ((unsigned long) pgsz);
    // it should be always true in real life
    assert(u_pgsz == (unsigned long) pgsz && ((u_pgsz << 1) >> 1) == u_pgsz);
    if (sz <= u_pgsz) {
        sz = u_pgsz << 1;
    } else {
        size_t new_sz;
        if ((sz & (u_pgsz - 1)) != 0) {
            new_sz = (sz & (~(u_pgsz - 1)));
            assert(new_sz >= u_pgsz);
            aco_size_t_safe_add_assert(new_sz, (u_pgsz << 1));
            new_sz = new_sz + (u_pgsz << 1);
            assert(sz / u_pgsz + 2 == new_sz / u_pgsz);
        } else {
            aco_size_t_safe_add_assert(sz, u_pgsz);
            new_sz = sz + u_pgsz;
            assert(sz / u_pgsz + 1 == new_sz / u_pgsz);
        }
        sz = new_sz;
        assert((sz / u_pgsz > 1) && ((sz & (u_pgsz - 1)) == 0));
    }

    pthread_mutex_lock(&share_stack_lock);
    p->real_ptr = mmap((void *) share_stack_base, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    DEBUGF("[aco_share_stack_init] share_stack real_ptr=%p", p->real_ptr);
    //    share_stack_base += (uint64_t) 64 * 1024 * 1024 * 1024;
    share_stack_base += sz;
    pthread_mutex_unlock(&share_stack_lock);

    assert(p->real_ptr != MAP_FAILED);
    p->guard_page_enabled = 1;
    mprotect(p->real_ptr, u_pgsz, PROT_READ);
    // assert(0 == mprotect(p->real_ptr, u_pgsz, PROT_READ));

    p->ptr = (void *) (((uintptr_t) p->real_ptr) + u_pgsz);
    p->real_sz = sz;
    assert(sz >= (u_pgsz << 1));
    p->sz = sz - u_pgsz;

    p->owner = NULL;
#if defined(__x86_64__) || defined(__aarch64__)
    uintptr_t u_p = (uintptr_t) (p->sz - (sizeof(void *) << 1) + (uintptr_t) p->ptr);
    u_p = (u_p >> 4) << 4;
    p->align_highptr = (void *) u_p;

#ifdef __aarch64__
    // aarch64 hardware-enforces 16 byte stack alignment
    p->align_retptr  = (void*)(u_p - 16);
#else
    p->align_retptr  = (void*)(u_p - sizeof(void*));
#endif

    *((void **) (p->align_retptr)) = (void *) (aco_funcp_protector_asm);
    assert(p->sz > (16 + (sizeof(void *) << 1) + sizeof(void *)));
    p->align_limit = p->sz - 16 - (sizeof(void *) << 1);
#else
#error "platform no support yet"
#endif
}

// safe
void aco_share_stack_destroy(aco_share_stack_t *sstk) {
    assert(sstk != NULL && sstk->ptr != NULL);
    munmap(sstk->real_ptr, sstk->real_sz);
    sstk->real_ptr = NULL;
    sstk->ptr = NULL;
}

void aco_create_init(aco_t *aco, aco_t *main_co, aco_share_stack_t *share_stack, size_t save_stack_sz, aco_cofuncp_t fp,
                     void *arg) {
    assert(aco);
    memset(aco, 0, sizeof(aco_t));

    aco->ctx.msg = NULL;

    if (main_co != NULL) {// non-main co
        assert(share_stack);
        aco->share_stack = share_stack;

#ifdef __x86_64__
        aco->reg[ACO_REG_IDX_RETADDR] = (void *) fp;
        aco->reg[ACO_REG_IDX_SP] = aco->share_stack->align_retptr;
        aco->reg[ACO_REG_IDX_FPU] = uv_key_get(&aco_gtls_fpucw_mxcsr);
#elif defined(__aarch64__)
        aco->reg[ACO_REG_IDX_RETADDR] = (void*)fp;
        aco->reg[ACO_REG_IDX_SP] = aco->share_stack->align_retptr;
#else
#error "platform no support yet"
#endif
        aco->main_co = main_co;
        aco->arg = arg;
        aco->fp = fp;
        if (save_stack_sz == 0) {// save_stack 默认大小为 64byte
            save_stack_sz = 64;
        }

        aco->save_stack.ptr = rti_gc_malloc(save_stack_sz, NULL);
        DEBUGF("[aco_create_init] save_stack.ptr=%p, size=%ld", aco->save_stack.ptr, save_stack_sz);

        assert(aco->save_stack.ptr);
        aco->save_stack.sz = save_stack_sz;
#if defined(__x86_64__) || defined(__aarch64__)
        aco->save_stack.valid_sz = 0;
#else
#error "platform no support yet"
#endif
    } else {
        aco->main_co = NULL;
        aco->arg = arg;
        aco->fp = fp;
        aco->share_stack = NULL;
        aco->save_stack.ptr = NULL;
    }

    aco->inited = true;
}

aco_attr_no_asan void aco_resume(aco_t *resume_co) {
    assert(resume_co != NULL && resume_co->main_co != NULL && resume_co->is_end == 0);

    // 栈切换
    if (resume_co->share_stack->owner != resume_co) {
#ifdef __LINUX
        pthread_spin_lock(&resume_co->share_stack->owner_lock);
#else
        pthread_mutex_lock(&resume_co->share_stack->owner_lock);
#endif

        if (resume_co->share_stack->owner != NULL) {
            aco_t *owner_co = resume_co->share_stack->owner;
            assert(owner_co->share_stack == resume_co->share_stack);

#if defined(__x86_64__) || defined(__aarch64__)
            assert(((uintptr_t) (owner_co->share_stack->align_retptr) >= (uintptr_t) (owner_co->reg[ACO_REG_IDX_SP])) &&
                   ((uintptr_t) (owner_co->share_stack->align_highptr) -
                            (uintptr_t) (owner_co->share_stack->align_limit) <=
                    (uintptr_t) (owner_co->reg[ACO_REG_IDX_SP])));

            owner_co->save_stack.valid_sz =
                    (uintptr_t) (owner_co->share_stack->align_retptr) - (uintptr_t) (owner_co->reg[ACO_REG_IDX_SP]);

            // save 栈增长
            if (owner_co->save_stack.sz < owner_co->save_stack.valid_sz) {
                owner_co->save_stack.ptr = NULL;
                while (1) {
                    owner_co->save_stack.sz = owner_co->save_stack.sz << 1;
                    assert(owner_co->save_stack.sz > 0);
                    if (owner_co->save_stack.sz >= owner_co->save_stack.valid_sz) {
                        break;
                    }
                }
                // gc malloc TODO write_barrier
                owner_co->save_stack.ptr = rti_gc_malloc(owner_co->save_stack.sz, NULL);
                assert(owner_co->save_stack.ptr);
            }

            // TODO: optimize the performance penalty of memcpy function call
            //   for very short memory span
            if (owner_co->save_stack.valid_sz > 0) {
#ifdef __x86_64__
                aco_amd64_optimized_memcpy_drop_in(owner_co->save_stack.ptr, owner_co->reg[ACO_REG_IDX_SP],
                                                   owner_co->save_stack.valid_sz);
#else
                memcpy(owner_co->save_stack.ptr, owner_co->reg[ACO_REG_IDX_SP], owner_co->save_stack.valid_sz);
#endif
                owner_co->save_stack.ct_save++;
            }
            if (owner_co->save_stack.valid_sz > owner_co->save_stack.max_cpsz) {
                owner_co->save_stack.max_cpsz = owner_co->save_stack.valid_sz;
            }
            owner_co->share_stack->owner = NULL;
            owner_co->share_stack->align_validsz = 0;
#else
#error "platform no support yet"
#endif
        }

        assert(resume_co->share_stack->owner == NULL);
#if defined(__x86_64__) || defined(__aarch64__)
        assert(resume_co->save_stack.valid_sz <= resume_co->share_stack->align_limit - sizeof(void *));
        // TODO: optimize the performance penalty of memcpy function call
        //   for very short memory span
        if (resume_co->save_stack.valid_sz > 0) {
#ifdef __x86_64__
            aco_amd64_optimized_memcpy_drop_in(
                    (void *) ((uintptr_t) (resume_co->share_stack->align_retptr) - resume_co->save_stack.valid_sz),
                    resume_co->save_stack.ptr, resume_co->save_stack.valid_sz);
#else
            memcpy((void *) ((uintptr_t) (resume_co->share_stack->align_retptr) - resume_co->save_stack.valid_sz),
                   resume_co->save_stack.ptr,
                   resume_co->save_stack.valid_sz);
#endif
            resume_co->save_stack.ct_restore++;
        }

        if (resume_co->save_stack.valid_sz > resume_co->save_stack.max_cpsz) {
            resume_co->save_stack.max_cpsz = resume_co->save_stack.valid_sz;
        }
        resume_co->share_stack->align_validsz = resume_co->save_stack.valid_sz + sizeof(void *);
        resume_co->share_stack->owner = resume_co;

#else
#error "platform no support yet"
#endif

#ifdef __LINUX
        pthread_spin_unlock(&resume_co->share_stack->owner_lock);
#else
        pthread_mutex_unlock(&resume_co->share_stack->owner_lock);
#endif
    }

    uv_key_set(&aco_gtls_co, resume_co);
    acosw(resume_co->main_co, resume_co);
    uv_key_set(&aco_gtls_co, resume_co->main_co);
}

aco_attr_no_asan void aco_share_spill(aco_t *aco) {
    assert(aco->reg[ACO_REG_IDX_RETADDR]);
    assert(aco->reg[ACO_REG_IDX_SP]);
    assert(aco->share_stack->owner == aco);

    aco->save_stack.valid_sz = (uintptr_t) (aco->share_stack->align_retptr) - (uintptr_t) (aco->reg[ACO_REG_IDX_SP]);

    // realloc stack
    if (aco->save_stack.sz < aco->save_stack.valid_sz) {
        free(aco->save_stack.ptr);
        aco->save_stack.ptr = NULL;

        while (true) {
            // <<1 equals *2
            aco->save_stack.sz = aco->save_stack.sz << 1;
            assert(aco->save_stack.sz > 0);
            if (aco->save_stack.sz >= aco->save_stack.valid_sz) {
                break;
            }
        }

        aco->save_stack.ptr = rti_gc_malloc(aco->save_stack.sz, NULL);
        assert(aco->save_stack.ptr);
    }

    // 保存栈数据
    if (aco->save_stack.valid_sz > 0) {
#ifdef __x86_64__
        aco_amd64_optimized_memcpy_drop_in(aco->save_stack.ptr, aco->reg[ACO_REG_IDX_SP], aco->save_stack.valid_sz);
#else
        memcpy(aco->save_stack.ptr, aco->reg[ACO_REG_IDX_SP], aco->save_stack.valid_sz);
#endif
    }

    if (aco->save_stack.valid_sz > aco->save_stack.max_cpsz) {
        aco->save_stack.max_cpsz = aco->save_stack.valid_sz;
    }
}

void aco_destroy(aco_t *co) {
    assert(co);
    if (!aco_is_main_co(co)) {
        if (co->share_stack->owner == co) {
            co->share_stack->owner = NULL;
            co->share_stack->align_validsz = 0;
        }

        co->save_stack.ptr = NULL;
    }
}
