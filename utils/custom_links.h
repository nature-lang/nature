#ifndef NATURE_CUSTOM_LINKS_H
#define NATURE_CUSTOM_LINKS_H

#include "ct_list.h"
#include "type.h"
#include "src/types.h"
#include "utils/sc_map.h"

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
    uint64_t line;
    uint64_t column;
    char name[80]; // 函数名称
    char rel_path[256]; // 文件路径 TODO 可以像 elf 文件一样做 str 段优化。
    uint8_t *gc_bits; // 基于 stack_offset 计算出的 gc_bits TODO 做成数据段优化
} fndef_t;

typedef struct {
    uint64_t offset; // 从 fn label 开始计算的 offset (跳过当前 call)
    uint64_t line; // line
    uint64_t column; // column
    void *data; // fn_base 对应的 fn_name, 只占用 8byte 的地址数据, collect 收集完成时会被替换成 fndef list 中对应的 index
    char target_name[24]; // 调试使用
} caller_t;


// gc 基于此进行全部符号的遍历
// 连接器传输到 runtime 中的符号数据
#define SYMBOL_FN_MAIN_BASE "rt_fn_main_base"

#define SYMBOL_SYMDEF_COUNT  "rt_symdef_count"
#define SYMBOL_SYMDEF_DATA "rt_symdef_data"

#define SYMBOL_FNDEF_COUNT  "rt_fndef_count"
#define SYMBOL_FNDEF_DATA  "rt_fndef_data"

#define SYMBOL_CALLER_COUNT "rt_caller_count"
#define SYMBOL_CALLER_DATA "rt_caller_data"

#define SYMBOL_RTYPE_COUNT "rt_rtype_count"
#define SYMBOL_RTYPE_DATA "rt_rtype_data"


extern addr_t rt_fn_main_base;

extern uint64_t rt_symdef_count;
extern symdef_t rt_symdef_data; // &rt_symdef_data 指向 .data.symdef section 所在地址
extern symdef_t *rt_symdef_ptr;

extern uint64_t rt_fndef_count;
extern fndef_t rt_fndef_data;
extern fndef_t *rt_fndef_ptr;
extern struct sc_map_64v rt_fndef_cache;

extern uint64_t rt_caller_count;
extern caller_t rt_caller_data;
extern caller_t *rt_caller_ptr;
extern struct sc_map_64v rt_caller_map;


extern uint64_t rt_rtype_count;
extern rtype_t rt_rtype_data;

// - symdef
extern uint64_t ct_symdef_size; // 数量
extern uint8_t *ct_symdef_data; // 序列化后的 data 大小
extern uint64_t ct_symdef_count;
extern symdef_t *ct_symdef_list;

// - fndef
extern uint64_t ct_fndef_size;
extern uint8_t *ct_fndef_data;
extern uint64_t ct_fndef_count;
extern fndef_t *ct_fndef_list;

// - caller
extern uint8_t *ct_caller_data;
extern list_t *ct_caller_list;

// - rtype
extern uint64_t ct_rtype_count; // 从 list 中提取而来
extern uint8_t *ct_rtype_data;
extern uint64_t ct_rtype_size; // rtype + gc_bits + element_kinds 的总数据量大小, sh_size 预申请需要该值，已经在 reflect_type 时计算完毕
extern list_t *ct_rtype_list;
extern table_t *ct_rtype_table; // 避免 rtype_vec 重复写入

// 主要是需要处理 gc_bits 数据
static inline uint8_t *fndefs_serialize() {
    // 按 count 进行一次序列化，然后将 gc_bits 按顺序追加
    uint8_t *data = mallocz(ct_fndef_size);

    uint8_t *p = data;
    // 首先将 fndef 移动到 data 中
    uint64_t size = ct_fndef_count * sizeof(fndef_t);
    memmove(p, ct_fndef_list, size);

    // 将 gc_bits 移动到数据尾部
    p = p + size; // byte 类型，所以按字节移动
    for (int i = 0; i < ct_fndef_count; ++i) {
        fndef_t *f = &ct_fndef_list[i];
        uint64_t gc_bits_size = calc_gc_bits_size(f->stack_size, POINTER_SIZE);
        memmove(p, f->gc_bits, gc_bits_size);
        p += gc_bits_size;
    }

    return data;
}


// 由于不包含 gc_bits，所以可以直接使用 ct_symdef_list 生成 symdef_data
static inline uint8_t *symdefs_serialize() {
    return (uint8_t *) ct_symdef_list;
}


// 定长数据，直接进行序列化
static inline uint8_t *callers_serialize() {
    // 更新 rt_fndef_index
    for (int i = 0; i < ct_caller_list->length; i++) {
        caller_t *caller = ct_list_value(ct_caller_list, i);
        assert(caller->data);
        caller->data = (void *) ((closure_t *) caller->data)->rt_fndef_index;
    }

    uint64_t size = sizeof(caller_t) * ct_caller_list->length;
    if (size == 0) {
        return NULL;
    }
    uint8_t *data = mallocz(size);
    memmove(data, ct_caller_list->take, size);

    return data;
}

/**
 * 将 reflect_types 进行序列化,序列化后的 byte 总数就是 ct_rtype_size
 * @return
 */
static uint8_t *rtypes_serialize() {
    // 按 count 进行一次序列化，然后将 gc_bits 按顺序追加
    // 计算 ct_reflect_type
    uint8_t *data = mallocz(ct_rtype_size);
    uint8_t *p = data;

    // rtypes 整体一次性移动到 data 中，随后再慢慢移动 gc_bits
    uint64_t size = ct_rtype_list->length * sizeof(rtype_t);
    memmove(p, ct_rtype_list->take, size);

    // 移动 gc_bits
    p = p + size; // byte 类型，所以按字节移动
    for (int i = 0; i < ct_rtype_list->length; ++i) {
        rtype_t *r = ct_list_value(ct_rtype_list, i); // take 的类型是字节，所以这里按字节移动
        uint64_t gc_bits_size = calc_gc_bits_size(r->size, POINTER_SIZE);
        if (gc_bits_size) {
            memmove(p, r->malloc_gc_bits, gc_bits_size);
        }
        p += gc_bits_size;
    }

    // 移动 element_hashes
    for (int i = 0; i < ct_rtype_list->length; ++i) {
        rtype_t *r = ct_list_value(ct_rtype_list, i);

        // array 占用了 length 字段，但是程element_hashes 是没有值的。
        if (r->length > 0 && r->element_hashes) {
            memmove(p, r->element_hashes, r->length * sizeof(uint64_t));
        }

        p += r->length * sizeof(uint64_t);
    }

    return data;
}


#endif //NATURE_CUSTOM_LINKS_H
