void test_ld_macho_unit(void);
void test_ld_tapi(void);
void test_ld_macho_input(void);
void test_ld_macho_dylib(void);
void test_ld_macho_binary_dylib(void);
void test_ld_macho_codegen(void);
void test_ld_macho_platform(void);
void test_ld_macho_symbols(void);
void test_ld_macho_resolver(void);
void test_ld_macho_reloc(void);
void test_ld_macho_synthetic(void);
void test_ld_macho_unwind(void);

void test_ld_macho(void) {
    test_ld_macho_unit();
    test_ld_tapi();
    test_ld_macho_input();
    test_ld_macho_dylib();
    test_ld_macho_binary_dylib();
    test_ld_macho_platform();
    test_ld_macho_symbols();
    test_ld_macho_resolver();
    test_ld_macho_reloc();
    test_ld_macho_codegen();
    test_ld_macho_synthetic();
    test_ld_macho_unwind();
}
