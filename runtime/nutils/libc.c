#include "libc.h"
#include <arpa/inet.h>

n_u16_t libc_htons(n_u16_t host) {
    return htons(host);
}
