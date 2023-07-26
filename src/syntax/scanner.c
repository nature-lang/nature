#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "scanner.h"
#include "utils/error.h"

/**
 * 符号表使用什么结构来存储, 链表结构
 * @param chars
 */
linked_t *scanner(module_t *m) {
    // init scanner
    scanner_cursor_init(m);
    scanner_error_init(m);

    linked_t *list = linked_new();

    while (true) {
        scanner_skip_space(m);

        if (m->s_cursor.has_newline && scanner_is_at_stmt_end(m)) {
            // push token_t TOKEN_STMT_EOF
            linked_push(list, token_new(TOKEN_STMT_EOF, ";", m->s_cursor.line - 1));
        }

        // reset by guard
        m->s_cursor.current = m->s_cursor.guard;
        m->s_cursor.length = 0;

        if (scanner_is_alpha(m, *m->s_cursor.current)) {
            char *word = scanner_ident_advance(m);

            token_t *t = token_new(scanner_ident(word, m->s_cursor.length), word, m->s_cursor.line);
            linked_push(list, t);
            continue;
        }

        // 首个字符是 0 ~ 9 则判定为数字
        if (scanner_is_number(m, *m->s_cursor.current)) {
            scanner_guard_advance(m);

            // 16 进制字符串
            char *word = NULL;
            if (*m->s_cursor.guard == 'x' || *m->s_cursor.guard == 'X') {
                word = scanner_hex_number_advance(m);
            } else {
                word = scanner_number_advance(m); // 1, 1.12, 0.233
            }

            uint8_t type;
            if (scanner_is_float(m, word)) {
                type = TOKEN_LITERAL_FLOAT;
            } else {
                type = TOKEN_LITERAL_INT;
            }

            linked_push(list, token_new(type, word, m->s_cursor.line));
            continue;
        }

        // 字符串扫描
        if (scanner_is_string(m, *m->s_cursor.current)) {
            char *str = scanner_string_advance(m, *m->s_cursor.current);
            linked_push(list, token_new(TOKEN_LITERAL_STRING, str, m->s_cursor.line));
            continue;
        }

        // if current is 特殊字符
        if (!scanner_is_at_end(m)) {
            int8_t special_type = scanner_special_char(m);
            if (special_type == -1) { // 未识别的特殊符号
                m->s_error.message = "scanner_special_char() not scanner_match";
            } else {
                linked_push(list, token_new(special_type, scanner_gen_word(m), m->s_cursor.line));
                continue;
            }
        }


        // if is end or error
        if (scanner_has_error(m)) {
            error_exit(m->s_error.message);
        }

        if (scanner_is_at_end(m)) {
            break;
        }
    }

    linked_push(list, token_new(TOKEN_EOF, "EOF", m->s_cursor.line));

    return list;
}

