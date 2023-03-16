#include "links.h"

#include "bitmap.h"
#include "src/symbol/symbol.h"
#include "src/lir/lir.h"

/**
 * 基于 symbol fn 生成基础的 fn list
 */
void pre_fndef_list() {
    fndefs_size = symbol_fn_list->count * sizeof(fndef_t);
    fndefs = mallocz(fndefs_size);
    // - 遍历全局符号表中的所有 fn 数据就行了
    SLICE_FOR(symbol_fn_list) {
        int index = _i;
        symbol_t *s = SLICE_VALUE(symbol_fn_list);
        ast_new_fn *fn = s->ast_value;
        closure_t *c = fn->closure;

        fndef_t *f = &fndefs[index];
        f->stack_size = align(c->stack_size, POINTER_SIZE);
        f->gc_bits = malloc_gc_bits(f->stack_size);
        fndefs_size += calc_gc_bits_size(f->stack_size);
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
}

void pre_symdef_list() {
    symdefs_size = symbol_var_list->count * sizeof(symdef_t);
    symdefs = mallocz(symdefs_size);
    SLICE_FOR(symbol_var_list) {
        int index = _i;
        symbol_t *s = SLICE_VALUE(symbol_var_list);
        ast_var_decl *var_decl = s->ast_value;
        symdef_t *symdef = &symdefs[index];
        symdef->need_gc = type_need_gc(var_decl->type);
        symdef->base = 0;
        symdef->size = 0;
    }
}

byte *fndefs_serialize(fndef_t *_fndefs, uint64_t count) {
    // 按 count 进行一次序列化，然后将 gc_bits 按顺序追加
    byte *data = mallocz(fndefs_size);

    byte *p = data;
    // 首先将 fndef 移动到 data 中
    uint64_t size = count * sizeof(fndef_t);
    memmove(p, _fndefs, size);

    // 移动 gc_bits
    p = p + size; // byte 类型，所以按字节移动
    for (int i = 0; i < count; ++i) {
        fndef_t *f = &_fndefs[i];
        uint64_t gc_bits_size = calc_gc_bits_size(f->stack_size);
        memmove(p, f->gc_bits, gc_bits_size);
        p += gc_bits_size;
    }

    return data;
}

byte *rtypes_serialize(reflect_type_t *_rtypes, uint64_t count) {
    // 按 count 进行一次序列化，然后将 gc_bits 按顺序追加
    // 计算 reflect_type
    byte *data = mallocz(rtype_size);

    byte *p = data;
    // 首先将 fndef 移动到 data 中
    uint64_t size = count * sizeof(fndef_t);
    memmove(p, _rtypes, size);

    // 移动 gc_bits
    p = p + size; // byte 类型，所以按字节移动
    for (int i = 0; i < count; ++i) {
        reflect_type_t *r = &_rtypes[i];
        uint64_t gc_bits_size = calc_gc_bits_size(r->size);
        memmove(p, r->gc_bits, gc_bits_size);
        p += gc_bits_size;
    }

    return data;
}
