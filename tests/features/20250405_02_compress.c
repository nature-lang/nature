#include "tests/test.h"

int main(void) {
//    char *ldflags = "-lbz2 -lz -liconv";
//    strcpy(LDFLAGS, ldflags);

//    char *useld = "ld";
//    strcpy(USE_LD, useld);

    feature_test_build();
    char* actual = exec_output();
//    printf("%s", actual);
//    return 0;
    char* expect = "encode success\n"
                   "decode success\n";

    assert_string_equal(actual, expect);
}
