#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "stdlib.h"
#include "ast.h"
#include "instruction.h"

int label_count = 0;

typedef struct {
  string type; // 数据类型
  string ident; // 标识符
  int size; // 类型占用字节数
  int stack_offset; //
  ast_struct_decl *custom_type; // 如果 type 不是基础类型是，该指针则执行对应的 ast
  bool is_local;
} compiler_var; // 对象属性是否具有这些数据？

// 这该不会就是一个闭包了吧
typedef struct compiler {
  struct compiler *parent;
  compiler_var locals[UINT8_MAX]; // 编译时局部变量
  compiler_var frees[UINT8_MAX]; // 编译时自由变量
  int local_count;
  int free_count;
  int scope_depth;
  int stack_offset; // 当前栈帧中的偏移量
} compiler;

// 动态 struct 表,编译时使用，最好能使 type => value 的形式

// 当前编译时, 计算 local 和 upvalue 时会使用
compiler *current = NULL;
string current_type;

// return label name
// linkage 分为特殊值 next 对应继续执行 /return 对应 ret/其余字符串 jmp linkage
insts *compiler_block(ast_block_stmt *block, string target, string linkage);

void compiler_var_decl(ast_var_decl_stmt *decl, string target, string linkage);

// int b = 123; // result to %rax
// int b = 123 + a * 2; // result to %rax
// int b = call(); // result to %rax
// People c = call(); // 预处理返回值优化
// People a = {.name = "1"} // 预处理返回值优化
// Closure d = function() {} // 预处理返回值优化
// 当且仅当 expression 有返回值时才有效, target 通常为 %rax 寄存器
// 如果返回结果是大型数据结构则需要进行返回值优化，即 target 中已经保存了返回值的位置
insts *compiler_assign(ast_assign_stmt *stmt, string target, string linkage);

// 结果存在哪里？如果当成表达式编译的话放在 rax 好了？
insts *compiler_expr(ast_expr expr, string target, string linkage);

// 比较特殊，自身会生成一个局部变量，且可以再次赋值
void compiler_closure(ast_function_decl function, string target, string linkage);

void compiler_call(ast_call_function call, string target, string linkage);

insts *compiler_binary(ast_binary_expr *expr, string target, string linkage);

insts *compiler_literal(ast_literal_expr *literal, string target, string linkage);
insts *compiler_ident(ast_ident *literal, string target, string linkage);

insts *compiler_if(ast_if_stmt *if_stmt, string target, string linkage);

int calc_var_size(string type); // 单位字节
ast_struct_decl *lookup_custom_type();
string compiler_target(ast_expr expr);
compiler_var resolve_ident(ast_ident *ident, compiler *c);
compiler_var resolve_obj_property(ast_obj_property *ident);
string compiler_string(string value);
void begin_scope();
void end_scope();
string make_label(string label);

// 根据目标寄存器判断出操作寄存器
string register_binary_src(string target);
string register_imm(string value);

string register_false();

// 根据变量结构体解析出栈位置
string register_var(compiler_var var);
string register_if_condition();

#endif //NATURE_SRC_COMPILER_H_
