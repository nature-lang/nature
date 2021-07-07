#ifndef NATURE_SRC_SCANNER_H_
#define NATURE_SRC_SCANNER_H_

#include "src/lib/list.h"
#include "src/value.h"

list *scanner(string source);

void init_scanner_cursor(string source);
void init_scanner_error();

void skip_space();
bool is_alpha(char c);
bool is_string(char s); // '/"/`
bool is_number(char c);
bool is_float(char *word);
char *ident_advance();
char *number_advance();
char *string_advance(char c);
int8_t ident_type(char *word, int8_t length);
int8_t rest_ident_type(char *word, int8_t word_length, int8_t rest_start, int8_t rest_length, char *rest, int8_t type);
int8_t special_char_type();
char guard_advance(); // guard 前进一个字符
char *scanner_gen_word();

bool is_at_end(); // guard 是否遇见了 '\0'
bool is_error();
bool match(char expected);

#endif //NATURE_SRC_SCANNER_H_
