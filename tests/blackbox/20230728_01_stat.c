#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static struct stat *mock_stat() {
    struct stat *buf = mallocz(sizeof(struct stat));
    int s = stat("./mock/stat.txt", buf);
    assertf(s != -1, "stat failed");
    return buf;
}

static void test_basic() {
    struct stat *s = mock_stat();

    char *expect = dsprintf("%lu\n%lu\n%lu\n%lu\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n",
                            s->st_dev, s->st_ino, s->st_nlink, s->st_rdev,
                            s->st_size, s->st_blksize, s->st_blocks, s->st_mode,
                            s->st_uid, s->st_gid,
                            s->st_atim.tv_sec, s->st_atim.tv_nsec,
                            s->st_mtim.tv_sec, s->st_mtim.tv_nsec,
                            s->st_ctim.tv_sec, s->st_ctim.tv_nsec);

    char *raw = exec_output();

//    char *str = "";
    assert_string_equal(raw, expect);
}

int main(void) {
    TEST_BASIC
}