#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "scanner.h"
#include "stdio.h"
#include "src/lib/error.h"

scanner_cursor s_cursor;
scanner_error s_error;

/**
 * 符号表使用什么结构来存储, 链表结构
 * @param chars
 */
list *scanner(string source) {
  // init scanner
  scanner_cursor_init(source);
  scanner_error_init();

  list *list = list_new();

  while (true) {
    scanner_skip_space();

    // reset by guard
    s_cursor.current = s_cursor.guard;
    s_cursor.length = 0;

    if (scanner_is_alpha(*s_cursor.current)) {
      char *word = scanner_ident_advance();

      token *t = token_new(scanner_ident_type(word, s_cursor.length), word);
      list_push(list, t);
      continue;
    }

    if (scanner_is_number(*s_cursor.current)) {
      char *word = scanner_number_advance(); // 1, 1.12, 0.233
      uint8_t type;
      if (scanner_is_float(word)) {
        type = TOKEN_LITERAL_FLOAT;
      } else {
        type = TOKEN_LITERAL_INT;
      }

      list_push(list, token_new(type, word));
      continue;
    }

    if (scanner_is_string(*s_cursor.current)) {
      char *str = scanner_string_advance(*s_cursor.current);
      list_push(list, token_new(TOKEN_LITERAL_STRING, str));
      continue;
    }

    // if current is 特殊字符
    if (!scanner_is_at_end()) {
      int8_t special_type = scanner_special_char_type();
      if (special_type == -1) { // 未识别的特殊符号
        s_error.message = "scanner_special_char_type() not scanner_match";
      } else {
        list_push(list, token_new(special_type, scanner_gen_word()));
        continue;
      }
    }


    // if is end or error
    if (scanner_has_error()) {
      error_exit(0, s_error.message);
    }

    if (scanner_is_at_end()) {
      break;
    }
  }

  list_push(list, token_new(TOKEN_EOF, "EOF"));

  return list;
}

char *scanner_ident_advance() {
  // guard = current, 向前推进 guard,并累加 length
  while (scanner_is_alpha(*s_cursor.guard) && !scanner_is_at_end()) {
    scanner_guard_advance();
  }

  return scanner_gen_word();
}

int8_t scanner_special_char_type() {
  char c = scanner_guard_advance();

  switch (c) {
    case '(': return TOKEN_LEFT_PAREN;
    case ')': return TOKEN_RIGHT_PAREN;
    case '[': return TOKEN_LEFT_SQUARE;
    case ']': return TOKEN_RIGHT_SQUARE;
    case '{': return TOKEN_LEFT_CURLY;
    case '}': return TOKEN_RIGHT_CURLY;
    case ';': return TOKEN_SEMICOLON;
    case ',': return TOKEN_COMMA;
    case '.': return TOKEN_DOT;
    case '-': return TOKEN_MINUS;
    case '+': return TOKEN_PLUS;
    case '/': return TOKEN_SLASH;
    case '*': return TOKEN_STAR;
    case '!': return scanner_match('=') ? TOKEN_NOT_EQUAL : TOKEN_NOT;
    case '=': return scanner_match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL;
    case '<': return scanner_match('=') ? TOKEN_LESS_EQUAL : TOKEN_LEFT_ANGLE;
    case '>': return scanner_match('=') ? TOKEN_GREATER_EQUAL : TOKEN_RIGHT_ANGLE;
    case '&': return scanner_match('&') ? TOKEN_AND_AND : TOKEN_AND;
    case '|': return scanner_match('|') ? TOKEN_OR_OR : TOKEN_OR;
    default: return -1;
  }
}
bool scanner_match(char expected) {
  if (scanner_is_at_end()) {
    return false;
  }

  if (*s_cursor.guard != expected) {
    return false;
  }

  scanner_guard_advance();
  return true;
}

bool scanner_has_error() {
  return s_error.has;
}

void scanner_cursor_init(char *source) {
  s_cursor.source = source;
  s_cursor.current = source;
  s_cursor.guard = source;
  s_cursor.length = 0;
  s_cursor.line = 0;
}

void scanner_error_init() {
  s_error.message = "";
  s_error.has = false;
}

void scanner_skip_space() {
  while (true) {
    char c = *s_cursor.guard;
    switch (c) {
      case ' ':
      case '\r':
      case '\t': {
        scanner_guard_advance();
        break;
      }
      case '\n': {
        scanner_guard_advance();
        s_cursor.line++;
        break;
      }
      case '/':
        if (s_cursor.guard[1] == '/' && !scanner_is_at_end()) {
          // 注释处理
          while (*s_cursor.guard != '\n' && !scanner_is_at_end()) {
            scanner_guard_advance();
          }
          break;
        } else {
          return;
        }
      default: return;
    }
  }
}

