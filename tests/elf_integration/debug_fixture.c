static int value = 40;
static int *pointer = &value;

#line 100 "debug_fixture.c"
__attribute__((noinline)) static int add(int left, int right) {
    return left + right;
}

#line 200 "debug_fixture.c"
__attribute__((noreturn, noinline)) static void exit_process(int status) {
#if defined(__x86_64__)
    __asm__ volatile("syscall"
                     :
                     : "a"(60L), "D"((long) status)
                     : "rcx", "r11", "memory");
#elif defined(__aarch64__)
    register long x0 __asm__("x0") = status;
    register long x8 __asm__("x8") = 93L;
    __asm__ volatile("svc 0" : : "r"(x0), "r"(x8) : "memory");
#elif defined(__riscv) && __riscv_xlen == 64
    register long a0 __asm__("a0") = status;
    register long a7 __asm__("a7") = 93L;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
#else
#error unsupported debug fixture architecture
#endif
    __builtin_unreachable();
}

__attribute__((noreturn)) void _start(void) {
    *pointer = add(*pointer, 2);
    exit_process(*pointer == 42 ? 0 : 1);
}
