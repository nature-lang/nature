#ifndef NATURE_SRC_LIR_H_
#define NATURE_SRC_LIR_H_

#include "utils/list.h"
#include "utils/helper.h"
#include "src/value.h"
#include "src/ast.h"
#include "utils/table.h"
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

#define OP(_node) ((lir_op_t*)_node)

#define LIR_NEW_IMMEDIATE_OPERAND(operand_type, key, val) \
({                                               \
   lir_operand_immediate *imm_operand = malloc(sizeof(lir_operand_immediate)); \
   imm_operand->type = operand_type; \
   imm_operand->key = val; \
   lir_operand *operand = malloc(sizeof(lir_operand)); \
   operand->type = LIR_OPERAND_IMM; \
   operand->value = imm_operand;              \
   operand; \
})

#define LIR_NEW_OPERAND(_type, _value) \
({                                 \
  lir_operand *_operand = NEW(lir_operand); \
  _operand->type = _type;           \
  _operand->value = _value;    \
  _operand;                                   \
})

#define LIR_NEW_VAR_OPERAND(_ident) \
({                                 \
  lir_operand_var *_var = NEW(lir_operand_var); \
  _var->old = _ident;    \
  _var->ident = _ident;             \
  _var;                                   \
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
    LIR_OPERAND_NULL,
    LIR_OPERAND_VAR, // 虚拟寄存器? 那我凭什么给虚拟寄存器分配内存地址？又或者是 symbol?
    LIR_OPERAND_REG,
    LIR_OPERAND_SYMBOL_VAR, // 虚拟寄存器? 那我凭什么给虚拟寄存器分配内存地址？
    LIR_OPERAND_STACK,
    LIR_OPERAND_PHI_BODY,
    LIR_OPERAND_FORMAL_PARAM,
    LIR_OPERAND_ACTUAL_PARAM,
    LIR_OPERAND_SYMBOL_LABEL, // 指令里面都有 label 指令了，operand 其实只需要 symbol 就行了，没必要多余的 label 误导把？
    LIR_OPERAND_IMM,
    LIR_OPERAND_ADDR,
} lir_operand_type;

typedef enum {
    LIR_OPCODE_ADD = 1,
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
    LIR_OPCODE_PUSH,
    LIR_OPCODE_POP,
    LIR_OPCODE_CALL, // 复合指令，位置在 second
    LIR_OPCODE_RUNTIME_CALL,
    LIR_OPCODE_BUILTIN_CALL, // BUILTIN_CALL print params -> nil
    LIR_OPCODE_RETURN, // return != ret, 其主要是做了 mov res -> rax
    LIR_OPCODE_LABEL,
    LIR_OPCODE_FN_BEGIN, // 无操作数
    LIR_OPCODE_FN_END, // 无操作数
} lir_opcode;

typedef struct lir_operand {
    lir_operand_type type;
    void *value;
} lir_operand;

//typedef struct {
//  string ident;
//} lir_operand_reg;

// 变量的定义点
typedef struct {
    string ident;
    type_t type; // 原始类型存储(包含指针深度等数据)
//    int16_t *stack_offset; // 可正可负, 对应 rbp-8 或者 rbp+8
} lir_var_decl;

/**
 * 存放在寄存器或者内存中, var a = 1
 */
typedef struct {
    string ident; // ssa 后的新名称
    string old; // ssa 之前的名称
//    uint8_t reg_index; // reg list index, 寄存器分配, 及时是同一个变量,也会有时在寄存器中,有时在内存中
    lir_var_decl *decl; // local 如果为 nil 就是外部符号引用
    type_base_t type_base;// lir 为了保证通用性，只能有类型，不能有 size, 该类型也决定了分配的寄存器的类型，已经 stack slot 的 size
    bool indirect_addr; // &a 对变量进行解引用操作
} lir_operand_var;

/**
 * mov DWORD 0x1,[rbp-8] 假设 rbp = 100, 则表示将 0x1 存储在 92 ~ 96 之间
 * 也就是 stack 是向下增长，但是存储数据是从低地址往搞地址存储
 */
typedef struct {
    int slot;
    int size;
} lir_operand_stack;

typedef struct {
    lir_operand *base;
    int offset; // 偏移量是可以计算出来的, 默认为 0
    type_base_t type_base;// lir 为了保证通用性，只能有类型，不能有 size
    bool indirect_addr;
} lir_operand_addr;

typedef struct {
    char *ident;
    bool is_local; // 是否为局部符号, 否则就是 global, 可以被链接器链接
} lir_operand_symbol_label;

typedef struct {
    string ident;
    type_base_t type;
} lir_operand_symbol_var; // 外部符号引用, 外部符号引用

typedef struct {
    union {
        uint64_t int_value; // 8bit, 负数使用补码存储
        double float_value; // 8bit = c.double
        bool bool_value; // 1bit
        string string_value; // 8bit
    };
    type_base_t type;
} lir_operand_immediate;

typedef struct {
    slice_t *vars;
    uint8_t count;
} lir_operand_formal_param;

typedef struct {
    lir_operand *list[UINT8_MAX];
    uint8_t count;
} lir_operand_actual_param;


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
    lir_opcode code;
    lir_operand *first; // 参数1
    lir_operand *second; // 参数2
    lir_operand *output; // 参数3
    int id; // 编号, 也就是寄存器分配期间的 position, 一般都是顺序编码的
} lir_op_t;

