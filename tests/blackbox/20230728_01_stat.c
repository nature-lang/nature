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

static struct stat *mock_fstat() {
    int fd = open("./mock/stat.txt", O_RDONLY, 0666);
    struct stat *buf = mallocz(sizeof(struct stat));
    int s = fstat(fd, buf);
    assertf(s != -1, "stat failed");
    close(fd);
    return buf;
}

static void test_basic() {
    struct stat *s = mock_stat();
    struct stat *s2 = mock_fstat();

    char *expect = dsprintf("%lu\n%lu\n%lu\n%lu\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n"
                            "%lu\n%lu\n%lu\n%lu\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n",
                            s->st_dev, s->st_ino, s->st_nlink, s->st_rdev,
                            s->st_size, s->st_blksize, s->st_blocks, s->st_mode,
                            s->st_uid, s->st_gid,
                            s->st_atim.tv_sec, s->st_atim.tv_nsec,
                            s->st_mtim.tv_sec, s->st_mtim.tv_nsec,
                            s->st_ctim.tv_sec, s->st_ctim.tv_nsec,
                            s2->st_dev, s2->st_ino, s2->st_nlink, s2->st_rdev,
                            s2->st_size, s2->st_blksize, s2->st_blocks, s2->st_mode,
                            s2->st_uid, s2->st_gid,
                            s2->st_atim.tv_sec, s2->st_atim.tv_nsec,
                            s2->st_mtim.tv_sec, s2->st_mtim.tv_nsec,
                            s2->st_ctim.tv_sec, s2->st_ctim.tv_nsec);

    expect = str_connect(expect, "No such file or directory\n");

    char *raw = exec_output();
    assert_string_equal(raw, expect);
}

int main(void) {
    TEST_BASIC
}