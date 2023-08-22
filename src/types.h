#ifndef NATURE_TYPES_H
#define NATURE_TYPES_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "utils/linked.h"
#include "utils/slice.h"
#include "src/symbol/symbol.h"
#include "binary/elf/elf.h"
#include "utils/stack.h"
#include "package.h"

typedef uint64_t flag_t;

typedef enum {
    ALLOC_KIND_NOT = 1, // 不能分配寄存器, 例如 LEA 的左值
    ALLOC_KIND_MUST = 2, // 必须分配寄存器
    ALLOC_KIND_SHOULD = 3, // 尽量分配寄存器，但不强制
} alloc_kind_e;

typedef enum {
    LIR_FLAG_FIRST = 1,
    LIR_FLAG_SECOND,
    LIR_FLAG_OUTPUT,
    LIR_FLAG_ALLOC_INT,
    LIR_FLAG_ALLOC_FLOAT,
    LIR_FLAG_ALLOC_MUST, // 必须分配寄存器
    LIR_FLAG_ALLOC_SHOULD, // 可以分可以不分配
    LIR_FLAG_ALLOC_NOT, // 绝对不能分配寄存器
    LIR_FLAG_USE,
    LIR_FLAG_DEF,
    LIR_FLAG_INDIRECT_ADDR_BASE,
} lir_flag_t;

typedef enum {
    // 前端
    CT_STAGE_SCANNER = 1,
    CT_STAGE_PARSER,
    CT_STAGE_ANALYZER,
    CT_STAGE_GENERIC,
    CT_STAGE_CHECKING,
    CT_STAGE_LINEAR,
    CT_STAGE_CFG, // return check 是基于 cfg 的

    // 后端
    CT_STAGE_SSA,
    CT_STAGE_LOWER,
    CT_STAGE_REG_ALLOC,
    CT_STAGE_NATIVE, // 编程了汇编源语，不再受 closure 约束
} ct_stage;


typedef enum {
    MODULE_TYPE_MAIN = 1, // 入口文件
    MODULE_TYPE_COMMON = 2, // 标准 module
    MODULE_TYPE_TEMP = 3 // nt
} module_type_t;

typedef struct {
    char *name; // 符号名称
    size_t size; // 符号大小，单位 byte, 生成符号表的时候需要使用
    uint8_t *value; // 符号值
} asm_global_symbol_t;


typedef struct {
    string name;
    uint8_t index; // index 对应 intel 手册表中的索引，可以直接编译进 modrm 中
    uint8_t size; // 实际的位宽, 对应 intel 手册
    uint8_t alloc_id; // 寄存器分配期间的 id，能通过 id 进行唯一检索
    flag_t flag;
} reg_t; // 做类型转换

typedef struct {
    char *source;
    char *current;
    char *guard;
    int length;

    int line; // 当前所在代码行，用于代码报错提示
    int column; // 当前 scan 所在 column

    char space_prev;
    char space_next;
} scanner_cursor_t;

typedef struct {
    linked_node *current;
} parser_cursor_t;

/**
 * free_var 是在 parent function 作用域中被使用,但是被捕获存放在了 current function free_vars 中,
 * 所以这里的 is_local 指的是在 parent 中的位置
 * 如果 is_local 为 true 则 index 为 parent.locals[index]
 * 如果 is_local 为 false 则 index 为参数 env[index]
 */
typedef struct {
    bool is_local;
    int env_index; // env_index
    string ident;
    uint64_t index; // free in frees index
    symbol_type_t type;
} free_ident_t;

typedef struct {
    symbol_type_t type;
    void *decl; // ast_var_decl,ast_type_decl_stmt,ast_new_fn
    string ident; // 原始名称
    string unique_ident; // 唯一名称
    int depth; // 变量声明的深度，如果变量的 depth == depth 则说明同一作用域下重复声明
    bool is_capture; // 是否被捕获(是否被下级引用)
} local_ident_t;

/**
 * 词法作用域
 */
