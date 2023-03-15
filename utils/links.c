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
        uint64_t gc_bits_size = f->stack_size / POINTER_SIZE;
        f->gc_bits = mallocz(gc_bits_size);
        fndefs_size += gc_bits_size;
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