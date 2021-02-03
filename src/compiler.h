#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "stdlib.h"
#include "ast.h"
#include "instruction.h"

typedef struct {
  string type; // 数据类型
  string identifier; // 标识符
} Local;

typedef struct {
  string type;
  string identifier; // 标识符
} Upvalue;

// 这该不会就是一个闭包了吧
typedef struct Compiler {
  struct Compiler *parent;
  Local locals[UINT8_MAX]; // 编译时局部变量
  Upvalue upvales[UINT8_MAX]; // 编译时自由变量
  int localCount;
  int scopeDepth;
} Compiler;

// 动态 struct 表,编译时使用，最好能使 type => value 的形式

// 当前编译时, 计算 local 和 upvalue 时会使用
Compiler *current = NULL;
int8_t current_type;

// return label name
// linkage 分为特殊值 next 对应继续执行 /return 对应 ret/其余字符串 jmp linkage
void compiler_block(ast_block_stat block, string target, string linkage);

void compiler_var_decl(ast_var_decl_stat stat, string target, string linkage);

// int b = 123; // result to %rax
// int b = 123 + a * 2; // result to %rax
// int b = call(); // result to %rax
// People c = call(); // 预处理返回值优化
// People a = {.name = "1"} // 预处理返回值优化
// Closure d = function() {} // 预处理返回值优化
// 当且仅当 expression 有返回值时才有效, target 通常为 %rax 寄存器
// 如果返回结果是大型数据结构则需要进行返回值优化，即 target 中已经保存了返回值的位置
void compiler_assign(ast_assign_stat stat, string target, string linkage);

// 结果存在哪里？如果当成表达式编译的话放在 rax 好了？
void compiler_expression(ast_expr expr, string target, string linkage);

// 比较特殊，自身会生成一个局部变量，且可以再次赋值
void compiler_closure(ast_function_decl function, string target, string linkage);

void compiler_call(ast_call_function call, string target, string linkage);

insts *compiler_binary(ast_binary_expr *expr, string target, string linkage);

insts *compiler_literal(ast_literal_expr *literal, string target, string linkage);

// 根据目标寄存器判断出操作寄存器
string parse_src_target(string target);

string parse_imm(string value);

string resolve_identifier(string value);
string compiler_string(string value);
#endif //NATURE_SRC_COMPILER_H_
