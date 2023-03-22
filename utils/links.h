#ifndef NATURE_LINKS_H
#define NATURE_LINKS_H

#include "utils/type.h"
#include "utils/ct_list.h"


// 由 linker 传递到 runtime,其中最重要的莫过于符号所在的虚拟地址
// 以及符号的内容摘要
// 同理目前 nature 最大的符号就是 8byte
typedef struct {
    uint64_t size;
    addr_t base; // data 中的数据对应的虚拟内存中的地址(通常在 .data section 中)
    bool need_gc; // 符号和栈中的 var 一样最大的值不会超过 8byte,所以使用 bool 就可以判断了
} symdef_t;

typedef struct {
    addr_t base; // text 虚拟地址起点
    addr_t end; // text 虚拟地址终点
    int64_t stack_size; // 基于当前函数 frame 占用的栈的大小(主要包括 args 和 locals，不包括 prev rbp 和 return addr)
    uint8_t *gc_bits; // 基于 stack_size 计算出的 gc_bits
} fndef_t;

// ct = compile time
//symdef_t *ct_symdefs;
//uint64_t ct_symdefs_size;
// 预处理时使用的临时数据
//fndef_t *ct_fndefs;
//uint64_t ct_fndefs_size; // 对 fndef 进行序列化后的 byte 数，主要是包含 gc_bits 的数量

void *fn_main_base_data_ptr; // 在 elf output 之前，都可以直接通过修改到 data_section->data 中的数据


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
extern symdef_t *rt_symdef_data;

extern uint64_t rt_fndef_count;
extern fndef_t *rt_fndef_data; // 仅需要修复一下 gc_bits 数据即可

extern uint64_t rt_rtype_count;
extern rtype_t *rt_rtype_data;


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

/**
 * 反序列化
 * @param data
 * @param count 入参时是 byte 的数量，出参时是 ct_reflect_type 的数量
 * @return
 */
//void rtypes_deserialize();

uint64_t pre_fndef_list();

uint64_t pre_symdef_list();

#endif //NATURE_LINKS_H
