#include "compiler.h"
#include "instruction.h"
#include "string.h"

// closure a = {
//     env: env
//     call: call
// }
// 假设返回值是 N, N 可以是 %rax, 又或者是 -4(%rax), 返回值优化？
void compiler_closure(ast_function_decl function, string target, string linkage) {
  // 1. 读取局部变量 struct(直接将基址放入到 esp+offset) 中, 下文基于 struct point

  // 2. 收集环境并放入 (struct point).env

  // 3. 编译 lambda 放入 (struct point).call
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
  // ① 根据 ident 确定 closure 基值，并放入临时寄存器   -n(%esp), temp
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
    case AST_EXPR_TYPE_LITERAL: return compiler_literal((ast_literal_expr *) expr.expr, target, linkage);
    case AST_EXPR_TYPE_IDENTIFIER: return compiler_ident((ast_ident *) expr.expr, target, linkage);
//    case AST_EXPR_TYPE_ACCESS_STRUCT: return compiler_a
  }
}

// target 是上层经过精心规划的
insts *compiler_binary(ast_binary_expr *expr, string target, string linkage) {
  // 左值 %rdx， 右值 %rax.
  string src = register_binary_src(target);
  insts *left = compiler_expr(expr->left, src, linkage);
  // TODO 计算右值期间如果 %rdx 被覆盖了怎么办？ 可以使用 push/pop 解决？还是哪个阶段可以保护 %rdx
  insts *right = compiler_expr(expr->right, target, linkage);
  insts *ins = inst_append(left, right);

  insts *result = inst_new();
  switch (expr->operator) {
    case AST_EXPR_ADD: {
      // ... + ...
      inst_add *add = NEW_INST(inst_add);
      add->src = src; // 需要考虑浮点型和整形的区别
      add->dst = target;
      add->byte = 64; // TODO bool 型 怎么办？
      add->type = current_type;
      inst_insert(result, &add, INST_MOV);
      break;
    }
    case AST_EXPR_SUBTRACT: {
      inst_sub *sub = NEW_INST(inst_add);
      sub->src = src;
      sub->dst = target;
      sub->byte = 64;
      sub->type = current_type;
      inst_insert(result, sub, INST_SUB);
      break;
    }
    case AST_EXPR_MULTIPLY: {
      inst_mul *mul = NEW_INST(inst_mul);
      mul->src = src;
      mul->dst = target;
      mul->byte = 64;
      mul->type = current_type;
      inst_insert(result, mul, INST_MUL);
      break;
    }
  }

  return inst_append(ins, result);
}

insts *compiler_ident(ast_ident *ident, string target, string linkage) {
  insts *insts = inst_new();
  compiler_var var = resolve_ident(ident, current);

  inst_mov *mov = NEW_INST(inst_mov);
  mov->src = register_var(var);
  mov->dst = target;
  mov->type = var.type;
  mov->byte = var.size;
  inst_insert(insts, mov, INST_MOV);

  return insts;
}

// 立即数
insts *compiler_literal(ast_literal_expr *literal, string target, string linkage) {
  insts *result = inst_new();
  switch (literal->type) {
    case AST_BASE_TYPE_FLOAT:
    case AST_BASE_TYPE_INT: {
      // 解析出立即数
      string src = register_imm(literal->value);

      inst_mov *mov = NEW_INST(inst_mov);
      mov->src = src;
      mov->dst = target;
      mov->byte = 64;
      mov->type = current_type;
      inst_insert(result, mov, INST_MOV);
      break;
    }
    case AST_BASE_TYPE_STRING: {
      string src = compiler_string(literal->value); // 字符串地址寄存器
      inst_mov *mov = NEW_INST(inst_mov);
      mov->src = src;
      mov->dst = target;
      mov->byte = 64; // 指针类型
      mov->type = current_type;
      inst_insert(result, mov, INST_MOV);
      break;
    }
  }

  return result;
}

// int a;
// 定义变量，读取长度，并根据需要开辟栈插槽和栈偏移
void compiler_var_decl(ast_var_decl_stmt *decl, string target, string linkage) {
  // 1.判断是否已经定义过变量
  for (int i = 0; i < current->local_count; ++i) {
    compiler_var local = current->locals[i];
    if (strcmp(decl->ident, local.ident) == 0) {
      // TODO 想办法抛出错误和错误的行号
      exit(1);
    }
  }

  // 2. 写入 local
  compiler_var local;
  local.ident = decl->ident;
  local.type = decl->type;
  local.size = calc_var_size(decl->type);
  local.stack_offset = current->stack_offset;
  local.custom_type = lookup_custom_type(local.type);

  // 3. 写入 current compiler
  current->locals[current->local_count++] = local;
  current->stack_offset += local.size;
}

