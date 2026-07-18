#include "src/syntax/scanner.h"

#include <stdio.h>
#include <string.h>

#define SCANNER_CHECK(condition)                                          \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "scanner number check failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                      \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int main(void) {
    module_t module = {0};
    module.source = "0x2545F4914F6CDD1D 0xffffffffffffffff 0o1777777777777777777777 "
                    "0b1111111111111111111111111111111111111111111111111111111111111111";

    linked_t *tokens = scanner(&module);
    static const char *const expected[] = {
            "2685821657736338717",
            "18446744073709551615",
            "18446744073709551615",
            "18446744073709551615",
    };

    linked_node *node = linked_first(tokens);
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        SCANNER_CHECK(node != NULL && node->value != NULL);
        token_t *token = node->value;
        SCANNER_CHECK(token->type == TOKEN_LITERAL_INT);
        SCANNER_CHECK(strcmp(token->literal, expected[i]) == 0);
        node = node->succ;
    }

    SCANNER_CHECK(node != NULL && node->value != NULL);
    SCANNER_CHECK(((token_t *) node->value)->type == TOKEN_EOF);
    return 0;
}
