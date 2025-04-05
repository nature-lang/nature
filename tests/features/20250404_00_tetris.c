#include "tests/test.h"

int main(void) {
    char *ldflags = "-framework IOKit -framework Cocoa";
    strcpy(LDFLAGS, ldflags);

    feature_test_build();
    //    exec_imm_param();
    //    feature_testar_test(NULL);
}
