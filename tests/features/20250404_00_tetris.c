#include "tests/test.h"

int main(void) {
    // custom ldflags
    //    char* ldflags = "-L/usr/local/lib -lraylib";
    char *ldflags = "-framework IOKit -framework Cocoa";
    strcpy(LDFLAGS, ldflags);

    feature_test_build();
    //    feature_testar_test(NULL);
}
