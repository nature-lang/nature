#include "token.h"
#include "src/debug/debug.h"
#include "stdio.h"

token *token_new(uint8_t type, char *literal, int line) {
  token *t = malloc(sizeof(token));
  t->type = type;
  t->literal = literal;
  t->line = line;

#ifdef DEBUG_SCANNER
  debug_scanner(t);
#endif
  return t;
}
