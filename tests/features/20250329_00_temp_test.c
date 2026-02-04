#include "tests/test.h"

int main(void) {
    BUILD_TEST = true;
    //    char *ldflags = "-nostdlib -static";
    //    strcpy(LDFLAGS, ldflags);

    //    char *useld = "ld";
    //    strcpy(USE_LD, useld);

    //    chdir("/Users/weiwenhao/Code/emoji-api");
    //    setenv("ENTRY_FILE", "main.n", 1);

    feature_test_build();
    exec_imm_param();
    //    feature_testar_test(NULL);
}
