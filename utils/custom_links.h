#ifndef NATURE_CUSTOM_LINKS_H
#define NATURE_CUSTOM_LINKS_H

#include "utils/type.h"
#include "utils/ct_list.h"

/**
 * 这里存储了 nature 所有全局变量
 */
typedef struct {
    // base 一定要放在第一个位置，这关乎到能否争取进行重定位
    addr_t base; // data 中的数据对应的虚拟内存中的地址(通常在 .data section 中)
    uint64_t size;
    bool need_gc; // 符号和栈中的 var 一样最大的值不会超过 8byte,所以使用 bool 就可以判断了
    char name[80];
} symdef_t;

typedef struct {
    addr_t base; // text 虚拟地址起点,在 .data.fndef 段中占有 8byte 段空间，通过重定位补齐
    uint64_t size; // 这里的 size 是 fn 编译成二进制后占用的空间的大小
    uint64_t fn_runtime_reg; //  0 表示不在
    uint64_t fn_runtime_stack; // 0 就啥也不是，
    int64_t stack_size; // 基于当前函数 frame 占用的栈的大小(主要包括 args 和 locals，不包括 prev rbp 和 return addr)
    uint8_t *gc_bits; // 基于 stack_offset 计算出的 gc_bits
    char name[80];
} fndef_t;


// gc 基于此进行全部符号的遍历
// 连接器传输到 runtime 中的符号数据
#define SYMBOL_FN_MAIN_BASE "rt_fn_main_base"

#define SYMBOL_SYMDEF_COUNT  "rt_symdef_count"
#define SYMBOL_SYMDEF_DATA "rt_symdef_data"

#define SYMBOL_FNDEF_COUNT  "rt_fndef_count"
#define SYMBOL_FNDEF_DATA  "rt_fndef_data"

#define SYMBOL_RTYPE_COUNT "rt_rtype_count"
#define SYMBOL_RTYPE_DATA "rt_rtype_data"


extern addr_t rt_fn_main_base;

extern uint64_t rt_symdef_count;
extern symdef_t rt_symdef_data; // &rt_symdef_data 指向 .data.symdef section 所在地址
symdef_t *rt_symdef_ptr;

extern uint64_t rt_fndef_count;
extern fndef_t rt_fndef_data;
fndef_t *rt_fndef_ptr;

extern uint64_t rt_rtype_count;
extern rtype_t rt_rtype_data;
rtype_t *rt_rtype_ptr;


// - symdef
uint64_t ct_symdef_size; // 数量
byte *ct_symdef_data; // 序列化后的 data 大小
uint64_t ct_symdef_count;
symdef_t *ct_symdef_list;

// - fndef
uint64_t ct_fndef_size;
byte *ct_fndef_data;
uint64_t ct_fndef_count;
fndef_t *ct_fndef_list;


// - rtype
uint64_t ct_rtype_count; // 从 list 中提取而来
byte *ct_rtype_data;
uint64_t ct_rtype_size; // rtype + gc_bits 的总数据量大小, sh_size 预申请需要该值，已经在 reflect_type 时计算完毕
list_t *ct_rtype_list;
table_t *ct_rtype_table;

// 主要是需要处理 gc_bits 数据
byte *fndefs_serialize();

byte *symdefs_serialize();

/**
 * 将 reflect_types 进行序列化,序列化后的 byte 总数就是 ct_rtype_size
 * @return
 */
byte *rtypes_serialize();

#endif //NATURE_CUSTOM_LINKS_H