typedef struct analyzer_fndef_t {
    struct analyzer_fndef_t *parent;

    ast_fndef_t *fndef;
    slice_t *locals; // local_ident
    // 当前函数内的块作用域深度(基于当前函数,所以初始值为 0, 用于块作用域判定)
    uint8_t scope_depth;

    // 使用了当前函数作用域之外的变量
    slice_t *frees; // analyzer_free_ident_t*
    table_t *free_table; // analyzer_free_ident_t*
} analyzer_fndef_t;

typedef struct {
    ct_stage stage;
    int line;
    int column;
    char msg[1024];
} ct_error_t;

/**
 * 可以理解为文件维度数据
 * path 基于 import 编译， import 能提供完整的 full_path 以及 module_name
 * Target district
 */
typedef struct {
    char *source; // 文件内容
    char *source_path; // 文件完整路径(外面丢进来的)
    char *source_dir; // 文件所在目录,去掉 xxx.n
    string ident; // 符号表中都使用这个前缀 /code/nature/foo/bar.n => unique_name: nature/foo/bar

    // 用于 analyzer ident 时需要将 ident 改为 package.module 中的真实符号
    char *package_dir;
    toml_table_t *package_conf;

    // parser/analyzer/checking/compiler 阶段的所有异常都写入到这里
    slice_t *ct_errors;

//    bool entry; // 入口
    module_type_t type;

    scanner_cursor_t s_cursor;
    linked_t *token_list; // scanner 结果

    int var_unique_count; // 同一个 module 下到所有变量都会通过该 ident 附加唯一标识

    // parser
    parser_cursor_t p_cursor;

    slice_t *stmt_list;
    table_t *parser_type_formals; // 辅助记录 ident 是 alias 还是 type param

    // analyzer
    analyzer_fndef_t *analyzer_current;
    ast_fndef_t *analyzer_global;

    // 编译器前端统一使用
    int current_line;
    int current_column;

    // checking
    ast_fndef_t *checking_current; // 当前正在 checking 都 fn, return 时需要基于改值判断 return type
    table_t *type_args; // 临时存储 checking alias 时传递的实参(TODO 如果存在多层次嵌套，则需要使用栈来存储)

    // compiler
    struct closure_t *linear_current;
    int linear_line;

    // call init stmt
    ast_stmt_t *call_init_stmt;  // analyzer 阶段写入

    // 分析阶段(包括 closure_t 构建,全局符号表构建), 根据是否为 main 生成 import/symbol/asm_global_symbols(symbol)/closure_decls
    slice_t *imports; // ast_import_t, 图遍历 imports
    table_t *import_table; // 使用处做符号改写使用

    // 对外全局符号 -> 三种类型 var/fn/type_decl
    slice_t *global_symbols; // symbol_t, 这里只存储全局符号

    // ast_fndef
    slice_t *ast_fndefs;

    // closure_t
    slice_t *closures; // 包含 lir, 无论是 local 还是 global 都会在这里进行注册

    // native -> opcodes
    int asm_temp_var_decl_count;
    slice_t *asm_operations; // 和架构相关
    slice_t *asm_global_symbols; // 和架构无关

    // elf target.o
    uint64_t elf_count;
    uint8_t *elf_binary;
    string object_file;
} module_t;

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
    uint16_t id; // label 标号, 基本块编号(可以方便用于数组索引)， 和 op_label 还是要稍微区分一下,
    string name;

    // op pointer
//    linked_node *phi; // fist_node 即可
    linked_node *first_op; // 跳过了 phi 和 label 的指令开始位置
    linked_node *last_op; // last_node 即可
    linked_t *operations;

    slice_t *preds;
    slice_t *succs;
    slice_t *forward_succs; // 当前块正向的 succ 列表
    struct basic_block_t *backward_succ; // loop end
    uint8_t incoming_forward_count; // 正向进入到该节点的节点数量
    slice_t *loop_ends; // 仅 loop header 有这个值

    slice_t *use;
    slice_t *def;
    slice_t *live_out;
    slice_t *live_in; // ssa 阶段计算的精确 live in 一个变量如果在当前块被使用，或者再当前块的后继块中被使用，则其属于入口活跃
    slice_t *live; // reg alloc 阶段计算
    // employer
    slice_t *domers; // 当前块被哪些基本块管辖
    struct basic_block_t *imm_domer; // 当前块的直接(最近)支配者
    slice_t *df;
    // employee
    slice_t *imm_domees; // 当前块直接支配了那些块，也就是哪些块已当前块作为最近支配块,其组成了支配者树

    // loop detection
    loop_t loop;
} basic_block_t;