bool scanner_is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool scanner_is_number(char c) {
  return c >= '0' && c <= '9';
}
bool scanner_is_at_end() {
  return *s_cursor.guard == '\0';
}

char scanner_guard_advance() {
  s_cursor.guard++;
  s_cursor.length++;
  return s_cursor.guard[-1]; // [] 访问的为值
}

bool scanner_is_string(char s) {
  return s == '"' || s == '`' || s == '\'';
}

bool scanner_is_float(char *word) {
  // 是否包含 .,包含则为 float
  int dot_count = 0;

  if (*word == '.') {
    s_error.message = "float literal format error";
    return false;
  }

  while (*word != '\0') {
    if (*word == '.') {
      dot_count++;
    }

    word++;
  }

  if (word[-1] == '.') {
    s_error.message = "float literal format error";
    return false;
  }

  if (dot_count == 0) {
    return false;
  }

  if (dot_count > 1) {
    s_error.message = "float literal format error";
    return false;
  }

  return true;
}

// 需要考虑到浮点数
char *scanner_number_advance() {
  // guard = current, 向前推进 guard,并累加 length
  while ((scanner_is_number(*s_cursor.guard) || *s_cursor.guard == '.') && !scanner_is_at_end()) {
    scanner_guard_advance();
  }

  return scanner_gen_word();
}

int8_t scanner_ident_type(char *word, int length) {
  switch (word[0]) {
    case 'a': return scanner_rest_ident_type(word, length, 1, 1, "s", TOKEN_AS);
    case 'b': return scanner_rest_ident_type(word, length, 1, 3, "ool", TOKEN_BOOL);
    case 'e': return scanner_rest_ident_type(word, length, 1, 3, "lse", TOKEN_ELSE);
    case 'f': {
      if (length == 1) {
        return TOKEN_FUNCTION;
      }

      if (length > 2) {
        switch (word[1]) {
          case 'a': return scanner_rest_ident_type(word, length, 2, 3, "lse", TOKEN_FALSE);
          case 'l': return scanner_rest_ident_type(word, length, 2, 3, "oat", TOKEN_FLOAT);
          case 'o': return scanner_rest_ident_type(word, length, 2, 1, "r", TOKEN_FOR);
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
          case 'n': return scanner_rest_ident_type(word, length, 2, 1, "t", TOKEN_INT);
          case 'm': return scanner_rest_ident_type(word, length, 2, 4, "port", TOKEN_IMPORT);
        }
      }
    }
    case 'l': return scanner_rest_ident_type(word, length, 1, 3, "ist", TOKEN_LIST);
    case 'm': return scanner_rest_ident_type(word, length, 1, 2, "ap", TOKEN_MAP);
    case 's': return scanner_rest_ident_type(word, length, 1, 5, "tring", TOKEN_STRING);
    case 't': return scanner_rest_ident_type(word, length, 1, 3, "ure", TOKEN_TRUE);
    case 'v': return scanner_rest_ident_type(word, length, 1, 2, "ar", TOKEN_VAR);
    case 'w': return scanner_rest_ident_type(word, length, 1, 4, "hile", TOKEN_WHILE);
    case 'r': return scanner_rest_ident_type(word, length, 1, 5, "eturn", TOKEN_RETURN);
  }

  return TOKEN_LITERAL_IDENT;
}

char *scanner_string_advance(char c) {
  // 在遇到下一个 c 之前， 如果中间遇到了空格则忽略
  s_cursor.guard++;
  while (*s_cursor.guard != c && !scanner_is_at_end()) {
    scanner_guard_advance();
  }
  s_cursor.guard++; // 跳过结尾的 ' 或者"

  s_cursor.current++;
  return scanner_gen_word();
}

char *scanner_gen_word() {
  char *word = (char *) malloc(sizeof(char) * s_cursor.length);
  strncpy(word, s_cursor.current, s_cursor.length);

  return word;
}

int8_t scanner_rest_ident_type(char *word,
                               int word_length,
                               int8_t rest_start,
                               int8_t rest_length,
                               char *rest,
                               int8_t type) {
  if (rest_start + rest_length == word_length &&
      memcmp(word + rest_start, rest, rest_length) == 0) {
    return type;
  }
  return TOKEN_LITERAL_IDENT;
}
