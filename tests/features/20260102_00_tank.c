#include "tests/test.h"

int main(void) {
    char *ldflags = "-framework IOKit -framework Cocoa";
    strcpy(LDFLAGS, ldflags);

    // 仅支持 darwin
#ifdef __DARWIN
    feature_test_build();
#endif
    exec_imm_param();
    //    feature_testar_test(NULL);
}
