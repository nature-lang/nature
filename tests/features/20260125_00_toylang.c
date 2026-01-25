#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "Token(kind: TokenKindLet, literal: let, pos: (line: 1, column: 1))\n"
                "Token(kind: TokenKindIdent, literal: ____x00___09, pos: (line: 1, column: 5))\n"
                "Token(kind: TokenKindAssign, literal: =, pos: (line: 1, column: 18))\n"
                "Token(kind: TokenKindMinus, literal: -, pos: (line: 1, column: 20))\n"
                "Token(kind: TokenKindNumber, literal: 10, pos: (line: 1, column: 21))\n"
                "Token(kind: TokenKindSemicolon, literal: ;, pos: (line: 1, column: 23))\n"
                "Token(kind: TokenKindLet, literal: let, pos: (line: 5, column: 1))\n"
                "Token(kind: TokenKindIdent, literal: a, pos: (line: 5, column: 5))\n"
                "Token(kind: TokenKindAssign, literal: =, pos: (line: 5, column: 7))\n"
                "Token(kind: TokenKindIf, literal: if, pos: (line: 5, column: 9))\n"
                "Token(kind: TokenKindLParen, literal: (, pos: (line: 5, column: 12))\n"
                "Token(kind: TokenKindIdent, literal: a, pos: (line: 5, column: 13))\n"
                "Token(kind: TokenKindLess, literal: <, pos: (line: 5, column: 14))\n"
                "Token(kind: TokenKindNumber, literal: 1, pos: (line: 5, column: 15))\n"
                "Token(kind: TokenKindRParen, literal: ), pos: (line: 5, column: 16))\n"
                "Token(kind: TokenKindIdent, literal: a, pos: (line: 5, column: 18))\n"
                "Token(kind: TokenKindElse, literal: else, pos: (line: 5, column: 20))\n"
                "Token(kind: TokenKindNumber, literal: 2.3333, pos: (line: 5, column: 25))\n"
                "Token(kind: TokenKindSemicolon, literal: ;, pos: (line: 5, column: 31))\n"
                "Token(kind: TokenKindEof, literal: eof, pos: (line: 6, column: 1))\n";
    assert_string_equal(raw, str);
}

int main(void) {
    VERBOSE = true; // TODO
    TEST_BASIC
}