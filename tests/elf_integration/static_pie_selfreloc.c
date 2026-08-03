typedef unsigned long u64;
typedef long s64;
typedef unsigned int u32;

typedef struct {
    s64 tag;
    u64 value;
} elf_dyn_t;

typedef struct {
    u64 offset;
    u64 info;
    s64 addend;
} elf_rela_t;

extern unsigned char __ehdr_start[] __attribute__((visibility("hidden")));
extern elf_dyn_t _DYNAMIC[] __attribute__((visibility("hidden")));
extern void (*__init_array_start[])(void)
        __attribute__((visibility("hidden")));
extern void (*__init_array_end[])(void)
        __attribute__((visibility("hidden")));

#if defined(__x86_64__)
#define TEST_RELATIVE 8U
#define TEST_IRELATIVE 37U
#elif defined(__aarch64__)
#define TEST_RELATIVE 1027U
#define TEST_IRELATIVE 1032U
#elif defined(__riscv) && __riscv_xlen == 64
#define TEST_RELATIVE 3U
#define TEST_IRELATIVE 58U
#else
#error unsupported fixture architecture
#endif

static int global_value = 40;
static int *volatile global_pointer = &global_value;
static int constructor_ran;
static __thread int tls_value = 5;

extern int selected(void) __attribute__((visibility("hidden")));

__attribute__((constructor)) static void initialize(void) {
    *global_pointer += 2;
    tls_value += 3;
    constructor_ran = 1;
}

static int relocate_image(void) {
    u64 base = (u64) __ehdr_start;
    u64 rela_address = 0U;
    u64 rela_size = 0U;
    u64 rela_entry_size = 0U;
    for (elf_dyn_t *dynamic = _DYNAMIC; dynamic->tag != 0; dynamic++) {
        if (dynamic->tag == 7)
            rela_address = dynamic->value;
        else if (dynamic->tag == 8)
            rela_size = dynamic->value;
        else if (dynamic->tag == 9)
            rela_entry_size = dynamic->value;
    }
    if (rela_size == 0U) return 0;
    if (rela_entry_size != sizeof(elf_rela_t) ||
        rela_size % rela_entry_size != 0U)
        return -1;
    elf_rela_t *relocations = (elf_rela_t *) (base + rela_address);
    u64 count = rela_size / rela_entry_size;
    for (u64 i = 0U; i < count; i++) {
        u32 type = (u32) relocations[i].info;
        if (type == TEST_RELATIVE) {
            u64 *place = (u64 *) (base + relocations[i].offset);
            *place = base + (u64) relocations[i].addend;
        }
    }
    for (u64 i = 0U; i < count; i++) {
        u32 type = (u32) relocations[i].info;
        if (type == TEST_IRELATIVE) {
            u64 *place = (u64 *) (base + relocations[i].offset);
            void *(*resolver)(void) =
                    (void *(*) (void) )(base +
                                        (u64) relocations[i].addend);
            *place = (u64) resolver();
        } else if (type != TEST_RELATIVE) {
            return -2;
        }
    }
    return 0;
}

static int setup_tls(void) {
    unsigned char *base = __ehdr_start;
    u64 program_offset = *(u64 *) (base + 32U);
    unsigned short program_entry_size =
            *(unsigned short *) (base + 54U);
    unsigned short program_count = *(unsigned short *) (base + 56U);
    for (unsigned short i = 0U; i < program_count; i++) {
        unsigned char *program =
                base + program_offset + (u64) i * program_entry_size;
        if (*(u32 *) program != 7U) continue;
        u64 tls_address = *(u64 *) (program + 16U);
#if defined(__x86_64__)
        u64 tls_size = *(u64 *) (program + 40U);
        u64 tls_alignment = *(u64 *) (program + 48U);
        if (tls_alignment == 0U) tls_alignment = 1U;
        u64 thread_pointer = (tls_address + tls_size + tls_alignment - 1U) &
                             ~(tls_alignment - 1U);
        long result;
        __asm__ volatile("syscall"
                         : "=a"(result)
                         : "a"(158L), "D"(0x1002L),
                           "S"((u64) base + thread_pointer)
                         : "rcx", "r11", "memory");
        return result == 0L ? 0 : -3;
#elif defined(__aarch64__)
        u64 tls_alignment = *(u64 *) (program + 48U);
        if (tls_alignment == 0U) tls_alignment = 1U;
        if (tls_address < 16U) return -3;
        u64 thread_pointer =
                (tls_address - 16U) & ~(tls_alignment - 1U);
        thread_pointer += (u64) base;
        __asm__ volatile("msr tpidr_el0, %0"
                         :
                         : "r"(thread_pointer)
                         : "memory");
        return 0;
#else
        u64 thread_pointer = (u64) base + tls_address;
        __asm__ volatile("mv tp, %0" : : "r"(thread_pointer) : "memory");
        return 0;
#endif
    }
    return -4;
}

__attribute__((noreturn)) static void exit_process(int status) {
#if defined(__x86_64__)
    __asm__ volatile("syscall"
                     :
                     : "a"(60L), "D"((long) status)
                     : "rcx", "r11", "memory");
#elif defined(__aarch64__)
    register long x0 __asm__("x0") = status;
    register long x8 __asm__("x8") = 93L;
    __asm__ volatile("svc #0" : : "r"(x0), "r"(x8) : "memory");
#else
    register long a0 __asm__("a0") = status;
    register long a7 __asm__("a7") = 93L;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
#endif
    __builtin_unreachable();
}

__attribute__((noreturn)) void _start(void) {
    int status = relocate_image();
    if (status == 0) status = setup_tls();
    if (status == 0) {
        for (void (**constructor)(void) = __init_array_start;
             constructor != __init_array_end; constructor++) {
            (*constructor)();
        }
        status = *global_pointer == 42 && tls_value == 8 &&
                                 constructor_ran == 1 && selected() == 17
                         ? 0
                         : 3;
    }
    exit_process(status);
}
