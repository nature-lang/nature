#include "token.h"
#include "scanner.h"
#include "stdlib.h"
#include "string.h"
#include "src/lib/list.h"

typedef struct {
  char *current;
  char *guard;
  int length;
} scanner_cursor;

typedef struct {
  bool has;
  char *message;
} scanner_error;

scanner_cursor cursor;
scanner_error error;

/**
 * 符号表使用什么结构来存储, 链表结构
 * @param chars
 */
token *scanner(char *source) {

  list *list = list_new();

  while (true) {
    skip_space();

    cursor.current = cursor.guard;
    cursor.length = 0;

    token *t = token_new();
    if (is_alpha(*cursor.current)) {
      char *word = ident_advance();
      t->literal = word;
      // 判断是否为关键字
      t->type = ident_type(word);

      list_push(list, t);
      continue;
    }

    if (is_number(*cursor.current)) {
      char *word = number_advance(); // 1, 1.12, 0.233
      t->literal = word;
      if (is_float(word)) {
        t->type = TOKEN_LITERAL_FLOAT;
      } else {
        t->type = TOKEN_LITERAL_INT;
      }

      list_push(list, t);
      continue;
    }

    if (is_string(*cursor.current)) {
      char *str = string_advance();
      t->literal = str;
      t->type = TOKEN_LITERAL_STRING;
    }
    // if current is 特殊字符
    int8_t special_type = special_char_type();
    if (special_type == -1) {
      // TODO error
      break;
    }

    list_push(list, t);

    // if is end or error
    break;
  }
}

char *ident_advance() {
  // guard = current, 向前推进 guard,并累加 length
  while (is_alpha(*cursor.guard)) {
    guard_advance();
  }

  char *word = (char *) malloc(sizeof(char) * cursor.length);

  // 字符串 copy 赋值
  strncpy(word, cursor.current, cursor.length);

  return word;
}

int8_t special_char_type() {
  char c = guard_advance();

  switch (c) {
    case '(': return TOKEN_LEFT_PAREN;
    case ')': return TOKEN_RIGHT_PAREN;
    case '[': return TOKEN_LEFT_SQUARE;
    case ']': return TOKEN_RIGHT_SQUARE;
    case '{': return TOKEN_LEFT_CURLY;
    case '}': return TOKEN_RIGHT_PAREN;
//    case '<': return TOKEN_LEFT_ANGLE;
//    case '>': return TOKEN_RIGHT_ANGLE;
    case ';': return TOKEN_SEMICOLON;
    case ',': return TOKEN_COMMA;
    case '.': return TOKEN_DOT;
    case '-': return TOKEN_MINUS;
    case '+': return TOKEN_PLUS;
    case '/': return TOKEN_SLASH;
    case '*': return TOKEN_STAR;
    case '!': return match('=') ? TOKEN_NOT_EQUAL : TOKEN_NOT;
    case '=': return match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL;
    case '<': return match('=') ? TOKEN_LESS_EQUAL : TOKEN_LEFT_ANGLE;
    case '>': return match('=') ? TOKEN_GREATER_EQUAL : TOKEN_RIGHT_ANGLE;
    default: {
      // TODO set error for example @/% symbol
    }
  }

  return -1;
}
bool match(char expected) {
  if (is_at_end()) {
    return false;
  }

  if (*cursor.guard != expected) {
    return false;
  }

  guard_advance();
  return true;
}
bool is_error() {
  return error.has;
}
