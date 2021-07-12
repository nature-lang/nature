#ifndef NATURE_SRC_SCANNER_H_
#define NATURE_SRC_SCANNER_H_

#include "src/lib/list.h"
#include "src/value.h"

typedef struct {
  char *source;
  char *current;
  char *guard;
  int length;
  int line; // 当前所在代码行，用于代码报错提示

  bool has_entry;
  char space_last;
  char space_next;
} scanner_cursor;

typedef struct {
  bool has;
  char *message;
} scanner_error;

list *scanner(string source);

void scanner_cursor_init(string source);
void scanner_error_init();

void scanner_skip_space();
bool scanner_is_alpha(char c);
bool scanner_is_string(char s); // '/"/`
bool scanner_is_number(char c);
bool scanner_is_float(char *word);
char *scanner_ident_advance();
char *scanner_number_advance();
char *scanner_string_advance(char c);
token_type scanner_ident_type(char *word, int length);
token_type scanner_rest_ident_type(char *word,
                                   int word_length,
                                   int8_t rest_start,
                                   int8_t rest_length,
                                   char *rest,
                                   int8_t type);
token_type scanner_special_char_type();
char scanner_guard_advance(); // guard 前进一个字符
char *scanner_gen_word();

bool scanner_is_at_end(); // guard 是否遇见了 '\0'
bool scanner_has_error();
bool scanner_match(char expected);
bool scanner_is_space(char c);
bool scanner_is_at_stmt_end();

#endif //NATURE_SRC_SCANNER_H_