// 寄存器分配 ------------------------------------------------------------------------------------------------------
/**
 * 存放在寄存器或者内存中, var a = 1
 */
typedef struct {
    string ident; // ssa 后的新名称
    string old; // ssa 之前的名称

    flag_t flag;
    type_t type;
} lir_var_t;

typedef struct {
    int value;
    alloc_kind_e kind;
} use_pos_t;

typedef struct {
    int from; // 包含
    int to; // 不包含
} interval_range_t;

// interval 分为两种，一种是虚拟寄存器，一种是固定寄存器
typedef struct interval_t {
    int index; // 对应的 var 对应的 interval 编号，可能是物理寄存器，也可能是虚拟寄存器产生的 index
    interval_range_t *first_range;
    interval_range_t *last_range;
    linked_t *ranges;
    linked_t *use_pos_list; // 存储 use_position 列表
    struct interval_t *parent;
    linked_t *children; // 动态数组

    lir_var_t *var; // var 中存储着 stack slot

    int64_t *stack_slot; // slot 对栈帧顶部的偏移(0), 值向上增长，比如 stack_slot = -8, size = 8，表示值存储在 (top-8) ~ top
    bool spilled; // 当前 interval 是否是溢出状态,去 stack_slot 中找对应的插槽
    // 当有多个空闲 register 时，优先分配 hint 对应的 register
    struct interval_t *reg_hint;
    slice_t *phi_hints; // phi def interval 对应的多个 body interval,def interval 优先分配 body var 已经分配的寄存器
    uint8_t assigned; // 分配的 reg id, 通过 alloc_regs[assigned] 可以定位唯一寄存器

    lir_flag_t alloc_type; //    VR_FLAG_ALLOC_INT,VR_FLAG_ALLOC_FLOAT
    bool fixed; // 是否是物理寄存器所产生的 interval, index 对应物理寄存器的编号，通常小于 40
} interval_t;

typedef struct {
    slice_t *from_list;
    slice_t *to_list;
    basic_block_t *insert_block;
    int insert_id;
} resolver_t;

// lir ------------------------------------------------------------------------------------------------------
typedef enum {
    LIR_OPERAND_NULL = 0,
    LIR_OPERAND_VAR, // 虚拟寄存器? 那我凭什么给虚拟寄存器分配内存地址？又或者是 symbol?
    LIR_OPERAND_REG,
    LIR_OPERAND_SYMBOL_VAR, // 虚拟寄存器? 那我凭什么给虚拟寄存器分配内存地址？
    LIR_OPERAND_STACK,
    LIR_OPERAND_PHI_BODY,
    LIR_OPERAND_PARAMS,
    LIR_OPERAND_ARGS,
    LIR_OPERAND_SYMBOL_LABEL, // 指令里面都有 label 指令了，operand 其实只需要 symbol 就行了，没必要多余的 label 误导把？
    LIR_OPERAND_IMM,
    LIR_OPERAND_INDIRECT_ADDR,
    LIR_OPERAND_VARS, // 与 pyi_body, formals 一样都是 slice_t + lir_var
    LIR_OPERAND_REGS, // 与 pyi_body, formals 一样都是 slice_t + reg_t
    LIR_OPERAND_CLOSURE_VARS,  // 无法通过 extract 函数提取出来，也不是提取出来，仅仅是为了临时存储使用
} lir_operand_type_t;

