void test_ld_macho_unit(void);
void test_ld_macho_input(void);
void test_ld_macho_dylib(void);
void test_ld_macho_codegen(void);

void test_ld_macho(void) {
    test_ld_macho_unit();
    test_ld_macho_input();
    test_ld_macho_dylib();
    test_ld_macho_codegen();
}
