#include "token.h"

token *token_new() {
  token *t = malloc(sizeof(token));
  return t;
}
