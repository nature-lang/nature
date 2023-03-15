#include "assembler.h"
#include "arch.h"
#include "src/native/native.h"

void var_decl_encodings(elf_context *ctx, slice_t *var_decls) {
    // 将全局变量定义写入到数据段与符号表
    for (int i = 0; i < var_decls->count; ++i) {
        asm_var_decl_t *var_decl = var_decls->take[i];
        // 写入到数据段
        uint64_t offset = elf_put_data(ctx->data_section, var_decl->value, var_decl->size);

        // 写入符号表
        Elf64_Sym sym = {
                .st_size = var_decl->size,
                .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
                .st_other = 0,
                .st_shndx = ctx->data_section->sh_index, // 定义符号的段
                .st_value = offset, // 定义符号的位置，基于段的偏移
        };
        elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, var_decl->name);
    }
}

void linkable_object_load_closure(elf_context *ctx, closure_t *c) {
    // 将全局变量写入到数据段或者符号表 (这里应该叫 global var)
    var_decl_encodings(ctx, c->asm_var_decls);

    // 代码段生成
    c->text_count = opcode_encodings(ctx, c->asm_operations);

    alloc_section_names(ctx, 1);
    size_t file_offset = sizeof(Elf64_Ehdr);
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
        file_offset = (file_offset + 15) & -16;
        s->sh_offset = file_offset;
        if (s->sh_type != SHT_NOBITS) {
            file_offset += s->sh_size;
        }
    }
    ctx->file_offset = file_offset;
}
