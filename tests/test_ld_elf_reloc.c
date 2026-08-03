void test_ld_elf_reloc_aarch64(void);
void test_ld_elf_reloc_x86_64(void);
void test_ld_elf_reloc_x86_64_tls(void);
void test_ld_elf_reloc_riscv64(void);

void test_ld_elf_reloc(void) {
    test_ld_elf_reloc_aarch64();
    test_ld_elf_reloc_x86_64();
    test_ld_elf_reloc_x86_64_tls();
    test_ld_elf_reloc_riscv64();
}
