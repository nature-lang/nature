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
  int stack_offset;
  int scope_depth; // 当前局部变量所处的块作用域深度（基于当前函数）

  ast_struct_decl *custom_type; // 如果 type 不是基础类型是，该指针则执行对应的 ast
  bool is_capture; // 如果 local 被捕获，则其被释放时应该 close free var
} local_var; // 对象属性是否具有这些数据？

/**
 * 如果 is_local 为 true,则 index 为 compiler.locals 中的 index
 * 如果 is_local 为 false,则 index 为 compiler.frees 中的 index
 */
typedef struct {
  bool is_local;
  int8_t index;
  int8_t self_index; // 对象自身所处的 index 的位置
  string type; // 数据类型
} free_var;

/**
 * frees 对于当前编译时不存在的变量，会被收集写入到 frees,但是实际读取者是 compiler->parent
 * frees.is_local 也是相对于写入方的 parent 来说的
 * ---------------------------------------------------------------------------------------------------------------------
 */
typedef struct compiler {
  struct compiler *parent;
  local_var locals[UINT8_MAX]; // 当前函数内的局部变量数量
  free_var frees[UINT8_MAX]; // 编译时自由变量
  int free_count;
  int local_count; // 当前作用域中变量的数量
  int scope_depth; // 当前函数内的块作用域深度(基于当前函数)
  int stack_offset; // 当前栈帧中的偏移量(基于当前函数）
  int max_stack_offset;
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

insts *compiler_literal(ast_literal *literal, string target, string linkage);
insts *compiler_ident(ast_ident *literal, string target, string linkage);

insts *compiler_if(ast_if_stmt *if_stmt, string target, string linkage);

// 更新 stack_offset 和 max_stack_offset
void change_stack_offset(compiler *c, int size);
int calc_var_size(string type); // 单位字节
ast_struct_decl *lookup_custom_type();
string compiler_assign_target(ast_expr expr);
string compiler_ident_target(ast_ident *ident);
local_var resolve_ident(ast_ident *ident);
int8_t resolve_free(compiler *c, ast_ident *ident);
int8_t push_free(compiler *c, int8_t index, bool is_local);
local_var resolve_obj_property(ast_obj_property *ident);
string compiler_string(string value);
void begin_scope();
void end_scope();
string make_label(string label);

// 根据目标寄存器判断出操作寄存器
string register_binary_src(string target);
string register_imm(string value);

string register_false();

// 根据 var->is_free 决定从何处取来相关的变量的地址
string register_local_var(local_var var);
// 根据 current->frees[index] 从 env 中读取出对应的自由变量
string register_free_var(int8_t index);
string register_if_condition();

#endif //NATURE_SRC_COMPILER_H_
