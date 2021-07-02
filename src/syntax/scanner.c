#include "token.h"
#include "scanner.h"

char *source_code;
char *current; // 前推支持
int length;

typedef struct {
  char *current;
  char *guard;
  int length;
} char_cursor;

char_cursor c;

/**
 * 符号表使用什么结构来存储, 链表结构
 * @param chars
 */
token *scanner(char *source) {
  while (true) {
    skip_space();

    if (is_alpha()) {
      char *word = ident_advance();
    }

    if (is_number(*current)) {
      char *word = number_advance();
    }

    // 特殊字符识别

    // token push
    break;
  }
}

