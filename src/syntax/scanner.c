#include "scanner.h"

#include <stdlib.h>
#include <string.h>

#include "src/error.h"
#include "token.h"
#include "utils/autobuf.h"

static char *scanner_ident_advance(module_t *m);

static token_type_t scanner_ident(char *word, int length);

static bool scanner_skip_space(module_t *m);

static char *scanner_gen_word(module_t *m) {
    char *word = malloc(sizeof(char) * m->s_cursor.length + 1);
    strncpy(word, m->s_cursor.current, m->s_cursor.length);
    word[m->s_cursor.length] = '\0';

    return word;
}

static bool scanner_is_space(char c) {
    if (c == '\n' || c == '\t' || c == '\r' || c == ' ') {
        return true;
    }
    return false;
}


static bool scanner_is_string(module_t *m, char s) {
    return s == '"' || s == '`' || s == '\'';
}


/**
 * 判断 word(number 开头) 是否为 int
 * @param m
 * @param word
 * @return
 */
static bool scanner_is_float(module_t *m, char *word) {
    // 是否包含 .,包含则为 float
    int dot_count = 0;

    // 遍历 dot 数量
    while (*word != '\0') {
        if (*word == '.') {
            dot_count++;
        }

        word++;
    }

    // 结尾不能是 .
    if (word[-1] == '.') {
        dump_errorf(m, CT_STAGE_SCANNER, m->s_cursor.line, m->s_cursor.column,
                    "floating-point numbers cannot end with '.'");
        return false;
    }

    if (dot_count == 0) {
        return false;
    }

    if (dot_count > 1) {
        dump_errorf(m, CT_STAGE_SCANNER, m->s_cursor.line, m->s_cursor.column,
                    "floating-point number contains multiple '.'");
        return false;
    }

    return true;
}

