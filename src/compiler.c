#include "compiler.h"
#include "instruction.h"
#include "string.h"

// closure a = {
//     env: env
//     call: call
// }
// 假设返回值是 N, N 可以是 %rax, 又或者是 -4(%rax), 返回值优化？
void compiler_closure(ast_function_decl function, string target, string linkage) {
  // ① 读取局部变量 struct(直接将基址放入到 esp+offset) 中, 下文基于 struct point

  // ② 收集环境并放入 (struct point).env

  // ③ 编译 lambda 放入 (struct point).call
  // 如果返回的参数不是基本类型，则从约定寄存器中读取返回值的指针地址
  // 从 参数 1 中读取出自由变量环境（env）
  // 按调用约定处理 参数2，参数3 ... 照写 pop_param();
  // 编译 block
  // 如果 编译 block 期间遇到了自由变量读取，则需要改写成从参数1 env 中读取
  // 返回值编译
  // ret
}

// closure.call(closure.env, param1, param2...)
void compiler_call(ast_call_function call, string target, string linkage) {
  // ① 根据 identifier 确定 closure 基值，并放入临时寄存器   -n(%esp), temp
  // ② 参数 1 永远为自由变量 env,  movq temp.env, %rdi
  // ③ 其余真实调用参数依次添加 movq param_1, %rsi .....
  // ④ call temp.call(%rip)
}

// target 是啥？
// var a = 1 ↓
// mov a, -4(%esp)
//
//
// var a = 1 + 1 (操作对象其实是 add) =>
//  movl 1, %rax
//  add 1, %rax
//  movl %rax, -4(%esp)
//
//
// a = b + 3
// movl -8(%esp), %rax
// movl 3, %rax
// movl %rax, -4(%esp)
// 返回 operand -> 立即数、寄存器、指针偏移
// target 表达式计算结果存放地, caller 会根据数据类型规划传入的值的类型
insts *compiler_expr(ast_expr expr, string target, string linkage) {
  switch (expr.type) {
    case AST_EXPR_TYPE_BINARY: return compiler_binary((ast_binary_expr *) expr.expr, target, linkage);
      break;
    case AST_EXPR_TYPE_LITERAL: return compiler_literal((ast_literal_expr *) expr.expr, target, linkage);
      break;
  }
}

// target 是上层经过精心规划的
insts *compiler_binary(ast_binary_expr *expr, string target, string linkage) {
  // 左值 %rdx， 右值 %rax.
  string src = parse_src_target(target);
  insts *left = compiler_expr(expr->left, src, linkage);
  // TODO 计算右值期间如果 %rdx 被覆盖了怎么办？ 可以使用 push/pop 解决？还是哪个阶段可以保护 %rdx
  insts *right = compiler_expr(expr->right, target, linkage);
  insts *ins = inst_append(left, right);

  insts *result = ins_new();
  switch (expr->operator) {
    case AST_EXPR_ADD: {
      inst_add add;
      add.src = src; // 需要考虑浮点型和整形的区别
      add.dst = target;
      add.byte = 64;
      add.type = current_type;
      inst_insert(result, &add, INST_MOV);
      break;
    }
    case AST_EXPR_SUBTRACT: {
      inst_sub sub;
      sub.src = src;
      sub.dst = target;
      sub.byte = 64;
      sub.type = current_type;
      inst_insert(result, &sub, INST_SUB);
      break;
    }
    case AST_EXPR_MULTIPLY: {
      inst_mul mul;
      mul.src = src;
      mul.dst = target;
      mul.byte = 64;
      mul.type = current_type;
      inst_insert(result, &mul, INST_MUL);
      break;
    }
  }

  return inst_append(ins, result);
}

// return 立即数或者指针便宜位置即可
insts *compiler_literal(ast_literal_expr *literal, string target, string linkage) {
  insts *result = ins_new();
  switch (literal->type) {
    case AST_IN_TYPE_FLOAT:
    case AST_LITERAL_TYPE_INT: {
      string src = parse_imm(literal->value);
      // 解析出立即数
      inst_mov mov = {
          .src = src,
          .dst = target,
          .byte = 64,
          .type = current_type
      };
      inst_insert(result, &mov, INST_MOV);
      break;
    }
    case AST_LITERAL_TYPE_IDENTIFIER: {
      string src = resolve_identifier(literal->value);
      inst_mov mov = {
          .src = src,
          .dst = target,
          .byte = 64,
          .type = current_type
      };
      inst_insert(result, &mov, INST_MOV);
      break;
    }
    case AST_LITERAL_TYPE_STRING: {
      string src = compiler_string(literal->value); // 字符串地址寄存器
      inst_mov mov = {
          .src = src,
          .dst = target,
          .byte = 64,
          .type = current_type
      };
      inst_insert(result, &mov, INST_MOV);
      break;
    }
  }

  return result;
}

