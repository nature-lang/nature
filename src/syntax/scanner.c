#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "scanner.h"
#include "stdio.h"
#include "src/lib/error.h"

typedef struct {
  char *source;
  char *current;
  char *guard;
  int length;
  int line; // 当前所在代码行，用于代码报错提示
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
list *scanner(string source) {
  // init scanner
  init_scanner_cursor(source);
  init_scanner_error();

  list *list = list_new();

  while (true) {
    skip_space();

    // reset by guard
    cursor.current = cursor.guard;
    cursor.length = 0;

    token *t = token_new();
    if (is_alpha(*cursor.current)) {
      char *word = ident_advance();
      t->literal = word;
      // 判断是否为关键字, 标识符是不允许太长，超过 128 个字符的
      t->type = ident_type(word, cursor.length);

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
      char *str = string_advance(*cursor.current);
      t->literal = str;
      t->type = TOKEN_LITERAL_STRING;
      list_push(list, t);
      continue;
    }

    // if current is 特殊字符
    int8_t special_type = special_char_type();
    if (special_type == -1) {
      error.message = "special_char_type() not match";
    }
    list_push(list, t);

    // if is end or error
    if (is_error()) {
      error_exit(0, error.message);
    }

    if (is_at_end()) {
      break;
    }
  }

  return list;
}

char *ident_advance() {
  // guard = current, 向前推进 guard,并累加 length
  while (is_alpha(*cursor.guard) && !is_at_end()) {
    guard_advance();
  }

  return scanner_gen_word();
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
    case '&': return match('&') ? TOKEN_AND_AND : TOKEN_AND;
    case '|': return match('|') ? TOKEN_OR_OR : TOKEN_OR;
    default: return -1;
  }
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

void init_scanner_cursor(char *source) {
  cursor.source = source;
  cursor.current = source;
  cursor.guard = source;
  cursor.length = 0;
  cursor.line = 0;
}

void init_scanner_error() {
  error.message = "";
  error.has = false;
}

void skip_space() {
  while (true) {
    char c = *cursor.guard;
    switch (c) {
      case ' ':
      case '\r':
      case '\t': {
        guard_advance();
        break;
      }
      case '\n': {
        guard_advance();
        cursor.line++;
        break;
      }
      case '/':
        if (cursor.guard[1] == '/' && !is_at_end()) {
          // 注释处理
          while (*cursor.guard != '\n' && !is_at_end()) {
            guard_advance();
          }
          break;
        } else {
          return;
        }
      default: return;
    }
  }
}

bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool is_number(char c) {
  return c >= '0' && c <= '9';
}
bool is_at_end() {
  return *cursor.guard == '\0';
}

char guard_advance() {
  cursor.guard++;
  cursor.length++;
  return cursor.guard[-1]; // [] 访问的为值
}

bool is_string(char s) {
  return s == '"' || s == '`' || s == '\'';
}

bool is_float(char *word) {
  // 是否包含 .,包含则为 float
  int dot_count = 0;

  if (*word == '.') {
    error.message = "float literal format error";
    return false;
  }

  while (*word != '\0') {
    printf("%c", *word);
    if (*word == '.') {
      dot_count++;
    }

    word++;
  }

  if (word[-1] == '.') {
    error.message = "float literal format error";
    return false;
  }

  if (dot_count == 0) {
    return false;
  }

  if (dot_count > 1) {
    error.message = "float literal format error";
    return false;
  }

  return true;
}

// 需要考虑到浮点数
char *number_advance() {
  // guard = current, 向前推进 guard,并累加 length
  while ((is_number(*cursor.guard) || *cursor.guard == '.') && !is_at_end()) {
    guard_advance();
  }

  return scanner_gen_word();
}

int8_t ident_type(char *word, int length) {
  switch (word[0]) {
    case 'a': return rest_ident_type(word, length, 1, 1, "s", TOKEN_AS);
    case 'b': return rest_ident_type(word, length, 1, 3, "ool", TOKEN_BOOL);
    case 'e': return rest_ident_type(word, length, 1, 3, "lse", TOKEN_ELSE);
    case 'f': {
      if (length == 1) {
        return TOKEN_FUNCTION;
      }

      if (length > 2) {
        switch (word[1]) {
          case 'a': return rest_ident_type(word, length, 2, 3, "lse", TOKEN_FALSE);
          case 'l': return rest_ident_type(word, length, 2, 3, "oat", TOKEN_FLOAT);
          case 'o': return rest_ident_type(word, length, 2, 1, "r", TOKEN_FOR);
        }
      }
    }
    case 'i': {
      if (length == 2) {
        if (word[1] == 'n') {
          return TOKEN_IN;
        }

        if (word[1] == 'f') {
          return TOKEN_IF;
        }
      }
      if (length > 1) {
        switch (word[1]) {
          case 'n': return rest_ident_type(word, length, 2, 1, "t", TOKEN_INT);
          case 'm': return rest_ident_type(word, length, 2, 4, "port", TOKEN_IMPORT);
        }
      }
    }
    case 'l': return rest_ident_type(word, length, 1, 3, "ist", TOKEN_LIST);
    case 'm': return rest_ident_type(word, length, 1, 2, "ap", TOKEN_MAP);
    case 's': return rest_ident_type(word, length, 1, 5, "tring", TOKEN_STRING);
    case 't': return rest_ident_type(word, length, 1, 3, "ure", TOKEN_TRUE);
    case 'v': return rest_ident_type(word, length, 1, 2, "ar", TOKEN_VAR);
    case 'w': return rest_ident_type(word, length, 1, 4, "hile", TOKEN_WHILE);
  }

  return TOKEN_LITERAL_IDENT;
}

char *string_advance(char c) {
  // 在遇到下一个 c 之前， 如果中间遇到了空格则忽略
  cursor.guard++;
  while (*cursor.guard != c && !is_at_end()) {
    guard_advance();
  }
  cursor.guard++; // 跳过结尾的 ' 或者"

  cursor.current++;
  return scanner_gen_word();
}

char *scanner_gen_word() {
  char *word = (char *) malloc(sizeof(char) * cursor.length);
  strncpy(word, cursor.current, cursor.length);

  return word;
}

int8_t rest_ident_type(char *word, int word_length, int8_t rest_start, int8_t rest_length, char *rest, int8_t type) {
  if (rest_start + rest_length == word_length &&
      memcmp(word + rest_start, rest, rest_length) == 0) {
    return type;
  }
  return TOKEN_LITERAL_IDENT;
}
