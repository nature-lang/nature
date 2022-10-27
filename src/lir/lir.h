#ifndef NATURE_SRC_LIR_H_
#define NATURE_SRC_LIR_H_

#include "utils/list.h"
#include "utils/helper.h"
#include "src/value.h"
#include "src/ast.h"
#include "utils/table.h"
#include "src/structs.h"
#include "src/register/register.h"

#define TEMP_IDENT "t"
#define TEMP_LABEL "l"
#define CONTINUE_IDENT "continue"
#define WHILE_IDENT "while"
#define END_WHILE_IDENT "end_while"
#define FOR_IDENT "for"
#define END_FOR_IDENT "end_for"
#define END_IF_IDENT "end_if"
#define ALTERNATE_IF_IDENT "alternate_if"

#define RUNTIME_CALL_ARRAY_NEW "array_new"
#define RUNTIME_CALL_ARRAY_VALUE "array_value"

#define RUNTIME_CALL_MAP_NEW "map_new"
#define RUNTIME_CALL_MAP_VALUE "map_value"
#define RUNTIME_CALL_ITERATE_COUNT "iterate_count"

#define RUNTIME_CALL_ITERATE_GEN_KEY "iterate_gen_key"
#define RUNTIME_CALL_ITERATE_GEN_VALUE "iterate_gen_value"

#define RUNTIME_CALL_ENV_NEW "env_new"
#define RUNTIME_CALL_SET_ENV "set_env"
#define RUNTIME_CALL_GET_ENV "get_env"

#define RUNTIME_CALL_STRING_NEW "string_new"
#define RUNTIME_CALL_STRING_ADDR "string_addr"
#define RUNTIME_CALL_STRING_LENGTH "string_length"

// GC 相关函数
#define RUNTIME_CALL_GC_NEW "gc_new"

#define OP(_node) ((lir_op_t*)_node->value)

#define LIR_COPY_VAR_OPERAND(_original) \
({                                 \
  lir_operand_var *_var = NEW(lir_operand_var); \
  _var->ident = (_original)->ident; \
  _var->old = (_original)->old;    \
  _var->reg_id = (_original)->reg_id; \
  _var->decl = (_original)->decl; \
  _var->infer_size_type = (_original)->infer_size_type; \
  _var->indirect_addr = (_original)->indirect_addr; \
  _var;                                   \
})

#define LIR_NEW_IMMEDIATE_OPERAND(operand_type, key, val) \
({                                               \
   lir_imm_t *imm_operand = malloc(sizeof(lir_imm_t)); \
   imm_operand->type = operand_type; \
   imm_operand->key = val; \
   lir_operand_t *operand = malloc(sizeof(lir_operand_t)); \
   operand->type = LIR_OPERAND_IMM; \
   operand->value = imm_operand;              \
   operand; \
})

#define LIR_NEW_OPERAND(_type, _value) \
({                                 \
  lir_operand_t *_operand = NEW(lir_operand_t); \
  _operand->type = _type;           \
  _operand->value = _value;    \
  _operand;                                   \
})

#define LIR_UNIQUE_NAME(_ident) \
({                                 \
   char *temp_name = malloc(strlen(_ident) + sizeof(int) + 2); \
   sprintf(temp_name, "%s_%d", _ident, var_unique_count++); \
   temp_name;                                   \
})

int var_unique_count;
int lir_line;

typedef enum {
    VAR_FLAG_OUTPUT,
    VAR_FLAG_FIRST,
    VAR_FLAG_SECOND,
} lir_var_flag_e;

typedef enum {
    LIR_OPERAND_NULL,
    LIR_OPERAND_VAR, // 虚拟寄存器? 那我凭什么给虚拟寄存器分配内存地址？又或者是 symbol?
    LIR_OPERAND_REG,
    LIR_OPERAND_SYMBOL_VAR, // 虚拟寄存器? 那我凭什么给虚拟寄存器分配内存地址？
    LIR_OPERAND_STACK,
    LIR_OPERAND_PHI_BODY,
    LIR_OPERAND_FORMAL_PARAMS,
    LIR_OPERAND_ACTUAL_PARAMS,
    LIR_OPERAND_SYMBOL_LABEL, // 指令里面都有 label 指令了，operand 其实只需要 symbol 就行了，没必要多余的 label 误导把？
    LIR_OPERAND_IMM,
    LIR_OPERAND_ADDR,
} lir_operand_e;