insts *compiler_block(ast_block_stmt *block, string target, string linkage) {
  insts *result = inst_new();
  begin_scope(); // 词法作用域增加
  // 循环，遍历，
  for (int i = 0; i < block->count; ++i) {
    insts *append = NULL;
    ast_stmt stmt = block->list[i];
    // 表达式数据导向
    switch (stmt.type) {
      case AST_STMT_VAR_DECL: compiler_var_decl((ast_var_decl_stmt *) stmt.stmt, target, linkage);
        break;
      case AST_STMT_ASSIGN: {
        append = compiler_assign((ast_assign_stmt *) stmt.stmt, target, linkage);
        break;
      }
      case AST_STMT_IF: {
        append = compiler_if((ast_if_stmt *) stmt.stmt, target, linkage);
        break;
      }
    }

    inst_append(result, append);
  }
  end_scope();

  return result;
}

// stmt 的 target 基本没啥用
insts *compiler_assign(ast_assign_stmt *assign, string target, string linkage) {
  insts *result = inst_new();
  // 1. 编译出左值(TODO 如果左值是一个临时寄存器，则需要考虑被覆盖的风险)
  target = compiler_target(assign->left);
  // 3. 编译右值表达式
  insts *append = compiler_expr(assign->right, target, linkage);

  inst_append(result, append);

  return result;
}

string compiler_target(ast_expr expr) {
  compiler_var variable;
  switch (expr.type) {
    case AST_EXPR_TYPE_IDENTIFIER: {
      variable = resolve_ident((ast_ident *) expr.expr, current);
      break;
    }
    case AST_EXPR_TYPE_OBJ_PROPERTY: {
      variable = resolve_obj_property((ast_obj_property *) expr.expr);
      break;
    }
  }

  current_type = variable.type;
  return register_var(variable);
}

/**
 * 普通块级作用域不会像函数 block 一样引用外部变量导致指针空悬问题
 * 普通块级作用域总是从内向外出栈
 */
compiler_var resolve_ident(ast_ident *ident, compiler *c) {
  for (int i = 0; i < c->local_count; ++i) {
    compiler_var local = c->locals[i];
    if (strcmp(*ident, local.ident) == 0) {
      c->locals[i].is_local = true;
      return local;
    }
  }

  if (c->parent == NULL) {
    // 没找到准备报错吧
    compiler_var empty = {.size = 0};
    return empty;
  }

  // 递归搜索父级
  compiler_var free = resolve_ident(ident, c->parent);
  if (free.size == 0) { // 搜索成功
    return free;
  }

  // 加入到 free 数组
  free.is_local = false;
  c->frees[c->free_count++] = free;
  return free;
}

compiler_var resolve_obj_property(ast_obj_property *ident) {
  // 递归编译
}

/**
 * 汇编指令推测
 *
 */
insts *compiler_if(ast_if_stmt *if_stmt, string target, string linkage) {
  insts *result = inst_new();
  target = register_if_condition();
  // 编译 condition
  insts *conditions = compiler_expr(if_stmt->condition, target, linkage);
  inst_append(result, conditions);

  // test conditions
  inst_cmp *cmp = NEW_INST(inst_cmp);
  cmp->left = register_false();
  cmp->right = target;
  cmp->byte = 64;
  inst_insert(result, cmp, INST_CMP);

  string end_label = make_label("end_if");
  string alternate_label = make_label("alternate_if");

  // jmp 等于 false 则跳转，等于 true 则顺序执行
  inst_jmp *jmp = NEW_INST(inst_jmp);
  jmp->type = INST_JMP_TYPE_EQUAL;
  if (if_stmt->alternate.count == 0) { // 不存在 else
    jmp->dst = end_label;
  } else {
    jmp->dst = alternate_label;
  }
  inst_insert(result, jmp, INST_JMP);

  // 编译 consequent block
  insts *consequent = compiler_block(&if_stmt->consequent, NULL, end_label);
  inst_append(result, consequent);

  // alternate block label
  inst_label *inst_alternate_label = NEW_INST(inst_label);
  inst_alternate_label->name = alternate_label;
  inst_insert(result, inst_alternate_label, INST_LABEL);
  // 编译 alternate block
  insts *alternate = compiler_block(&if_stmt->alternate, NULL, linkage);
  inst_append(result, alternate);

  // label end if
  inst_label *inst_endif_label = NEW_INST(inst_label);
  inst_endif_label->name = alternate_label;
  inst_insert(result, inst_endif_label, INST_LABEL);

  return result;
}