char *scanner_ident_advance(module_t *m) {
    // guard = current, 向前推进 guard,并累加 length
    while ((scanner_is_alpha(m, *m->s_cursor.guard) ||
            scanner_is_number(m, *m->s_cursor.guard))
           && !scanner_is_at_end(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

token_e scanner_special_char(module_t *m) {
    char c = scanner_guard_advance(m);

    switch (c) {
        case '(':
            return TOKEN_LEFT_PAREN;
        case ')':
            return TOKEN_RIGHT_PAREN;
        case '[':
            return TOKEN_LEFT_SQUARE;
        case ']':
            return TOKEN_RIGHT_SQUARE;
        case '{':
            return TOKEN_LEFT_CURLY;
        case '}':
            return TOKEN_RIGHT_CURLY;
        case ':':
            return TOKEN_COLON;
        case ';':
            return TOKEN_SEMICOLON;
        case ',':
            return TOKEN_COMMA;
        case '%':
            return scanner_match(m, '=') ? TOKEN_PERSON_EQUAL : TOKEN_PERSON;
        case '-':
            return scanner_match(m, '=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS;
        case '+':
            return scanner_match(m, '=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS;
        case '/':
            return scanner_match(m, '=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH;
        case '*':
            return scanner_match(m, '=') ? TOKEN_STAR_EQUAL : TOKEN_STAR;
        case '.': {
            if (scanner_match(m, '.')) {
                // 下一个值必须也要是点，否则就报错
                if (scanner_match(m, '.')) {
                    return TOKEN_ELLIPSIS;
                }
                // 以及吃掉了 2 个点了，没有回头路
                return -1;
            }
            return TOKEN_DOT;
        }
        case '!':
            return scanner_match(m, '=') ? TOKEN_NOT_EQUAL : TOKEN_NOT;
        case '=':
            return scanner_match(m, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL;
        case '<':
            if (scanner_match(m, '<')) { // <<
                if (scanner_match(m, '=')) { // <<=
                    return TOKEN_LEFT_SHIFT_EQUAL;
                }
                // <<
                return TOKEN_LEFT_SHIFT;
            } else if (scanner_match(m, '=')) {
                return TOKEN_LESS_EQUAL;
            }
            return TOKEN_LEFT_ANGLE;
        case '>':
            if (scanner_match(m, '>')) {
                if (scanner_match(m, '=')) { // >>=
                    return TOKEN_RIGHT_SHIFT_EQUAL;
                }
                return TOKEN_RIGHT_SHIFT; // >>
            } else if (scanner_match(m, '=')) { // >=
                return TOKEN_GREATER_EQUAL;
            }
            return TOKEN_RIGHT_ANGLE; // >
        case '&':
            return scanner_match(m, '&') ? TOKEN_AND_AND : TOKEN_AND;
        case '|':
            return scanner_match(m, '|') ? TOKEN_OR_OR : TOKEN_OR;
        case '~':
            return TOKEN_TILDE;
        case '^':
            return scanner_match(m, '=') ? TOKEN_XOR_EQUAL : TOKEN_XOR;
        default:
            return -1;
    }
}

bool scanner_match(module_t *m, char expected) {
    if (scanner_is_at_end(m)) {
        return false;
    }

    if (*m->s_cursor.guard != expected) {
        return false;
    }

    scanner_guard_advance(m);
    return true;
}

bool scanner_has_error(module_t *m) {
    return m->s_error.has;
}

void scanner_cursor_init(module_t *m) {
    m->s_cursor.source = m->source;
    m->s_cursor.current = m->source;
    m->s_cursor.guard = m->source;
    m->s_cursor.length = 0;
    m->s_cursor.line = 1;
    m->s_cursor.has_newline = false;
    m->s_cursor.space_prev = STRING_EOF;
    m->s_cursor.space_next = STRING_EOF;
}

void scanner_error_init(module_t *m) {
    m->s_error.message = "";
    m->s_error.has = false;
}

/**
 * 在没有 ; 号的情况下，换行符在大多数时候承担着判断是否需要添加 TOKEN_EOF
 */
void scanner_skip_space(module_t *m) {
    m->s_cursor.has_newline = false;
    if (m->s_cursor.guard != m->s_cursor.current) {
        m->s_cursor.space_prev = m->s_cursor.guard[-1];
    }

    while (true) {
        char c = *m->s_cursor.guard;
        switch (c) {
            case ' ':
            case '\r':
            case '\t': {
                scanner_guard_advance(m);
                break;
            }
            case '\n': {
                scanner_guard_advance(m);
                m->s_cursor.line++;
                m->s_cursor.has_newline = true;
                break;
            }
            case '/':
                if (m->s_cursor.guard[1] == '/' && !scanner_is_at_end(m)) {
                    // 注释处理
                    while (*m->s_cursor.guard != '\n' && !scanner_is_at_end(m)) {
                        scanner_guard_advance(m);
                    }
                    break;
                } else {
                    m->s_cursor.space_next = *m->s_cursor.guard;
                    return;
                }
            default: {
                m->s_cursor.space_next = *m->s_cursor.guard;
                return;
            }
        }
    }
}

bool scanner_is_alpha(module_t *m, char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool scanner_is_number(module_t *m, char c) {
    return c >= '0' && c <= '9';
}

bool scanner_is_hex_number(module_t *m, char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

bool scanner_is_at_end(module_t *m) {
    return *m->s_cursor.guard == '\0';
}

char scanner_guard_advance(module_t *m) {
    m->s_cursor.guard++;
    m->s_cursor.length++;
    return m->s_cursor.guard[-1]; // [] 访问的为值
}

bool scanner_is_string(module_t *m, char s) {
    return s == '"' || s == '`' || s == '\'';
}

bool scanner_is_float(module_t *m, char *word) {
    // 是否包含 .,包含则为 float
    int dot_count = 0;

    if (*word == '.') {
        m->s_error.message = "float literal format error";
        return false;
    }

    while (*word != '\0') {
        if (*word == '.') {
            dot_count++;
        }

        word++;
    }

    if (word[-1] == '.') {
        m->s_error.message = "float literal format error";
        return false;
    }

    if (dot_count == 0) {
        return false;
    }

    if (dot_count > 1) {
        m->s_error.message = "float literal format error";
        return false;
    }

    return true;
}

char *scanner_hex_number_advance(module_t *m) {
    scanner_guard_advance(m); // x or X

    // guard = current, 向前推进 guard,并累加 length
    while (scanner_is_hex_number(m, *m->s_cursor.guard) && !scanner_is_at_end(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

// 需要考虑到浮点数
char *scanner_number_advance(module_t *m) {
    // guard = current, 向前推进 guard,并累加 length
    while ((scanner_is_number(m, *m->s_cursor.guard) || *m->s_cursor.guard == '.') &&
           !scanner_is_at_end(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

token_e scanner_ident(char *word, int length) {
    switch (word[0]) {
        case 'a': {
            if (length == 2 && word[1] == 's') {
                return TOKEN_AS;
            }
            if (length > 1) {
                switch (word[1]) {
                    case 'n': {
                        return scanner_rest(word, length, 1, 2, "ny", TOKEN_ANY);
                    }
                    case 'r': {
                        return scanner_rest(word, length, 1, 3, "ray", TOKEN_ANY);
                    }
                }
            }

        }
        case 'b':
            switch (word[1]) {
                case 'o':
                    return scanner_rest(word, length, 2, 2, "ol", TOKEN_BOOL);
                case 'r':
                    return scanner_rest(word, length, 2, 3, "eak", TOKEN_BREAK);
            }
        case 'c':
            switch (word[1]) {
                case 'o':
                    return scanner_rest(word, length, 2, 6, "ntinue", TOKEN_CONTINUE);
                case 'a':
                    return scanner_rest(word, length, 2, 3, "tch", TOKEN_CATCH);
            }
        case 'e':
            return scanner_rest(word, length, 1, 3, "lse", TOKEN_ELSE);
        case 'f': {
            switch (word[1]) {
                case 'n':
                    return scanner_rest(word, length, 2, 0, "n", TOKEN_FN);
                case 'a':
                    return scanner_rest(word, length, 2, 3, "lse", TOKEN_FALSE);
                case 'l':
                    return scanner_rest(word, length, 2, 3, "oat", TOKEN_FLOAT);
                case '3':
                    return scanner_rest(word, length, 2, 1, "2", TOKEN_F32);
                case '6':
                    return scanner_rest(word, length, 2, 1, "4", TOKEN_F64);
                case 'o':
                    return scanner_rest(word, length, 2, 1, "r", TOKEN_FOR);
            }
        }
        case 'g':
            return scanner_rest(word, length, 1, 2, "en", TOKEN_GEN);
        case 'i': {
            if (length == 2 && word[1] == 'n') {
                return TOKEN_IN;
            }
            if (length == 2 && word[1] == 's') {
                return TOKEN_IS;
            }

            switch (word[1]) {
                case 'm':
                    return scanner_rest(word, length, 2, 4, "port", TOKEN_IMPORT);
                case 'f':
                    return scanner_rest(word, length, 2, 0, "", TOKEN_IF);
                case 'n':
                    return scanner_rest(word, length, 2, 1, "t", TOKEN_INT);
                case '8':
                    return scanner_rest(word, length, 2, 0, "", TOKEN_I8);
                case '1':
                    return scanner_rest(word, length, 2, 1, "6", TOKEN_I16);
                case '3':
                    return scanner_rest(word, length, 2, 1, "2", TOKEN_I32);
                case '6':
                    return scanner_rest(word, length, 2, 1, "4", TOKEN_I64);
            }
        }
        case 'l': {
            return scanner_rest(word, length, 1, 2, "et", TOKEN_LET);
        }
        case 'n':
            return scanner_rest(word, length, 1, 3, "ull", TOKEN_NULL);
        case 'p':
            return scanner_rest(word, length, 1, 2, "tr", TOKEN_POINTER);
        case 's': { // self,string,struct
            if (word[1] == 'e') {
                return scanner_rest(word, length, 2, 2, "lf", TOKEN_SELF);
            }
            if (length == 6 && word[1] == 't' && word[2] == 'r') {
                switch (word[3]) {
                    case 'i':
                        return scanner_rest(word, length, 4, 2, "ng", TOKEN_STRING);
                    case 'u':
                        return scanner_rest(word, length, 4, 2, "ct", TOKEN_STRUCT);
                }
            }
        }
            // tup/throw/type/true
        case 't': {
            if (length > 1) {
                switch (word[1]) {
                    case 'h':
                        return scanner_rest(word, length, 2, 3, "row", TOKEN_THROW);
//                    case 'u':
//                        return scanner_rest(word, length, 2, 1, "p", TOKEN_TUPLE);
                    case 'y' :
                        return scanner_rest(word, length, 2, 2, "pe", TOKEN_TYPE);
                    case 'r' : {
                        if (length == 3 && word[2] == 'y') {
                            return TOKEN_TRY;
                        }
                        return scanner_rest(word, length, 2, 2, "ue", TOKEN_TRUE);
                    }
                }
            }
        }
        case 'v': {
            return scanner_rest(word, length, 1, 2, "ar", TOKEN_VAR);
        }
        case 'u': {
            switch (word[1]) {
                case 'i':
                    return scanner_rest(word, length, 2, 2, "nt", TOKEN_UINT);
                case '8':
                    return scanner_rest(word, length, 2, 0, "", TOKEN_U8);
                case '1':
                    return scanner_rest(word, length, 2, 1, "6", TOKEN_U16);
                case '3':
                    return scanner_rest(word, length, 2, 1, "2", TOKEN_U32);
                case '6':
                    return scanner_rest(word, length, 2, 1, "4", TOKEN_U64);
            }
        }
        case 'r': {
            return scanner_rest(word, length, 1, 5, "eturn", TOKEN_RETURN);
        }
        case (char) 0xF0: { // temp use 💥
            return scanner_rest(word, length, 1, 3, "\x9F\x92\xA5", TOKEN_BOOM);
        }
    }

    return TOKEN_IDENT;
}

char *scanner_string_advance(module_t *m, char c) {
    // 在遇到下一个 c 之前， 如果中间遇到了空格则忽略
    m->s_cursor.guard++;
    while (*m->s_cursor.guard != c && !scanner_is_at_end(m)) {
        scanner_guard_advance(m);
    }
    m->s_cursor.guard++; // 跳过结尾的 ' 或者"

    m->s_cursor.current++;
    return scanner_gen_word(m);
}

char *scanner_gen_word(module_t *m) {
    char *word = malloc(sizeof(char) * m->s_cursor.length + 1);
    strncpy(word, m->s_cursor.current, m->s_cursor.length);
    word[m->s_cursor.length] = '\0';

    return word;
}

token_e scanner_rest(char *word,
                     int word_length,
                     int8_t rest_start,
                     int8_t rest_length,
                     char *rest,
                     int8_t type) {
    if (rest_start + rest_length == word_length &&
        memcmp(word + rest_start, rest, rest_length) == 0) {
        return type;
    }
    return TOKEN_IDENT;
}

/**
 * last not ,、[、=、{
 * next not {
 * @return
 */
bool scanner_is_at_stmt_end(module_t *m) {
    if (scanner_is_space(m->s_cursor.space_prev)
        || m->s_cursor.space_prev == '\\'
        || m->s_cursor.space_prev == STRING_EOF) {
        return false;
    }

    if (m->s_cursor.space_next == '}') {
        return false;
    }

    if (m->s_cursor.space_next == ']') {
        return false;
    }

    if (m->s_cursor.space_next == ')') {
        return false;
    }

    // 前置非空白字符
    if (m->s_cursor.space_prev != ','
        && m->s_cursor.space_prev != '('
        && m->s_cursor.space_prev != '['
        && m->s_cursor.space_prev != '='
        && m->s_cursor.space_prev != '{'
//           && m->s_cursor.space_next != '{'
            ) {
        return true;
    }

    return false;
}

bool scanner_is_space(char c) {
    if (c == '\n' || c == '\t' || c == '\r' || c == ' ') {
        return true;
    }
    return false;
}
