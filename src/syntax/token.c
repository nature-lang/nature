#include "token.h"

token *token_new(uint8_t type, char *literal) {
  token *t = malloc(sizeof(token));
  t->type = type;
  t->literal = literal;
  return t;
}
