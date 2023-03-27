#include "token.h"
#include "src/debug/debug.h"



token_t *token_new(uint8_t type, char *literal, int line) {
    token_t *t = malloc(sizeof(token_t));
    t->type = type;
    t->literal = literal;
    t->line = line;

#ifdef DEBUG_SCANNER
    debug_scanner(t);
#endif
    return t;
}