static bool scanner_is_alpha(module_t *m, char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool scanner_is_number(module_t *m, char c) { return c >= '0' && c <= '9'; }

static bool scanner_is_hex_number(module_t *m, char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool scanner_is_oct_number(module_t *m, char c) {
    return c >= '0' && c <= '7';
}

static bool scanner_is_bin_number(module_t *m, char c) {
    return c == '0' || c == '1';
}

static bool scanner_at_eof(module_t *m) { return *m->s_cursor.guard == '\0'; }

/**
 * @param m
 * @return
 */
static char scanner_guard_advance(module_t *m) {
    m->s_cursor.guard++;
    m->s_cursor.length++;
    m->s_cursor.column++;

    if (m->s_cursor.guard[-1] == '\n') {
        m->s_cursor.line++;
        m->s_cursor.column = 0;
    }

    return m->s_cursor.guard[-1]; // [] 访问的为值
}

static bool scanner_match(module_t *m, char expected) {
    if (scanner_at_eof(m)) {
        return false;
    }

    if (*m->s_cursor.guard != expected) {
        return false;
    }

    scanner_guard_advance(m);
    return true;
}


static char *scanner_ident_advance(module_t *m) {
    // guard = current, 向前推进 guard,并累加 length
    while ((scanner_is_alpha(m, *m->s_cursor.guard) ||
            scanner_is_number(m, *m->s_cursor.guard)) &&
           !scanner_at_eof(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

static token_type_t scanner_special_char(module_t *m) {
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
            return TOKEN_STMT_EOF;
        case ',':
            return TOKEN_COMMA;
        case '?':
            return TOKEN_QUESTION;
        case '%':
            return scanner_match(m, '=') ? TOKEN_PERSON_EQUAL : TOKEN_PERSON;
        case '-':
            if (scanner_match(m, '=')) {
                return TOKEN_MINUS_EQUAL;
            }
            if (scanner_match(m, '>')) {
                return TOKEN_RIGHT_ARROW;
            }

            return TOKEN_MINUS;
        case '+':
            return scanner_match(m, '=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS;
        case '/':
            return scanner_match(m, '=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH;
        case '*': {
            return scanner_match(m, '=') ? TOKEN_STAR_EQUAL : TOKEN_STAR;
        }
        case '.': {
            if (scanner_match(m, '.')) {
                // 下一个值必须也要是点，否则就报错
                if (scanner_match(m, '.')) {
                    return TOKEN_ELLIPSIS;
                }
                // 以及吃掉了 2 个点了，没有回头路
                return 0;
            }
            return TOKEN_DOT;
        }
        case '!':
            return scanner_match(m, '=') ? TOKEN_NOT_EQUAL : TOKEN_NOT;
        case '=':
            return scanner_match(m, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL;
        case '<':
            if (scanner_match(m, '<')) {
                // <<
                if (scanner_match(m, '=')) {
                    // <<=
                    return TOKEN_LEFT_SHIFT_EQUAL;
                }
                // <<
                return TOKEN_LEFT_SHIFT;
            } else if (scanner_match(m, '=')) {
                return TOKEN_LESS_EQUAL;
            }
            return TOKEN_LEFT_ANGLE;
        case '>': {
            char *p = m->s_cursor.guard;
            if (p[0] == '=') {
                scanner_guard_advance(m);
                return TOKEN_GREATER_EQUAL;
            }

            if (p[0] == '>' && p[1] == '=') {
                scanner_guard_advance(m);
                scanner_guard_advance(m);
                return TOKEN_RIGHT_SHIFT_EQUAL;
            }

            return TOKEN_RIGHT_ANGLE; // >
        }
        case '&':
            return scanner_match(m, '&') ? TOKEN_AND_AND : TOKEN_AND;
        case '|':
            return scanner_match(m, '|') ? TOKEN_OR_OR : TOKEN_OR;
        case '~':
            return TOKEN_TILDE;
        case '^':
            return scanner_match(m, '=') ? TOKEN_XOR_EQUAL : TOKEN_XOR;
        default:
            return 0;
    }
}

static void scanner_cursor_init(module_t *m) {
    m->s_cursor.source = m->source;
    m->s_cursor.current = m->source;
    m->s_cursor.guard = m->source;
    m->s_cursor.length = 0;
    m->s_cursor.line = 1;
    m->s_cursor.column = 1;
    m->s_cursor.space_prev = STRING_EOF;
    m->s_cursor.space_next = STRING_EOF;
}

static char *scanner_string_advance(module_t *m, char close_char) {
    // 在遇到下一个闭合字符之前， 如果中间遇到了空格则忽略
    m->s_cursor.guard++; // 跳过 open_char
    char escape_char = '\\';

    // 由于包含字符串处理, 所以这里不使用 scanner_gen_word 直接生成
    autobuf_t *buf = autobuf_new(10);

    while (*m->s_cursor.guard != close_char && !scanner_at_eof(m)) {
        char guard = *m->s_cursor.guard;

        SCANNER_ASSERTF(guard != '\n', "string cannot newline")

        if (guard == escape_char) {
            // 跳过转义字符
            m->s_cursor.guard++;

            guard = *m->s_cursor.guard;
            switch (guard) {
                case 'n':
                    guard = '\n';
                    break;
                case 't':
                    guard = '\t';
                    break;
                case 'r':
                    guard = '\r';
                    break;
                case 'b':
                    guard = '\b';
                    break;
                case 'f':
                    guard = '\f';
                    break;
                case 'a':
                    guard = '\a';
                    break;
                case 'v':
                    guard = '\v';
                    break;
                case '0':
                    guard = '\0';
                    break;
                case '\\':
                case '\'':
                case '\"':
                    break;
                default:
                    dump_errorf(m, CT_STAGE_SCANNER, m->s_cursor.line, m->s_cursor.column, "unknown escape char %c",
                                guard);
            }
        }

        autobuf_push(buf, &guard, 1);
        scanner_guard_advance(m);
    }

    // 跳过 close char
    m->s_cursor.guard++;

    // 结尾增加一个 \0 字符
    char end = '\0';

    autobuf_push(buf, &end, 1);

    return (char *) buf->data;
}

static token_type_t
scanner_rest(char *word, int word_length, int8_t rest_start, int8_t rest_length, char *rest, int8_t type) {
    if (rest_start + rest_length == word_length && memcmp(word + rest_start, rest, rest_length) == 0) {
        return type;
    }
    return TOKEN_IDENT;
}

/**
 * last not ,、[、=、{
 * next not {
 * @return
 */
static bool scanner_need_stmt_end(module_t *m, token_t *prev_token) {
    // 以这些类型结束行时，自动加入结束符号，如果后面还希望加表达式，比如 return a; 那请不要在 return 和 a 之间添加换行符
    // var a = new int，类似这种情况，类型也会作为表达式的结尾。需要进行识别
    switch (prev_token->type) {
        case TOKEN_IMPORT_STAR: // import x as *
        case TOKEN_LITERAL_INT: // 1
        case TOKEN_LITERAL_STRING: // 'hello'
        case TOKEN_LITERAL_FLOAT: // 3.14
        case TOKEN_IDENT: // a
        case TOKEN_BREAK: // break
        case TOKEN_CONTINUE: // continue
        case TOKEN_RETURN: // return\n
        case TOKEN_TRUE:
        case TOKEN_FALSE:
        case TOKEN_RIGHT_PAREN: // )\n
        case TOKEN_RIGHT_SQUARE: // ]\n
        case TOKEN_RIGHT_CURLY: // }\n
        case TOKEN_RIGHT_ANGLE: // new <T>\n
        case TOKEN_BOOL:
        case TOKEN_FLOAT:
        case TOKEN_F32:
        case TOKEN_F64:
        case TOKEN_INT:
        case TOKEN_I8:
        case TOKEN_I16:
        case TOKEN_I32:
        case TOKEN_I64:
        case TOKEN_UINT:
        case TOKEN_U8:
        case TOKEN_U16:
        case TOKEN_U32:
        case TOKEN_U64:
        case TOKEN_STRING:
        case TOKEN_VOID:
        case TOKEN_NULL:
        case TOKEN_NOT: // fn test():void!;  tpl fn 声明可以已 ! 结尾
        case TOKEN_QUESTION: // type nullable = T?; typedef 或者 fn 中都可能存在 ? 结尾的语句
        case TOKEN_LABEL:
            return true;
        default:
            return false;
    }
}

static long scanner_number_convert(module_t *m, char *word, int base) {
    char *endptr;
    long decimal = strtol(word, &endptr, base);
    if (*endptr != '\0') {
        dump_errorf(m, CT_STAGE_SCANNER, m->s_cursor.line, m->s_cursor.column - strlen(word),
                    "Invalid number `%s`", word);
    }

    return decimal;
}

static char *scanner_hex_number_advance(module_t *m) {
    m->s_cursor.guard++; // 0
    m->s_cursor.guard++; // x
    m->s_cursor.current = m->s_cursor.guard;

    // guard = current, 向前推进 guard,并累加 length
    while (scanner_is_hex_number(m, *m->s_cursor.guard) && !scanner_at_eof(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

static char *scanner_oct_number_advance(module_t *m) {
    m->s_cursor.guard++; // 0
    m->s_cursor.guard++; // o
    m->s_cursor.current = m->s_cursor.guard;

    while (scanner_is_oct_number(m, *m->s_cursor.guard) && !scanner_at_eof(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

static char *scanner_bin_number_advance(module_t *m) {
    m->s_cursor.guard++; // 0
    m->s_cursor.guard++; // b
    m->s_cursor.current = m->s_cursor.guard;

    while (scanner_is_bin_number(m, *m->s_cursor.guard) && !scanner_at_eof(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

static char *scanner_number_advance(module_t *m) {
    while ((scanner_is_number(m, *m->s_cursor.guard) || *m->s_cursor.guard == '.') && !scanner_at_eof(m)) {
        scanner_guard_advance(m);
    }

    return scanner_gen_word(m);
}

token_t *scanner_item(module_t *m, linked_node *prev_node) {
    // reset by guard
    m->s_cursor.current = m->s_cursor.guard;
    m->s_cursor.length = 0;

    if (scanner_is_alpha(m, *m->s_cursor.current)) {
        char *word = scanner_ident_advance(m);

        token_t *t = token_new(scanner_ident(word, m->s_cursor.length), word,
                               m->s_cursor.line, m->s_cursor.column);
        return t;
    }

    if (scanner_match(m, '@')) {
        char *word = scanner_ident_advance(m);
        word++;
        token_t *t = token_new(TOKEN_MACRO_IDENT, word, m->s_cursor.line, m->s_cursor.column);
        return t;
    }

    if (scanner_match(m, '#')) {
        char *word = scanner_ident_advance(m);
        word++;
        token_t *t = token_new(TOKEN_LABEL, word, m->s_cursor.line, m->s_cursor.column);
        return t;
    }


    // 首个字符是 0 ~ 9 则判定为数字
    if (scanner_is_number(m, *m->s_cursor.current)) {
        char *word = NULL;
        long decimal;

        // 0 开头的数字特殊处理
        if (*m->s_cursor.guard == '0') {
            if (m->s_cursor.guard[1] == 'x') {
                decimal = scanner_number_convert(m, scanner_hex_number_advance(m), 16);
                word = itoa(decimal);
            } else if (m->s_cursor.guard[1] == 'o') {
                decimal = scanner_number_convert(m, scanner_oct_number_advance(m), 8);
                word = itoa(decimal);
            } else if (m->s_cursor.guard[1] == 'b') {
                decimal = scanner_number_convert(m, scanner_bin_number_advance(m), 2);
                word = itoa(decimal);
            } else {
                word = scanner_number_advance(m); // 1, 1.12, 0.233
            }
        } else {
            word = scanner_number_advance(m); // 1, 1.12, 0.233
        }

        // word 已经生成，通过判断 word 中是否包含 . 判断 int 开头的 word 的类型
        uint8_t type;
        if (scanner_is_float(m, word)) {
            type = TOKEN_LITERAL_FLOAT;
        } else {
            type = TOKEN_LITERAL_INT;
        }

        return token_new(type, word, m->s_cursor.line, m->s_cursor.column);
    }

    // 字符串扫描
    if (scanner_is_string(m, *m->s_cursor.current)) {
        char *str = scanner_string_advance(m, *m->s_cursor.current);
        return token_new(TOKEN_LITERAL_STRING, str, m->s_cursor.line, m->s_cursor.column);
    }

    // if current is 特殊字符
    token_type_t special_type = scanner_special_char(m);
    SCANNER_ASSERTF(special_type > 0, "special characters are not recognized");

    if (special_type == TOKEN_STAR && prev_node && ((token_t *) prev_node->value)->type == TOKEN_AS) {
        return token_new(TOKEN_IMPORT_STAR, scanner_gen_word(m), m->s_cursor.line, m->s_cursor.column);
    }

    return token_new(special_type, scanner_gen_word(m), m->s_cursor.line, m->s_cursor.column);
}

/**
 * 符号表使用什么结构来存储, 链表结构
 * @param chars
 */
linked_t *scanner(module_t *m) {
    // init scanner
    scanner_cursor_init(m);

    linked_t *list = linked_new();

    while (!scanner_at_eof(m)) {
        // 每经过一个 word 就需要检测是否有空白符号或者注释需要跳过
        bool has_newline = scanner_skip_space(m);
        if (scanner_at_eof(m)) {
            break;
        }

        linked_node *prev_node = linked_last(list);
        if (has_newline && prev_node) {
            token_t *prev_token = prev_node->value;
            if (scanner_need_stmt_end(m, prev_token)) {
                token_t *stmt_end = token_new(TOKEN_STMT_EOF, ";", prev_token->line,
                                              prev_token->column + prev_token->length + 1);

                // push token_t TOKEN_STMT_EOF
                linked_push(list, stmt_end);
            }
        }

        token_t *next_token = scanner_item(m, prev_node); // 预扫描一个字符，用于辅助 stmt_end 插入判断
        linked_push(list, next_token);
    }

    linked_push(list, token_new(TOKEN_EOF, "EOF", m->s_cursor.line, m->s_cursor.line));

    return list;
}


static bool scanner_multi_comment_end(module_t *m) {
    return m->s_cursor.guard[0] == '*' && m->s_cursor.guard[1] == '/';
}

/**
 * 在没有 ; 号的情况下，换行符在大多数时候承担着判断是否需要添加 TOKEN_EOF
 */
static bool scanner_skip_space(module_t *m) {
    bool has_new = false;

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
                has_new = true;
                break;
            }
            case '/':
                // guard[1] 表示 guard 指向的下一个字符
                if (m->s_cursor.guard[1] == '/') {
                    while (m->s_cursor.guard[0] != '\n' && !scanner_at_eof(m)) {
                        scanner_guard_advance(m);
                    }
                    break;
                } else if (m->s_cursor.guard[1] == '*') {
                    while (!scanner_multi_comment_end(m)) {
                        if (scanner_at_eof(m)) {
                            dump_errorf(m, CT_STAGE_SCANNER, m->s_cursor.line, m->s_cursor.length,
                                        "unterminated comment");
                        }
                        scanner_guard_advance(m);
                    }
                    scanner_guard_advance(m); // *
                    scanner_guard_advance(m); // /
                    break;
                } else {
                    m->s_cursor.space_next = *m->s_cursor.guard;
                    return has_new;
                }

            default: {
                m->s_cursor.space_next = *m->s_cursor.guard;
                return has_new;
            }
        }
    }
}


static token_type_t scanner_ident(char *word, int length) {
    switch (word[0]) {
        case 'a': {
            switch (word[1]) {
                case 's': {
                    return scanner_rest(word, length, 2, 0, "", TOKEN_AS);
                }
                case 'n': {
                    return scanner_rest(word, length, 2, 1, "y", TOKEN_ANY);
                }
                case 'r': {
                    return scanner_rest(word, length, 2, 1, "r", TOKEN_ARR);
                }
            }
            break;
        }
        case 'b':
            switch (word[1]) {
                case 'o':
                    return scanner_rest(word, length, 2, 2, "ol", TOKEN_BOOL);
                case 'r':
                    return scanner_rest(word, length, 2, 3, "eak", TOKEN_BREAK);
            }
            break;
        case 'c':
            switch (word[1]) {
                case 'o':
                    switch (word[2]) {
                        case 'n':
                            return scanner_rest(word, length, 3, 5, "tinue", TOKEN_CONTINUE);
                            //                        case 'r':
                            //                            return scanner_rest(word, length, 3, 1, "o", TOKEN_CORO);
                    }
                    return scanner_rest(word, length, 2, 6, "ntinue", TOKEN_CONTINUE);
                case 'a':
                    return scanner_rest(word, length, 2, 3, "tch", TOKEN_CATCH);
                case 'h':
                    return scanner_rest(word, length, 2, 2, "an", TOKEN_CHAN);
            }
            break;
        case 'e':
            return scanner_rest(word, length, 1, 3, "lse", TOKEN_ELSE);
        case 'f': {
            switch (word[1]) {
                case 'n':
                    return scanner_rest(word, length, 2, 0, "", TOKEN_FN);
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
            break;
        }
        case 'g':
            return scanner_rest(word, length, 1, 1, "o", TOKEN_GO);
        case 'i': {
            if (length == 2 && word[1] == 'n') {
                return TOKEN_IN;
            } else if (length == 2 && word[1] == 's') {
                return TOKEN_IS;
            } else if (length == 3 && word[1] == 'n' && word[2] == 't') {
                return TOKEN_INT;
            }

            switch (word[1]) {
                case 'm':
                    return scanner_rest(word, length, 2, 4, "port", TOKEN_IMPORT);
                case 'f':
                    return scanner_rest(word, length, 2, 0, "", TOKEN_IF);
                case 'n':
                    return scanner_rest(word, length, 2, 7, "terface", TOKEN_INTERFACE);
                case '8':
                    return scanner_rest(word, length, 2, 0, "", TOKEN_I8);
                case '1':
                    return scanner_rest(word, length, 2, 1, "6", TOKEN_I16);
                case '3':
                    return scanner_rest(word, length, 2, 1, "2", TOKEN_I32);
                case '6':
                    return scanner_rest(word, length, 2, 1, "4", TOKEN_I64);
            }
            break;
        }
        case 'l': {
            return scanner_rest(word, length, 1, 2, "et", TOKEN_LET);
        }
        case 'n':
            switch (word[1]) {
                case 'u': // null
                    return scanner_rest(word, length, 2, 2, "ll", TOKEN_NULL);
                    // case 'e':// new, new 识别成 ident 在 parser 采用固定语法结构时才会被识别成 new
                    // return scanner_rest(word, length, 2, 1, "w", TOKEN_NEW);
            }
            break;
        case 'p':
            return scanner_rest(word, length, 1, 2, "tr", TOKEN_PTR);
        case 's': {
            // self,string,struct,sizeof,sett
            switch (word[1]) {
                case 'e': {
                    switch (word[2]) {
                        case 't':
                            return scanner_rest(word, length, 3, 0, "", TOKEN_SET);
                        case 'l': // select
                            return scanner_rest(word, length, 3, 3, "ect", TOKEN_SELECT);
                    }
                }
            }

            if (length == 6 && word[1] == 't' && word[2] == 'r') {
                switch (word[3]) {
                    case 'i':
                        return scanner_rest(word, length, 4, 2, "ng", TOKEN_STRING);
                    case 'u':
                        return scanner_rest(word, length, 4, 2, "ct", TOKEN_STRUCT);
                }
            }
            break;
        }
        case 't': {
            // tup/throw/type/true
            switch (word[1]) {
                case 'h':
                    return scanner_rest(word, length, 2, 3, "row", TOKEN_THROW);
                case 'y': // type
                    return scanner_rest(word, length, 2, 2, "pe", TOKEN_TYPE);
                case 'u': // tup
                    return scanner_rest(word, length, 2, 1, "p", TOKEN_TUP);
                case 'r': {
                    switch (word[2]) {
                        case 'y':
                            return scanner_rest(word, length, 3, 0, "", TOKEN_TRY);
                        case 'u':
                            return scanner_rest(word, length, 3, 1, "e", TOKEN_TRUE);
                    }
                    break;
                }
            }
            break;
        }
        case 'v': {
            switch (word[1]) {
                case 'a':
                    return scanner_rest(word, length, 2, 1, "r", TOKEN_VAR);
                case 'e': // vec
                    return scanner_rest(word, length, 2, 1, "c", TOKEN_VEC);
                case 'o': // void
                    return scanner_rest(word, length, 2, 2, "id", TOKEN_VOID);
            }
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
            break;
        }
        case 'm': {
            // map
            switch (word[1]) {
                case 'a': {
                    switch (word[2]) {
                        case 'p':
                            return scanner_rest(word, length, 3, 0, "", TOKEN_MAP);
                        case 't':
                            return scanner_rest(word, length, 3, 2, "ch", TOKEN_MATCH);
                    }
                }
            }
        }
        case 'r': {
            return scanner_rest(word, length, 1, 5, "eturn", TOKEN_RETURN);
        }
    }

    return TOKEN_IDENT;
}