/**
 * 遍历期间，block 第一次被访问时打上 visited 标识
 * 当 block 的所有 succs 被遍历后，该块同时被标记为 active
 * visited 在 iteration 期间不会被清除,而 active 标志则会在处理完所有后继后清除？
 */
typedef struct {
    bool visited;
    bool active;
    bool header;
    bool end;
    bool index_map[INT8_MAX]; // 默认都是 false
    int8_t index; // 默认值为 -1， 标识不在循环中 block maybe in multi loops，index is unique number in innermost(最深的) loop
    uint8_t depth; // block 的嵌套级别,数字越高嵌套的越深
} loop_t;

typedef struct basic_block_t {
    uint8_t id; // label 标号, 基本块编号(可以方便用于数组索引)， 和 op_label 还是要稍微区分一下,
    string name;

    // op point
//    list_node *phi; // fist_node 即可
    list_node *first_op; // 真正的指令开始,在插入 phi 和 label 之前的指令开始位置
    list_node *last_op; // last_node 即可
    list *operations;

    slice_t *preds;
    slice_t *succs;
    slice_t *forward_succs; // 当前块正向的 succ 列表
    struct basic_block_t *backward_succ; // loop end
    uint8_t incoming_forward_count; // 正向进入到该节点的节点数量
    slice_t *loop_ends; // 仅 loop header 有这个值

    slice_t *use;
    slice_t *def;
    slice_t *live_out;
    slice_t *live_in; // 一个变量如果在当前块被使用，或者再当前块的后继块中被使用，则其属于入口活跃
    slice_t *dom; // 当前块被哪些基本块支配
    slice_t *df;
    slice_t *be_idom; // 哪些块已当前块作为最近支配块,其组成了支配者树
    struct basic_block_t *idom; // 当前块的最近支配者

    // loop detection
    loop_t loop;
} basic_block_t;

/**
 * 1. cfg 需要专门构造一个结尾 basic block 么，用来处理函数返回值等？其一定位于 blocks[count - 1]
 * 形参有一条专门的指令 lir_formal_param 编译这条指令的时候处理形参即可
 * lir_formal_param 在寄存器分配阶段已经分配了合适的 stack or reg, 所以依次遍历处理即可
 * 如果函数的返回值大于 8 个字节，则需要引用传递返回, ABI 规定形参 1 为引用返回地址
 * 假如形参和所有局部变量占用的总长为 N Byte, 那么 -n(rbp) 处存储的就是最后一个形参的位置(向上存储)
 * 所以还是需要 closure_t 增加一个字段记录大值地址响应值, 从而可以正常返回
 *
 * 2. closure_t 可以能定义在文件中的全局函数，也可能是定义在结构体中的局部函数，在类型推导阶段是有能力识别到函数的左值
 * 是一个变量，还是结构体的元素
 */
