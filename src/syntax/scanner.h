#ifndef NATURE_SRC_SCANNER_H_
#define NATURE_SRC_SCANNER_H_

#include "src/value.h"

token *scanner(string chars);
char *skip_space();
bool is_alpha(char c);
bool is_string(char s); // '/"/`
bool is_number(char c);
bool is_float(char *word);
char *ident_advance();
char *number_advance();
char *string_advance();
int8_t ident_type(char *word);
int8_t special_char_type();
char guard_advance(); // guard 前进一个字符

bool is_at_end(); // guard 是否遇见了 '\0'
bool is_error();
bool match(char expected);

#endif //NATURE_SRC_SCANNER_H_
