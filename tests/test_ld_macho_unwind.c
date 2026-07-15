#include "test_ld_macho_common.h"

#include "src/ld/ld_macho_eh_frame.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void test_unwind_relocation(uint8_t output[8], uint32_t address,
                                   uint32_t symbol, bool pcrel,
                                   unsigned length, bool external,
                                   unsigned type) {
    uint32_t word = symbol | ((uint32_t) pcrel << 24U) |
                    ((uint32_t) length << 25U) |
                    ((uint32_t) external << 27U) |
                    ((uint32_t) type << 28U);
    memcpy(output, &address, sizeof(address));
    memcpy(output + 4U, &word, sizeof(word));
}

static void test_unwind_context_deinit(ld_context_t *ctx,
                                       ld_options_t *options) {
    for (size_t index = 0; index < ctx->outputs.count; index++) {
        free(ctx->outputs.items[index]->data);
        free(ctx->outputs.items[index]);
    }
    free(ctx->outputs.items);
    free(ctx->unwind.records);
    free(ctx->got_refs.items);
    ld_options_deinit(options);
}

static void test_local_personality_and_null_gap(void) {
    ld_compact_unwind_entry_t compact_entries[2] = {
            {
                    .range_start = 0x40U,
                    .range_length = 8U,
                    .compact_encoding = 0x02000000U,
                    .personality_function = 0x100U,
            },
            {
                    .range_start = 0x50U,
                    .range_length = 8U,
                    .compact_encoding = 0x02000000U,
            },
    };
    uint8_t compact_relocations[3U * 8U];
    test_unwind_relocation(compact_relocations, 0U, 1U, false, 3U,
                           false, LD_ARM64_RELOC_UNSIGNED);
    test_unwind_relocation(compact_relocations + 8U, 16U, 2U, false,
                           3U, false, LD_ARM64_RELOC_UNSIGNED);
    test_unwind_relocation(compact_relocations + 16U, 32U, 1U, false,
                           3U, false, LD_ARM64_RELOC_UNSIGNED);

    ld_output_section_t text_output = {0};
    memcpy(text_output.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(text_output.sectname, "__text", sizeof("__text"));
    text_output.addr = LD_IMAGE_BASE + 0x1000U;
    ld_output_section_t data_output = {0};
    memcpy(data_output.segname, "__DATA", sizeof("__DATA"));
    memcpy(data_output.sectname, "__data", sizeof("__data"));
    data_output.addr = LD_IMAGE_BASE + 0x2000U;

    ld_input_section_t sections[3] = {0};
    memcpy(sections[0].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[0].header.sectname, "__text", sizeof("__text"));
    sections[0].header.addr = 0x40U;
    sections[0].header.size = 0x18U;
    sections[0].header.flags =
            LD_S_ATTR_PURE_INSTRUCTIONS | LD_S_ATTR_SOME_INSTRUCTIONS;
    sections[0].output = &text_output;
    memcpy(sections[1].header.segname, "__DATA", sizeof("__DATA"));
    memcpy(sections[1].header.sectname, "__data", sizeof("__data"));
    sections[1].header.addr = 0x100U;
    sections[1].header.size = 8U;
    sections[1].output = &data_output;
    memcpy(sections[2].header.segname, "__LD", sizeof("__LD"));
    memcpy(sections[2].header.sectname, "__compact_unwind", 16U);
    sections[2].header.size = sizeof(compact_entries);
    sections[2].header.nreloc = 3U;
    sections[2].data = (const uint8_t *) compact_entries;
    sections[2].relocations = compact_relocations;

    ld_input_symbol_t symbols[4] = {
            {.entry = {.n_type = LD_N_SECT, .n_sect = 1U, .n_value = 0x40U},
             .name = "_foo"},
            {.entry = {.n_type = LD_N_SECT, .n_sect = 1U, .n_value = 0x48U},
             .name = "_gap"},
            {.entry = {.n_type = LD_N_SECT, .n_sect = 1U, .n_value = 0x50U},
             .name = "_bar"},
            {.entry = {.n_type = LD_N_SECT, .n_sect = 2U, .n_value = 0x100U},
             .name = "_local_personality"},
    };
    ld_file_t file = {.path = "/tmp/ld-unwind-local-personality.o"};
    ld_object_t object = {
            .file = &file,
            .selected = true,
            .sections = sections,
            .section_count = 3U,
            .symbols = symbols,
            .symbol_count = 4U,
    };
    for (size_t index = 0; index < 3U; index++) sections[index].object = &object;
    for (size_t index = 0; index < 4U; index++) {
        symbols[index].object = &object;
        symbols[index].index = (uint32_t) index;
        symbols[index].output_symtab_index = UINT32_MAX;
    }
    ld_object_t *objects[] = {&object};
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    ctx.objects.items = objects;
    ctx.objects.count = 1U;
    ld_symbol_t colliding_global = {
            .name = "_local_personality",
            .kind = LD_SYMBOL_DEFINED,
    };
    HASH_ADD_KEYPTR(hh, ctx.symbols, colliding_global.name,
                    strlen(colliding_global.name), &colliding_global);

    assert(ld_unwind_prepare(&ctx) == LD_OK);
    assert(ctx.unwind.count == 3U);
    assert(ctx.unwind.personality_count == 1U);
    assert(ctx.got_refs.count == 1U);
    assert(ctx.got_refs.items[0].global == NULL);
    assert(ctx.got_refs.items[0].object == &object);
    assert(ctx.got_refs.items[0].input_index == 3U);
    ld_output_section_t got = {.addr = LD_IMAGE_BASE + 0x3000U};
    ctx.got = &got;
    assert(ld_unwind_emit(&ctx) == LD_OK);

    ld_unwind_info_header_t header;
    memcpy(&header, ctx.unwind.output->data, sizeof(header));
    assert(header.personalities_count == 1U);
    ld_unwind_info_index_entry_t index;
    memcpy(&index, ctx.unwind.output->data + header.index_offset,
           sizeof(index));
    ld_unwind_info_regular_page_header_t page;
    memcpy(&page, ctx.unwind.output->data + index.second_level_page_offset,
           sizeof(page));
    assert(page.entry_count == 3U);
    ld_unwind_info_regular_entry_t entries[3];
    memcpy(entries,
           ctx.unwind.output->data + index.second_level_page_offset +
                   page.entry_page_offset,
           sizeof(entries));
    assert(entries[0].function_offset == 0x1000U);
    assert(entries[1].function_offset == 0x1008U);
    assert(entries[2].function_offset == 0x1010U);
    assert((entries[0].encoding & LD_UNWIND_PERSONALITY_MASK) ==
           0x10000000U);
    assert(entries[1].encoding == 0U);

    HASH_DEL(ctx.symbols, &colliding_global);
    test_unwind_context_deinit(&ctx, &options);
}

static void test_eh_frame_parser_personality_and_lsda(void) {
    uint8_t eh_frame[0x40] = {
            0x18,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x01,
            'z',
            'P',
            'L',
            'R',
            0x00,
            0x01,
            0x78,
            0x1e,
            0x07,
            0x9b,
            0xed,
            0xff,
            0xff,
            0xff,
            0x10,
            0x10,
            0x0c,
            0x1f,
            0x00,
            0x20,
            0x00,
            0x00,
            0x00,
            0x20,
            0x00,
            0x00,
            0x00,
            0xdc,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0x08,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x08,
            0xcb,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0x00,
            0x00,
    };
    uint8_t relocations[5U * 8U];
    test_unwind_relocation(relocations, 0x35U, 2U, false, 3U, true,
                           LD_ARM64_RELOC_SUBTRACTOR);
    test_unwind_relocation(relocations + 8U, 0x35U, 1U, false, 3U,
                           true, LD_ARM64_RELOC_UNSIGNED);
    test_unwind_relocation(relocations + 16U, 0x24U, 2U, false, 3U,
                           true, LD_ARM64_RELOC_SUBTRACTOR);
    test_unwind_relocation(relocations + 24U, 0x24U, 0U, false, 3U,
                           true, LD_ARM64_RELOC_UNSIGNED);
    test_unwind_relocation(relocations + 32U, 0x13U, 3U, true, 2U,
                           true, LD_ARM64_RELOC_POINTER_TO_GOT);

    ld_output_section_t outputs[3] = {0};
    ld_input_section_t sections[3] = {0};
    memcpy(sections[0].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[0].header.sectname, "__text", sizeof("__text"));
    sections[0].header.size = 8U;
    sections[0].header.flags = LD_S_ATTR_PURE_INSTRUCTIONS;
    sections[0].output = &outputs[0];
    memcpy(sections[1].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[1].header.sectname, "__gcc_except_tab",
           sizeof("__gcc_except_tab"));
    sections[1].header.addr = 8U;
    sections[1].header.size = 1U;
    sections[1].output = &outputs[1];
    memcpy(sections[2].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[2].header.sectname, "__eh_frame", sizeof("__eh_frame"));
    sections[2].header.addr = 0x30U;
    sections[2].header.size = sizeof(eh_frame);
    sections[2].header.nreloc = 5U;
    sections[2].data = eh_frame;
    sections[2].relocations = relocations;
    sections[2].output = &outputs[2];
    sections[2].output_offset = 0x20U;

    ld_input_symbol_t symbols[4] = {
            {.entry = {.n_type = LD_N_SECT | LD_N_EXT,
                       .n_sect = 1U,
                       .n_value = 0U},
             .name = "_foo"},
            {.entry = {.n_type = LD_N_SECT, .n_sect = 2U, .n_value = 8U},
             .name = "_lsda"},
            {.entry = {.n_type = LD_N_SECT, .n_sect = 3U, .n_value = 0x30U},
             .name = "ltmp3"},
            {.entry = {.n_type = LD_N_UNDF | LD_N_EXT},
             .name = "_personality"},
    };
    ld_file_t file = {.path = "/tmp/ld-eh-frame-parser.o"};
    ld_object_t object = {
            .file = &file,
            .selected = true,
            .sections = sections,
            .section_count = 3U,
            .symbols = symbols,
            .symbol_count = 4U,
    };
    for (size_t index = 0; index < 3U; index++) sections[index].object = &object;
    for (size_t index = 0; index < 4U; index++) {
        symbols[index].object = &object;
        symbols[index].index = (uint32_t) index;
    }
    ld_object_t *objects[] = {&object};
    ld_options_t options;
    ld_options_init(&options);
    test_ld_diagnostic_capture_t capture = {0};
    options.diagnostic = test_ld_capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_context_t ctx = {.options = &options};
    ctx.objects.items = objects;
    ctx.objects.count = 1U;
    ld_macho_fde_list_t fdes = {0};

    assert(ld_macho_eh_frame_collect(&ctx, &fdes) == LD_OK);
    assert(fdes.count == 1U);
    assert(fdes.items[0].function_address == 0U);
    assert(fdes.items[0].function_size == 8U);
    assert(fdes.items[0].output_offset == 0x3cU);
    assert(fdes.items[0].has_personality);
    assert(fdes.items[0].personality_symbol_index == 3U);
    assert(fdes.items[0].has_lsda);
    assert(fdes.items[0].lsda_address == 8U);

    ld_macho_eh_frame_deinit(&fdes);
    eh_frame[0x12U] = 0x1bU;
    assert(ld_macho_eh_frame_collect(&ctx, &fdes) == LD_UNSUPPORTED);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "personality encoding") != NULL);
    ld_macho_eh_frame_deinit(&fdes);

    eh_frame[0x12U] = 0x9bU;
    uint32_t relocation_word;
    memcpy(&relocation_word, relocations + 4U, sizeof(relocation_word));
    relocation_word |= 1U << 24U;
    memcpy(relocations + 4U, &relocation_word, sizeof(relocation_word));
    memset(&capture, 0, sizeof(capture));
    assert(ld_macho_eh_frame_collect(&ctx, &fdes) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "subtractor pair") != NULL);
    ld_macho_eh_frame_deinit(&fdes);
    relocation_word &= ~(1U << 24U);
    memcpy(relocations + 4U, &relocation_word, sizeof(relocation_word));

    memset(&capture, 0, sizeof(capture));
    sections[0].header.size = 4U;
    assert(ld_unwind_prepare(&ctx) == LD_INVALID_INPUT);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "exceeds its code section") != NULL);
    free(ctx.unwind.records);
    free(ctx.got_refs.items);
    ld_options_deinit(&options);
}

