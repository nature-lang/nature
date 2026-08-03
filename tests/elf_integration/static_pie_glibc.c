static int global_value = 40;
static int *global_pointer = &global_value;
static __thread int tls_value = 5;
static int constructor_ran;

static int selected_impl(void) {
    return 17;
}

static void *select_impl(void) {
    return selected_impl;
}

int selected(void) __attribute__((ifunc("select_impl")));

__attribute__((constructor)) static void initialize(void) {
    *global_pointer += 2;
    tls_value += 3;
    constructor_ran = 1;
}

int main(void) {
    return *global_pointer == 42 && tls_value == 8 && constructor_ran == 1 &&
                           selected() == 17
                   ? 0
                   : 1;
}
