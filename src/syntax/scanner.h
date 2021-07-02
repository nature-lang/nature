#ifndef NATURE_SRC_SCANNER_H_
#define NATURE_SRC_SCANNER_H_

#include "src/value.h"

token *scanner(string chars);
char *skip_space();
bool is_alpha(char c);
bool is_number(char c);
char *ident_advance();
char *number_advance();

#endif //NATURE_SRC_SCANNER_H_