typedef struct closure_t {
    slice_t *globals; // closure_t 中定义的变量列表, 用于 ssa 构建, 以及寄存器分配时的 interval 也是基于次
    slice_t *fixed_regs; // 作为临时寄存器使用到的寄存器
    slice_t *blocks; // 根据解析顺序得到

    basic_block_t *entry; // 基本块入口, 指向 blocks[0]
    table_t *interval_table; // key 包括 fixed register as 和 variable.ident
    int interval_count; // 虚拟寄存器的偏移量 从 40 开始算，在这之前都是物理寄存器

    // 定义环境
    string name;
    string end_label; // 结束地址
    string env_name;
    struct closure_t *parent;
    list *operations; // 指令列表

    table_t *var_decl_table; // 主要是用于栈分配, 需要 hash 表查找(但是该结构不适合遍历), 形参和局部变量都在这里定义
    list *var_decls; // 只为了堆栈分配(形参的需要单独处理，就别写进来了)
    list *formal_params; // 也是为了堆栈分配

    int stack_slot; // 初始值为 0，用于寄存器 slot 分配

    // loop collect
    int8_t loop_count;
    slice_t *loop_headers;
    slice_t *loop_ends;
} closure_t;

lir_operand *set_indirect_addr(lir_operand *operand);

lir_operand *lir_new_phi_body(lir_operand_var *var, uint8_t count);

basic_block_t *lir_new_basic_block(char *name, uint8_t label_index);

//string lir_label_to_string(uint8_t label);

closure_t *lir_new_closure(ast_closure_t *ast);

/**
 * 符号使用
 * @param ident
 * @param type
 * @return
 */
lir_operand_var *lir_new_var_operand(closure_t *c, string ident);

/**
 * 符号定义
 * 定义一个 local var operand
 * @param ident
 * @param type
 */
lir_var_decl *lir_new_var_decl(closure_t *c, string ident, type_t type);

type_base_t lir_operand_type_base(lir_operand *operand);

uint8_t lir_operand_sizeof(lir_operand *operand);

lir_operand *lir_new_temp_var_operand(closure_t *c, type_t type);

lir_operand *lir_new_empty_operand();

lir_operand *lir_new_addr_operand(lir_operand *base, int offset, type_base_t type_base);

lir_operand *lir_new_label_operand(string ident, bool is_local);

lir_operand_actual_param *lir_new_actual_param();

lir_op_t *lir_op_label(string name, bool is_local);

lir_op_t *lir_op_unique_label(string name);

lir_operand *lir_copy_label_operand(lir_operand *label_operand);

lir_op_t *lir_op_bal(lir_operand *label);

//lir_op *lir_new_push(lir_operand *operand);
lir_op_t *lir_op_move(lir_operand *dst, lir_operand *src);

lir_op_t *lir_op_new(lir_opcode code, lir_operand *first, lir_operand *second, lir_operand *result);

lir_op_t *lir_op_builtin_call(string name, lir_operand *result, int arg_count, ...);

lir_op_t *lir_op_runtime_call(string name, lir_operand *result, int arg_count, ...);

lir_op_t *lir_op_call(string name, lir_operand *result, int arg_count, ...);

bool lir_blocks_contains(slice_t *blocks, uint8_t label);

bool lir_op_is_branch(lir_op_t *op);

bool lir_op_is_call(lir_op_t *op);

/**
 * 从 operand 中提取 vars 列表，用于 ssa operand var 改写, 以及寄存器分配
 * @param operand
 * @return
 */
slice_t *lir_operand_vars(lir_operand *operand);

bool lir_operand_equal(lir_operand *a, lir_operand *b);

slice_t *lir_operand_nests(lir_operand *operand, uint64_t flag);

slice_t *lir_op_nest_operands(lir_op_t *op, uint64_t flag);

#endif //NATURE_SRC_LIR_H_