static void test_fde_and_null_superposition(void) {
    static const uint8_t eh_frame[0x30] = {
            0x10,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x01,
            'z',
            'R',
            0x00,
            0x01,
            0x78,
            0x1e,
            0x01,
            0x10,
            0x0c,
            0x1f,
            0x00,
            0x18,
            0x00,
            0x00,
            0x00,
            0x18,
            0x00,
            0x00,
            0x00,
            0xe4,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            0x08,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0xff,
            0x00,
            0x00,
    };
    uint8_t relocations[2U * 8U];
    test_unwind_relocation(relocations, 0x1cU, 2U, false, 3U, true,
                           LD_ARM64_RELOC_SUBTRACTOR);
    test_unwind_relocation(relocations + 8U, 0x1cU, 0U, false, 3U,
                           true, LD_ARM64_RELOC_UNSIGNED);
    ld_output_section_t text_output = {.addr = LD_IMAGE_BASE + 0x1000U};
    ld_output_section_t eh_output = {0};
    ld_input_section_t sections[2] = {0};
    memcpy(sections[0].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[0].header.sectname, "__text", sizeof("__text"));
    sections[0].header.size = 0x10U;
    sections[0].header.flags = LD_S_ATTR_PURE_INSTRUCTIONS;
    sections[0].output = &text_output;
    memcpy(sections[1].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[1].header.sectname, "__eh_frame", sizeof("__eh_frame"));
    sections[1].header.addr = 0x28U;
    sections[1].header.size = sizeof(eh_frame);
    sections[1].header.nreloc = 2U;
    sections[1].data = eh_frame;
    sections[1].relocations = relocations;
    sections[1].output = &eh_output;
    ld_input_symbol_t symbols[3] = {
            {.entry = {.n_type = LD_N_SECT | LD_N_EXT,
                       .n_sect = 1U,
                       .n_value = 0U},
             .name = "_foo"},
            {.entry = {.n_type = LD_N_SECT | LD_N_EXT,
                       .n_sect = 1U,
                       .n_value = 8U},
             .name = "_gap"},
            {.entry = {.n_type = LD_N_SECT, .n_sect = 2U, .n_value = 0x28U},
             .name = "ltmp2"},
    };
    ld_file_t file = {.path = "/tmp/ld-eh-frame-superposition.o"};
    ld_object_t object = {
            .file = &file,
            .selected = true,
            .sections = sections,
            .section_count = 2U,
            .symbols = symbols,
            .symbol_count = 3U,
    };
    for (size_t index = 0; index < 2U; index++) sections[index].object = &object;
    for (size_t index = 0; index < 3U; index++) {
        symbols[index].object = &object;
        symbols[index].index = (uint32_t) index;
    }
    ld_object_t *objects[] = {&object};
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {.options = &options};
    ctx.objects.items = objects;
    ctx.objects.count = 1U;

    assert(ld_unwind_prepare(&ctx) == LD_OK);
    assert(ctx.unwind.count == 2U);
    assert(ld_unwind_emit(&ctx) == LD_OK);
    ld_unwind_info_header_t header;
    memcpy(&header, ctx.unwind.output->data, sizeof(header));
    ld_unwind_info_index_entry_t index;
    memcpy(&index, ctx.unwind.output->data + header.index_offset,
           sizeof(index));
    ld_unwind_info_regular_page_header_t page;
    memcpy(&page, ctx.unwind.output->data + index.second_level_page_offset,
           sizeof(page));
    assert(page.entry_count == 2U);
    ld_unwind_info_regular_entry_t entries[2];
    memcpy(entries,
           ctx.unwind.output->data + index.second_level_page_offset +
                   page.entry_page_offset,
           sizeof(entries));
    assert(entries[0].function_offset == 0x1000U);
    assert((entries[0].encoding & LD_UNWIND_ARM64_MODE_MASK) ==
           LD_UNWIND_ARM64_MODE_DWARF);
    assert((entries[0].encoding & 0x00ffffffU) == 0x14U);
    assert(entries[1].function_offset == 0x1008U);
    assert(entries[1].encoding == 0U);

    test_unwind_context_deinit(&ctx, &options);
}

