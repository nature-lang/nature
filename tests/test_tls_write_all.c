#include "runtime/nutils/tls_write.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char *expected;
    size_t max_record;
    size_t received;
    int calls;
    int forced_result;
} fake_tls_writer_t;

static int fake_write_record(void *ctx, const unsigned char *buf, size_t len) {
    fake_tls_writer_t *writer = ctx;
    writer->calls += 1;
    if (writer->forced_result <= 0) {
        return writer->forced_result;
    }

    size_t accepted = len < writer->max_record ? len : writer->max_record;
    assert(buf == writer->expected + writer->received);
    writer->received += accepted;
    return (int) accepted;
}

static void test_retries_positive_short_writes(void) {
    unsigned char request[48 * 1024] = {0};
    fake_tls_writer_t writer = {
            .expected = request,
            .max_record = 16 * 1024,
            .forced_result = 1,
    };

    int64_t result = tls_write_all_records(&writer, fake_write_record, request, sizeof(request));
    assert(result == (int64_t) sizeof(request));
    assert(writer.received == sizeof(request));
    assert(writer.calls == 3);
}

static void test_propagates_no_progress_and_error(void) {
    unsigned char request[1] = {0};
    fake_tls_writer_t stalled = {.expected = request, .forced_result = 0};
    assert(tls_write_all_records(&stalled, fake_write_record, request, sizeof(request)) == 0);
    assert(stalled.calls == 1);

    fake_tls_writer_t failed = {.expected = request, .forced_result = -42};
    assert(tls_write_all_records(&failed, fake_write_record, request, sizeof(request)) == -42);
    assert(failed.calls == 1);
}

int main(void) {
    test_retries_positive_short_writes();
    test_propagates_no_progress_and_error();
    return 0;
}
