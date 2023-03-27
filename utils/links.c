#include "links.h"

#include "bitmap.h"
#include "src/symbol/symbol.h"
#include "src/lir/lir.h"

/**
 * 基于 symbol fn 生成基础的 fn list
 */
uint64_t pre_fndef_list() {
    ct_fndef_count = symbol_fn_list->count;
    uint64_t size = ct_fndef_count * sizeof(fndef_t);
    ct_fndef_list = mallocz(size);

    // - 遍历全局符号表中的所有 fn 数据就行了
    SLICE_FOR(symbol_fn_list) {
        int index = _i;
        symbol_t *s = SLICE_VALUE(symbol_fn_list);
        ast_fn_decl *fn = s->ast_value;
        closure_t *c = fn->closure;

        fndef_t *f = &ct_fndef_list[index];
        f->stack_size = align(c->stack_size, POINTER_SIZE);
        f->gc_bits = malloc_gc_bits(f->stack_size);
        size += calc_gc_bits_size(f->stack_size);
        uint64_t offset = 0;
        for (int i = 0; i < c->stack_vars->count; ++i) {
            lir_var_t *var = c->stack_vars->take[i];
            offset = align(offset, type_sizeof(var->type)); // 按 size 对齐
            bool need = type_need_gc(var->type);
            if (need) {
                bitmap_set(f->gc_bits, offset / POINTER_SIZE);
            }
        }
        f->base = 0; // 等待符号重定位
        f->end = 0;
    }
    return size;
}

uint64_t pre_symdef_list() {
    ct_symdef_count = symbol_var_list->count;
    uint64_t size = ct_symdef_count * sizeof(symdef_t);
    ct_symdef_list = mallocz(size);
    SLICE_FOR(symbol_var_list) {
        int index = _i;
        symbol_t *s = SLICE_VALUE(symbol_var_list);
        ast_var_decl *var_decl = s->ast_value;
        symdef_t *symdef = &ct_symdef_list[index];
        symdef->need_gc = type_need_gc(var_decl->type);
        symdef->size = type_sizeof(var_decl->type); // 符号的大小
        symdef->base = 0;
    }
    return size;
}

byte *fndefs_serialize() {
    // 按 count 进行一次序列化，然后将 gc_bits 按顺序追加
    byte *data = mallocz(ct_fndef_size);

    byte *p = data;
    // 首先将 fndef 移动到 data 中
    uint64_t size = ct_fndef_count * sizeof(fndef_t);
    memmove(p, ct_fndef_list, size);

    // 移动 gc_bits
    p = p + size; // byte 类型，所以按字节移动
    for (int i = 0; i < ct_fndef_count; ++i) {
        fndef_t *f = &ct_fndef_list[i];
        uint64_t gc_bits_size = calc_gc_bits_size(f->stack_size);
        memmove(p, f->gc_bits, gc_bits_size);
        p += gc_bits_size;
    }

    return data;
}


byte *symdefs_serialize() {
    return (byte *) ct_symdef_list;
}

byte *rtypes_serialize() {
    // 按 count 进行一次序列化，然后将 gc_bits 按顺序追加
    // 计算 ct_reflect_type
    byte *data = mallocz(ct_rtype_size);
    byte *p = data;

    // rtypes 整体一次性移动到 data 中，随后再慢慢移动 gc_bits
    uint64_t size = ct_rtype_list->length * sizeof(rtype_t);
    memmove(p, ct_rtype_list->take, size);

    // 移动 gc_bits
    p = p + size; // byte 类型，所以按字节移动
    for (int i = 0; i < ct_rtype_list->length; ++i) {
        rtype_t *r = ct_list_value(ct_rtype_list, i); // take 的类型是字节，所以这里按字节移动
        uint64_t gc_bits_size = calc_gc_bits_size(r->size);
        memmove(p, r->gc_bits, gc_bits_size);
        p += gc_bits_size;
    }

    return data;
}

