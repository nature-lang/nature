#include "assembler.h"
#include "arch.h"
#include "src/native/native.h"

void object_load_symbols(elf_context *ctx, slice_t *asm_symbols) {
    // 将全局变量定义写入到数据段与符号表
    for (int i = 0; i < asm_symbols->count; ++i) {
        asm_global_symbol_t *symbol = asm_symbols->take[i];
        // 写入到数据段
        uint64_t offset = elf_put_data(ctx->data_section, symbol->value, symbol->size);

        // 写入符号表
        Elf64_Sym sym = {
                .st_size = symbol->size,
                .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
                .st_other = 0,
                .st_shndx = ctx->data_section->sh_index, // 定义符号的段
                .st_value = offset, // 定义符号的位置，基于段的偏移
        };
        elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, symbol->name);
    }
}

void object_load_operations(elf_context *ctx, closure_t *c) {
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
