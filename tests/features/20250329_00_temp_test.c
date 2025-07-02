#include "tests/test.h"

int main(void) {
    //    char *ldflags = "-nostdlib -static";
    //    strcpy(LDFLAGS, ldflags);

    char *useld = "ld";
    strcpy(USE_LD, useld);

    feature_test_build();
    exec_imm_param();
    //    feature_testar_test(NULL);
}
