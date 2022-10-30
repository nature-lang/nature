#ifndef NATURE_STRUCTS_H
#define NATURE_STRUCTS_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "utils/list.h"
#include "utils/slice.h"
#include "symbol.h"


typedef struct {
    char *source;
    char *current;
    char *guard;
    int length;
    int line; // 当前所在代码行，用于代码报错提示

    bool has_newline;
    char space_prev;
    char space_next;
} scanner_cursor_t;

typedef struct {
    bool has;
    char *message;
} scanner_error_t;


typedef struct {
    list_node *current;
} parser_cursor_t;

typedef struct {
    symbol_type type;
    void *decl; // ast_var_decl,ast_type_decl_stmt,ast_new_fn
    string ident; // 原始名称
    string unique_ident; // 唯一名称
    int scope_depth;
    bool is_capture; // 是否被捕获(是否被下级引用)
} analysis_local_ident_t;

/**
 * free_var 是在 parent function 作用域中被使用,但是被捕获存放在了 current function free_vars 中,
 * 所以这里的 is_local 指的是在 parent 中的位置
 * 如果 is_local 为 true 则 index 为 parent.locals[index]
 * 如果 is_local 为 false 则 index 为参数 env[index]
 */
typedef struct {
    bool is_local;
    uint8_t index;
    string ident;
} analysis_free_ident_t;

typedef struct analysis_local_scope_t {
    struct analysis_local_scope_t *parent;
    slice_t *idents; // analysis_local_ident_t*

    uint8_t scope_depth;
} analysis_local_scope_t;

/**
 * 词法作用域
 */
typedef struct analysis_function_t {
    struct analysis_function_t *parent;

    analysis_local_scope_t *current_scope;

//  analysis_local_ident_t *locals[UINT8_MAX];
//  uint8_t local_count;

    // wwh: 使用了当前作用域之外的变量
    analysis_free_ident_t frees[UINT8_MAX];
    uint8_t free_count;

    // 当前函数内的块作用域深度(基于当前函数,所以初始值为 0, 用于块作用域判定)
    uint8_t scope_depth;

    // 便于值改写, 放心 env unique as 会注册到字符表的要用
    string env_unique_name;

    // 函数定义在当前作用域仅加载 function as
    // 函数体的解析则延迟到当前作用域内的所有标识符都定义明确好
    struct {
        // 由于需要延迟处理，所以缓存函数定义时的 scope，在处理时进行还原。
        analysis_local_scope_t *scope;
        union {
            ast_stmt *stmt;
            ast_expr *expr;
        };
        bool is_stmt;
    } contains_fn_decl[UINT8_MAX];
    uint8_t contains_fn_count;
} analysis_function_t;

/**
 * 可以理解为文件维度数据
 * path 基于 import 编译， import 能提供完整的 full_path 以及 module_name
 * Target district
 */
typedef struct {
    char *source; // 文件内容
    char *source_path; // 文件完整路径(外面丢进来的)
    char *source_dir; // 文件所在目录,去掉 xxx.n
//    string namespace; // is dir, 从 base_ns 算起的 source_dir
    string module_unique_name; // 符号表中都使用这个前缀 /code/nature/foo/bar.n => unique_name: nature/foo/bar

    bool entry; // 入口

    scanner_cursor_t s_cursor;
    scanner_error_t s_error;
    list *token_list; // scanner 结果

    parser_cursor_t p_cursor;
    slice_t *stmt_list;

    // analysis
    analysis_function_t *analysis_current;
    int analysis_line;

    // call init stmt
    ast_stmt *call_init_stmt;  // analysis 阶段写入

    // TODO asm_var_decls to init closures? 这样就只需要 compiler closures 就行了
    // 分析阶段(包括 closure_t 构建,全局符号表构建), 根据是否为 main 生成 import/symbol/asm_var_decls(symbol)/closure_decls
    slice_t *imports; // import_t, 图遍历 imports
    table_t *import_table; // 使用处做符号改写使用

    // 对外全局符号 -> 三种类型 var/fn/type_decl
    slice_t *symbols; // symbol_t, 这里只存储全局符号

    // infer 阶段得到
    slice_t *ast_closures; // 全局的或者非全局的都在这里了

    // compiler 阶段得到
    slice_t *closures; // 包含 lir

    // native -> opcodes
    int asm_temp_var_decl_count;
    slice_t *asm_operations; // 和架构相关
    slice_t *asm_var_decls; // 和架构无关

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
    slice_t *live_in; // ssa 阶段计算的精确 live in 一个变量如果在当前块被使用，或者再当前块的后继块中被使用，则其属于入口活跃
    slice_t *live; // reg alloc 阶段计算
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
    slice_t *formal_params; // 也是为了堆栈分配

    int stack_slot; // 初始值为 0，用于寄存器 slot 分配

    // loop collect
    int8_t loop_count;
    slice_t *loop_headers;
    slice_t *loop_ends;

    // refer module
    slice_t *asm_operations; // 和架构相关
    slice_t *asm_var_decls; // 和架构无关
    module_t *module;
} closure_t;

#endif //NATURE_STRUCTS_H
