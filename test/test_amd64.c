#include "test.h"
#include "src/assembler/amd64/asm.h"

static void test_hello() {
  assert_true(hello());
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_hello)
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}