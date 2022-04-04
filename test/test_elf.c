#include "test.h"
#include <stdio.h>
#include "src/assembler/elf/elf.h"
#include "src/lib/helper.h"

static void test_elf_to_file() {
  uint8_t binary[5] = {1, 2, 3, 4, 5};
  filename = str_connect("test.n", ".o");
  elf_to_file(binary, 5, filename);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_elf_to_file),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}