typedef enum {
    LIR_OPCODE_ADD = 1,
    LIR_OPCODE_CLR,
    LIR_OPCODE_SUB,
    LIR_OPCODE_MUL,
    LIR_OPCODE_DIV,
    LIR_OPCODE_REM, // remainder
    LIR_OPCODE_SLT, // set less than
    LIR_OPCODE_SLE, // set less eq
    LIR_OPCODE_SGT,
    LIR_OPCODE_SGE,
    LIR_OPCODE_SEE,
    LIR_OPCODE_SNE,
    LIR_OPCODE_NOT, // ! 取反
    LIR_OPCODE_NEG, // -取负数

    LIR_OPCODE_LEA, // 取地址, lea _,_ => v_1 (v_1 必须是有效的内存地址)
    LIR_OPCODE_LIA, // load indirect addr to reg ，将内存中的数据加载到寄存器中
    LIR_OPCODE_SIA, // store reg to indirect addr，将寄存器中的数据存入内存

    LIR_OPCODE_PHI, // 复合指令, 位置在 first_param
    LIR_OPCODE_MOVE,
    LIR_OPCODE_BEQ, // branch if eq a,b
    LIR_OPCODE_BAL, // branch always
    LIR_OPCODE_PUSH, // first
    LIR_OPCODE_POP, // output
    LIR_OPCODE_CALL, // 复合指令，位置在 second
    LIR_OPCODE_RUNTIME_CALL,
    LIR_OPCODE_BUILTIN_CALL, // BUILTIN_CALL print params -> nil
    LIR_OPCODE_RETURN, // return != ret, 其主要是做了 mov res -> rax
    LIR_OPCODE_LABEL,
    LIR_OPCODE_FN_BEGIN, // 无操作数
    LIR_OPCODE_FN_END, // 无操作数
} lir_opcode_e;

typedef struct {
    lir_operand_e type;
    void *value;
} lir_operand_t;

// 变量的定义点
typedef struct {
    string ident;
    type_t type; // 原始类型存储(包含指针深度等数据)
//    int16_t *stack_offset; // 可正可负, 对应 rbp-8 或者 rbp+8
} lir_var_decl_t;

/**
 * 存放在寄存器或者内存中, var a = 1
 */
typedef struct {
    string ident; // ssa 后的新名称
    string old; // ssa 之前的名称
//    uint8_t reg_index; // reg list index, 寄存器分配, 及时是同一个变量,也会有时在寄存器中,有时在内存中
    lir_var_decl_t *decl; // local 如果为 nil 就是外部符号引用
    type_base_t type_base;// lir 为了保证通用性，只能有类型，不能有 size, 该类型也决定了分配的寄存器的类型，已经 stack slot 的 size
    bool indirect_addr; // &a 对变量进行解引用操作
    uint8_t flag;
} lir_var_t;

/**
 * mov DWORD 0x1,[rbp-8] 假设 rbp = 100, 则表示将 0x1 存储在 92 ~ 96 之间
 * 也就是 stack 是向下增长，但是存储数据是从低地址往搞地址存储
 */
typedef struct {
    int slot;
    int size;
} lir_stack_t;

typedef struct {
    lir_operand_t *base;
    int offset; // 偏移量是可以计算出来的, 默认为 0
    type_base_t type_base;// lir 为了保证通用性，只能有类型，不能有 size
    bool indirect_addr;
} lir_addr_t;

typedef struct {
    char *ident;
    bool is_local; // 是否为局部符号, 否则就是 global, 可以被链接器链接
} lir_symbol_label_t;

typedef struct {
    string ident;
    type_base_t type;
} lir_symbol_var_t; // 外部符号引用, 外部符号引用