static void test_truncated_eh_frame_record(void) {
    uint8_t data[8] = {0x20U};
    ld_output_section_t output = {0};
    ld_input_section_t section = {0};
    memcpy(section.header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(section.header.sectname, "__eh_frame", sizeof("__eh_frame"));
    section.header.size = sizeof(data);
    section.data = data;
    section.output = &output;
    ld_file_t file = {.path = "/tmp/ld-eh-frame-truncated.o"};
    ld_object_t object = {
            .file = &file,
            .selected = true,
            .sections = &section,
            .section_count = 1U,
    };
    section.object = &object;
    ld_object_t *objects[] = {&object};
    test_ld_diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.diagnostic = test_ld_capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_context_t ctx = {.options = &options};
    ctx.objects.items = objects;
    ctx.objects.count = 1U;
    ld_macho_fde_list_t fdes = {0};

    assert(ld_macho_eh_frame_collect(&ctx, &fdes) == LD_INVALID_INPUT);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "record size") != NULL);

    ld_macho_eh_frame_deinit(&fdes);
    ld_options_deinit(&options);
}

void test_ld_macho_unwind(void) {
    test_local_personality_and_null_gap();
    test_eh_frame_parser_personality_and_lsda();
    test_fde_and_null_superposition();
    test_truncated_eh_frame_record();
}
