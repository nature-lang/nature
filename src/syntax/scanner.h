#ifndef NATURE_SRC_SCANNER_H_
#define NATURE_SRC_SCANNER_H_

#include "utils/list.h"
#include "src/value.h"
#include "src/syntax/token.h"
#include "src/module.h"

list *scanner(module_t *module);

void scanner_cursor_init(module_t *module);

void scanner_error_init(module_t *module);

void scanner_skip_space(module_t *module);

bool scanner_is_alpha(module_t *module, char c);

bool scanner_is_string(module_t *module, char s); // '/"/`
bool scanner_is_number(module_t *module, char c);

bool scanner_is_float(module_t *module, char *word);

char *scanner_ident_advance(module_t *module);

char *scanner_number_advance(module_t *module);

char *scanner_string_advance(module_t *module, char c);

token_type scanner_ident_type(char *word, int length);

token_type scanner_rest_ident_type(char *word,
                                   int word_length,
                                   int8_t rest_start,
                                   int8_t rest_length,
                                   char *rest,
                                   int8_t type);

token_type scanner_special_char(module_t *m);

char scanner_guard_advance(module_t *module); // guard 前进一个字符
char *scanner_gen_word(module_t *module);

bool scanner_is_at_end(module_t *module); // guard 是否遇见了 '\0'
bool scanner_has_error(module_t *module);

bool scanner_match(module_t *module, char expected);

bool scanner_is_space(char c);

bool scanner_is_at_stmt_end(module_t *module);

#endif //NATURE_SRC_SCANNER_H_