typedef struct {
    union {
        uint64_t int_value; // 8bit, 负数使用补码存储
        // TODO 扩充更多的类型
        double float_value; // 8bit = c.double
        bool bool_value; // 1bit
        string string_value; // 8bit
    };
    type_base_t type;
} lir_imm_t;

/**
 * 四元组
 * add first second -> result
 * move first -> result // a = 12
 * 例如
 * call sum.n 12, 14 // 指令是 call
 * first param 是函数名称（label）
 * second param 是函数参数，函数调用并不产生新的变量，因此没必要放在 result 中
 * 原则上会新增变量的放在 result,使用变量放在 first/second
 *
 * label: 同样也是使用 first_param
 */
typedef struct lir_op {
    lir_opcode_e code;
    lir_operand_t *first; // 参数1
    lir_operand_t *second; // 参数2
    lir_operand_t *output; // 参数3
    int id; // 编号, 也就是寄存器分配期间的 position, 一般都是顺序编码的
} lir_op_t;

lir_operand_t *set_indirect_addr(lir_operand_t *operand);

lir_operand_t *lir_new_phi_body(closure_t *c, lir_var_t *var, uint8_t count);

basic_block_t *lir_new_basic_block(char *name, uint8_t label_index);

//string lir_label_to_string(uint8_t label);

closure_t *lir_new_closure(ast_closure_t *ast);

/**
 * 符号使用
 * @param ident
 * @param type
 * @return
 */
lir_var_t *lir_new_var_operand(closure_t *c, string ident);

/**
 * 符号定义
 * 定义一个 local var operand
 * @param ident
 * @param type
 */
lir_var_decl_t *lir_new_var_decl(closure_t *c, string ident, type_t type);

type_base_t lir_operand_type_base(lir_operand_t *operand);

uint8_t lir_operand_sizeof(lir_operand_t *operand);

lir_operand_t *lir_new_temp_var_operand(closure_t *c, type_t type);

lir_operand_t *lir_new_empty_operand();

lir_operand_t *lir_new_addr_operand(lir_operand_t *base, int offset, type_base_t type_base);

lir_operand_t *lir_new_label_operand(string ident, bool is_local);

lir_op_t *lir_op_label(string name, bool is_local);

lir_op_t *lir_op_unique_label(string name);

lir_operand_t *lir_copy_label_operand(lir_operand_t *label_operand);

lir_op_t *lir_op_bal(lir_operand_t *label);

//lir_op *lir_new_push(lir_operand *operand);
lir_op_t *lir_op_move(lir_operand_t *dst, lir_operand_t *src);

lir_op_t *lir_op_new(lir_opcode_e code, lir_operand_t *first, lir_operand_t *second, lir_operand_t *result);

lir_op_t *lir_op_builtin_call(string name, lir_operand_t *result, int arg_count, ...);

lir_op_t *lir_op_runtime_call(string name, lir_operand_t *result, int arg_count, ...);

lir_op_t *lir_op_call(string name, lir_operand_t *result, int arg_count, ...);

bool lir_blocks_contains(slice_t *blocks, uint8_t label);

bool lir_op_is_branch(lir_op_t *op);

bool lir_op_is_call(lir_op_t *op);

bool lir_op_contain_cmp(lir_op_t *op);

bool lir_op_is_branch_cmp(lir_op_t *op);

/**
 * 从 operand 中提取 vars 列表，用于 ssa operand var 改写, 以及寄存器分配
 * @param operand
 * @return
 */
slice_t *lir_operand_vars(lir_operand_t *operand);

bool lir_operand_equal(lir_operand_t *a, lir_operand_t *b);

slice_t *lir_nest_operands(lir_operand_t *operand, uint64_t flag);

slice_t *lir_input_operands(lir_op_t *op, uint64_t flag);

slice_t *lir_output_operands(lir_op_t *op, uint64_t flag);

slice_t *lir_op_nest_operands(lir_op_t *op, uint64_t flag);

void lir_init();

#endif //NATURE_SRC_LIR_H_
