void test_ld_macho(void);
void test_ld_elf(void);

int main(void) {
    test_ld_macho();
    test_ld_elf();
    return 0;
}