typedef enum {
    LIR_OPCODE_ADD = 1,
    LIR_OPCODE_SUB,
    LIR_OPCODE_MUL,
    LIR_OPCODE_DIV,
    LIR_OPCODE_REM, // remainder
    LIR_OPCODE_NEG, // -取负数

    // 位运算
    LIR_OPCODE_SHR, // >>
    LIR_OPCODE_SHL, // <<
    LIR_OPCODE_AND, // &
    LIR_OPCODE_OR, // |
    LIR_OPCODE_XOR, // ^
    LIR_OPCODE_NOT, // 按位取反

    LIR_OPCODE_CLR, // clean reg
    LIR_OPCODE_CLV, // clean up var, result is var，等同于首次变量注册的功能
    LIR_OPCODE_SLT, // set less than <
    LIR_OPCODE_SLE, // set less eq <=
    LIR_OPCODE_SGT, // >
    LIR_OPCODE_SGE, // >=
    LIR_OPCODE_SEE, // ==
    LIR_OPCODE_SNE, // !=

    LIR_OPCODE_LEA, // 取地址, lea _,_ => v_1 (v_1 必须是有效的内存地址)
//    LIR_OPCODE_LIA, // load indirect addr to reg(var) ，将内存中的数据加载到寄存器中, amd64: mov [rax] -> rdx
//    LIR_OPCODE_SIA, // store reg(var) to indirect addr，将寄存器中的数据存入内存

    LIR_OPCODE_PHI, // 复合指令, 位置在 first_param
    LIR_OPCODE_MOVE,
    LIR_OPCODE_BEQ, // branch if eq a,b
    LIR_OPCODE_BAL, // branch always
    LIR_OPCODE_PUSH, // first
    LIR_OPCODE_POP, // output
    LIR_OPCODE_CALL, // 复合指令，位置在 second
    LIR_OPCODE_RT_CALL,
    LIR_OPCODE_RETURN, // return != ret, 其主要是做了 mov res -> rax
    LIR_OPCODE_LABEL,
    LIR_OPCODE_FN_BEGIN, // output 为 formals 操作数
    LIR_OPCODE_FN_END, // 无操作数

    LIR_OPCODE_NOP, // 空的，不做任何操作的指令，但是将用于 ssa 的完整 use-def

    LIR_OPCODE_ENV_CAPTURE,
    LIR_OPCODE_ENV_CLOSURE,
} lir_opcode_t;

typedef struct lir_operand_t lir_operand_t;

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
typedef struct lir_op_t {
    lir_opcode_t code;
    lir_operand_t *first; // 参数1
    lir_operand_t *second; // 参数2
    lir_operand_t *output; // 参数3
    int id; // 编号, 也就是寄存器分配期间的 position, 一般都是顺序编码的

    // TODO def_regs, 可以预定一些 reg
    // TODO use_regs
} lir_op_t;

/**
 * 1. cfg 需要专门构造一个结尾 basic block 么，用来处理函数返回值等？其一定位于 blocks[count - 1]
 * 形参有一条专门的指令 lir_formal 编译这条指令的时候处理形参即可
 * lir_formal 在寄存器分配阶段已经分配了合适的 stack or reg, 所以依次遍历处理即可
 * 如果函数的返回值大于 8 个字节，则需要引用传递返回, ABI 规定形参 1 为引用返回地址
 * 假如形参和所有局部变量占用的总长为 N Byte, 那么 -n(rbp) 处存储的就是最后一个形参的位置(向上存储)
 * 所以还是需要 closure_t 增加一个字段记录大值地址响应值, 从而可以正常返回
 *
 * 2. closure_t 可以能定义在文件中的全局函数，也可能是定义在结构体中的局部函数，在类型推导阶段是有能力识别到函数的左值
 * 是一个变量，还是结构体的元素
 */
