#include "libc.h"
#include "string.h"
#include <arpa/inet.h>
#include <time.h>


n_u16_t libc_htons(n_u16_t host) {
    return htons(host);
}

n_struct_t *libc_localtime(n_int64_t timestamp) {
    struct tm *timeinfo = localtime(&timestamp);
    n_string_t *str = string_new((void *) timeinfo->tm_zone, strlen(timeinfo->tm_zone));
    timeinfo->tm_zone = (char *) str;

    return (n_struct_t *) timeinfo;
}

n_int64_t libc_mktime(n_struct_t *timeinfo) {
    struct tm *tp = (struct tm *) timeinfo;
    tp->tm_zone = NULL;
    n_int64_t timestamp = mktime(tp);

    return timestamp;
}

n_string_t *libc_strftime(n_struct_t *timeinfo, char *format) {
    struct tm *tp = (struct tm *) timeinfo;
    tp->tm_zone = NULL;
    char buffer[100];
    strftime(buffer, sizeof(buffer), format, tp);

    return string_new(buffer, strlen(buffer));
}
