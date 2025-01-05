#include "tests/test.h"
#include <stdio.h>

static struct stat *mock_stat() {
    struct stat *buf = mallocz(sizeof(struct stat));
    int s = stat("./asserts/stat.txt", buf);
    assertf(s != -1, "stat failed");
    return buf;
}

static struct stat *mock_fstat() {
    int fd = open("./asserts/stat.txt", O_RDONLY, 0666);
    struct stat *buf = mallocz(sizeof(struct stat));
    int s = fstat(fd, buf);
    assertf(s != -1, "stat failed");
    close(fd);
    return buf;
}

#ifdef __LINUX
#ifdef __AMD64
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
#elif __ARM64
static void test_basic() {
    struct stat *s = mock_stat();
    struct stat *s2 = mock_fstat();

    char *expect = dsprintf("%lu\n%lu\n%u\n%lu\n%ld\n%d\n%ld\n%u\n%u\n%u\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n"
                            "%lu\n%lu\n%u\n%lu\n%ld\n%d\n%ld\n%u\n%u\n%u\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n",
                            s->st_dev,      // u64 -> %lu
                            s->st_ino,      // u64 -> %lu
                            s->st_nlink,    // u32 -> %u
                            s->st_rdev,     // u64 -> %lu
                            s->st_size,     // i64 -> %ld
                            s->st_blksize,  // i32 -> %d
                            s->st_blocks,   // i64 -> %ld
                            s->st_mode,     // u32 -> %u
                            s->st_uid,      // u32 -> %u
                            s->st_gid,      // u32 -> %u
                            s->st_atim.tv_sec, s->st_atim.tv_nsec,  // i64 -> %ld
                            s->st_mtim.tv_sec, s->st_mtim.tv_nsec,  // i64 -> %ld
                            s->st_ctim.tv_sec, s->st_ctim.tv_nsec,  // i64 -> %ld
                            // 重复 s2 的相同顺序
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
#endif
#elif __DARWIN
static void test_basic() {
}
#endif

int main(void) {
    TEST_BASIC
}