typedef struct closure_t {
    // lir_var*, 寄存器分配将基于该 var 列表进行 interval 的创建
    // 这里的 def 不是变量定义，而是 ssa 中的定值
    slice_t *var_defs;
    slice_t *blocks; // 根据解析顺序得到, 在 linear scan 时进行了排序

    // ssa
    slice_t *ssa_globals; // 存活周期 >= 2 个 basic block 的 var, 基于次进行最小限度的放置 phi
    table_t *ssa_globals_table; // 存活周期 >= 2 个 basic block 的 var
    table_t *ssa_var_blocks; // linked_t* var def in blocks， 一个 var 可以在多个 block 中进行重新定值
    table_t *ssa_var_block_exists; // 是否已经添加过，避免 var+block_name 的重复添加

    table_t *closure_var_table;
    slice_t *closure_vars;

    basic_block_t *entry; // 基本块入口, 指向 blocks[0]
    table_t *interval_table; // key 包括 fixed register as 和 variable.ident
    int interval_count; // 虚拟寄存器的偏移量 从 40 开始算，在这之前都是物理寄存器

    // 定义环境
    char *symbol_name;
    char *closure_name;
    char *end_label; // 函数的结束地址 label
    char *error_label; //  遇到表达式错误时需要调整到的目标 label
    char *catch_error_label;

    ct_stack_t *for_start_labels; // 用于 for continue lir_operand*
    ct_stack_t *for_end_labels; // 用于 for break lir_operand*

    // lir_operand_t
    void *return_operand; // 返回结果，return 中如果有返回参数，则会进行一个 move 移动到该 result 中
    type_t type;

    linked_t *operations; // 指令列表

    // gc 使用
    int64_t stack_offset; // 用于栈区内存分配，基于 rbp 计算，值 > 0. rsp 在函数的入口点之前应该始终保持 16byte 对齐
    slice_t *stack_vars; // 与栈增长顺序一致,随着栈的增长而填入, 其存储的值为 *lir_var_t

    // runtime 参数可能保存在 stack 也可能保存在 reg 中。
    // 无论保存在哪里，其都是一个 8byte 的 pointer
    uint64_t fn_runtime_reg;
    uint64_t fn_runtime_stack;
    void *fn_runtime_operand; // lir_operand_t

    // loop collect
    int8_t loop_count;
    slice_t *loop_headers;
    slice_t *loop_ends;

    // refer module
    uint64_t text_count; // asm_operations 编译完成后占用的 count
    slice_t *asm_operations; // 和架构相关, 首个 opcode 一定是 label
    slice_t *asm_build_temps; // 架构相关编译临时
    slice_t *asm_symbols; // asm_global_symbol_t
    module_t *module;

    int line;
    int column;
} closure_t;

/**
 * 段表与相应的二进制数据合并
 */
typedef struct section_t {
    // Elf64_Shdr 原始字段继承
    uint64_t sh_name; // 段名称，段表字符串表 slot ~ \0
    uint64_t sh_type; // 段类型，
    uint64_t sh_flags;
    uint64_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
    uint64_t sh_size;
    uint64_t sh_offset;
    addr_t sh_addr; // 可重定位地址


    uint64_t data_count; // 数据位置
    uint64_t data_capacity; // 极限容量
    uint8_t *data; // 段二进制数据
    int sh_index; // 段表索引
    char name[50]; // 段表名称字符串冗余

    // 排序字段
    int actual_sh_index;
    int actual_sh_weight;
    uint64_t phdr_flags; // 第8位表示是否需要 PT_LOAD 装载到内存中

    struct section_t *link; // 部分 section 需要 link 其他字段, 如符号表的 link 指向字符串表
    struct section_t *relocate; // 当前段指向的的重定位段,如当前段是 text,则 cross_relocate 指向 .rela.text
    struct section_t *prev; // slice 中的上一个 section
} section_t;

typedef struct {
    uint64_t got_offset;
    uint64_t plt_offset;
    uint64_t plt_sym;
    int dyn_index;
} sym_attr_t;


typedef struct {
    slice_t *sections;
    slice_t *private_sections;
    table_t *symtab_hash; // 直接指向符号表 sym
    section_t *symtab_section;
    sym_attr_t *sym_attrs;
    uint64_t sym_attrs_count;
    section_t *bss_section;
    section_t *data_section;
    section_t *text_section;
//    section_t *rodata_section;
    section_t *got;
    section_t *plt;
    section_t *data_rtype_section;
    section_t *data_fndef_section;
    section_t *data_symdef_section;


    // 可执行文件构建字段
    Elf64_Phdr *phdr_list; // 程序头表
    uint64_t phdr_count; // 程序头表数量

    uint64_t file_offset;
    char *output; // 完整路径名称
    uint8_t output_type;
} elf_context;


#endif //NATURE_TYPES_H
