#include <stdio.h>
#include <stdlib.h>
#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "src/semantic/infer.h"
#include "utils/helper.h"
#include "src/debug/debug.h"
#include "src/semantic/analysis.h"
#include "src/target.h"
#include <unistd.h>

int main() {
    // 读取文件
    char *source = file_read("/home/vagrant/Code/nature/example/ssa.n");

    // scanner
    list *token_list = scanner(source);
    // parser
    ast_block_stmt stmt_list = parser(token_list);

    printf("Hello, World!\n");
    return 0;
}
