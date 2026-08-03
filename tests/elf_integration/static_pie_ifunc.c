static int selected_impl(void) {
    return 17;
}

#if defined(NATURE_STATIC_PIE_PLAIN_SELECTED)
__attribute__((visibility("hidden"))) int selected(void) {
    return selected_impl();
}
#else
static void *select_impl(void) {
    return (void *) selected_impl;
}

int selected(void)
        __attribute__((ifunc("select_impl"), visibility("hidden")));
#endif
