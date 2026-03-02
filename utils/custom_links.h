#ifndef NATURE_CUSTOM_LINKS_H
#define NATURE_CUSTOM_LINKS_H

#include "ct_list.h"
#include "src/types.h"
#include "type.h"
#include "utils/sc_map.h"

/**
 * 这里存储了 nature 所有全局变量
 */
typedef struct {
    // base 一定要放在第一个位置，这关乎到能否争取进行重定位
    addr_t base; // data 中的数据对应的虚拟内存中的地址(通常在 .data section 中)
    int64_t size;
    int64_t hash;
    int64_t name_offset;
} symdef_t;

typedef struct {
    addr_t base; // text 虚拟地址起点,在 .data.fndef 段中占有 8byte 段空间，通过重定位补齐
    uint64_t size; // 这里的 size 是 fn 编译成二进制后占用的空间的大小
    int64_t stack_size; // 基于当前函数 frame 占用的栈的大小(主要包括 args 和 locals，不包括 prev rbp 和 return addr)
    uint64_t line;
    uint64_t column;

    uint64_t name_offset;
    uint64_t relpath_offset; // 文件路径
    uint64_t gc_bits_offset; // uint64_t gc_bits_size = calc_gc_bits_size(f->stack_size, POINTER_SIZE);
} fndef_t;

typedef struct {
    uint64_t offset; // 从 fn label 开始计算的 offset (跳过当前 call)
    uint64_t line; // line
    uint64_t column; // column
    void *data; // fn_base 对应的 fn_name, 只占用 8byte 的地址数据, collect 收集完成时会被替换成 fndef list 中对应的 index
    uint64_t target_name_offset;
} caller_t;


// gc 基于此进行全部符号的遍历
// 连接器传输到 runtime 中的符号数据
#define SYMBOL_STRTABLE_DATA "rt_strtable_data"

#define SYMBOL_DATA "rt_data"

#define SYMBOL_SYMDEF_COUNT "rt_symdef_count"
#define SYMBOL_SYMDEF_DATA "rt_symdef_data"

#define SYMBOL_FNDEF_COUNT "rt_fndef_count"
#define SYMBOL_FNDEF_DATA "rt_fndef_data"

#define SYMBOL_CALLER_COUNT "rt_caller_count"
#define SYMBOL_CALLER_DATA "rt_caller_data"

#define SYMBOL_RTYPE_COUNT "rt_rtype_count"
#define SYMBOL_RTYPE_DATA "rt_rtype_data"
#define SYMBOL_MAIN_IS_FN "main_is_fn"


extern addr_t rt_fn_main_base;

extern char rt_strtable_data; // Put the symbol through the linker
extern char *rt_strtable_ptr;

#define STRTABLE(_offset) (rt_strtable_ptr + _offset)

extern uint8_t rt_data;
extern uint8_t *rt_data_ptr;

#define RTDATA(_offset) (rt_data_ptr + _offset)

extern uint64_t rt_symdef_count;
extern symdef_t rt_symdef_data; // &rt_symdef_data 指向 .data.symdef section 所在地址
extern symdef_t *rt_symdef_ptr;

extern uint64_t rt_fndef_count;
extern fndef_t rt_fndef_data;
extern fndef_t *rt_fndef_ptr;

extern uint64_t rt_caller_count;
extern caller_t rt_caller_data;
extern caller_t *rt_caller_ptr;
extern struct sc_map_64v rt_caller_map;

extern uint64_t rt_rtype_count;
extern rtype_t rt_rtype_data;

// - strtable
extern uint64_t ct_strtable_len;
extern uint64_t ct_strtable_cap;
extern char *ct_strtable_data;
extern struct sc_map_s64 ct_startable_map;

// - data
extern uint64_t ct_data_len;
extern uint64_t ct_data_cap;
extern uint8_t *ct_data;

#define CTDATA(_offset) (ct_data + _offset)

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

    // 首先将 fndef 移动到 data 中
    uint64_t size = ct_fndef_count * sizeof(fndef_t);
    memmove(data, ct_fndef_list, size);
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

    // rtypes 整体一次性移动到 data 中，随后再慢慢移动 gc_bits
    uint64_t size = ct_rtype_list->length * sizeof(rtype_t);
    memmove(data, ct_rtype_list->take, size);

    return data;
}

static inline uint64_t strtable_put(char *str) {
    // 首次初始化检查
    if (ct_strtable_cap == 0) {
        // 初始化字符串表数据
        ct_strtable_cap = 1024; // 初始容量
        ct_strtable_data = mallocz(ct_strtable_cap);
        ct_strtable_len = 0;

        sc_map_init_s64(&ct_startable_map, 0, 0);
    }

    // 使用 strstr 检查字符串是否已存在
    uint64_t offset = sc_map_get_s64(&ct_startable_map, str);
    if (sc_map_found(&ct_startable_map)) {
        return offset;
    }

    // 计算字符串长度（包括 \0 分隔符）
    uint64_t str_len = strlen(str) + 1;

    // 检查是否需要扩容
    if ((ct_strtable_len + str_len) > ct_strtable_cap) {
        // 需要扩容
        while (ct_strtable_len + str_len > ct_strtable_cap) {
            ct_strtable_cap *= 2;
        }

        ct_strtable_data = (char *) realloc(ct_strtable_data, ct_strtable_cap);
    }

    // 记录当前偏移量
    offset = ct_strtable_len;

    // 复制字符串到字符串表（包括 \0）
    memcpy(ct_strtable_data + offset, str, str_len);

    // 更新字符串表长度
    ct_strtable_len += str_len;

    return offset;
}

static inline uint64_t data_put(uint8_t *data, uint64_t len) {
    if (ct_data == NULL) {
        ct_data_cap = 1024;
        ct_data = mallocz(ct_data_cap);
        ct_data_len = 0;
    }

    // 检查是否需要扩容
    if ((ct_data_len + len) > ct_data_cap) {
        // 需要扩容
        while (ct_data_len + len > ct_data_cap) {
            ct_data_cap *= 2;
        }
        ct_data = realloc(ct_data, ct_data_cap);
        assert(ct_data);
    }

    // 复制数据到数据区
    if (data) {
        memcpy(ct_data + ct_data_len, data, len);
    } else {
        memset(ct_data + ct_data_len, 0, len);
    }

    // 更新数据长度
    uint64_t offset = ct_data_len;
    ct_data_len += len;

    return offset;
}

#endif //NATURE_CUSTOM_LINKS_H
