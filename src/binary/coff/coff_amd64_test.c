/*
 * Byte-level tests for Nature's AMD64 COFF adapter and Windows unwind data.
 *
 * Sources:
 *   llvm/lib/Target/X86/MCTargetDesc/X86WinCOFFObjectWriter.cpp
 *   llvm/include/llvm/Support/Win64EH.h
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "coff_amd64.h"

#include "src/binary/encoding/amd64/opcode.h"
#include "src/register/arch/amd64.h"
#include "utils/custom_links.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef COFF_AMD64_TEST_MAIN

#define COFF_AMD64_TEST_CHECK(condition)                             \
    do {                                                             \
        if (!(condition)) {                                          \
            fprintf(stderr, "COFF AMD64 test failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                 \
            return false;                                            \
        }                                                            \
    } while (0)

typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint16_t section_count;
    uint32_t symbol_offset;
    uint32_t symbol_count;
    uint32_t string_offset;
    uint32_t string_size;
} coff_amd64_test_image_t;

typedef struct {
    uint32_t index;
    uint32_t offset;
    uint8_t aux_count;
} coff_amd64_test_symbol_t;

static uint16_t coff_amd64_test_u16(const uint8_t *bytes) {
    return (uint16_t) bytes[0] | (uint16_t) ((uint16_t) bytes[1] << 8U);
}

static uint32_t coff_amd64_test_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | (uint32_t) bytes[1] << 8U |
           (uint32_t) bytes[2] << 16U | (uint32_t) bytes[3] << 24U;
}

static void coff_amd64_test_put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void coff_amd64_test_put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static bool coff_amd64_test_range(size_t size, size_t offset,
                                  size_t length) {
    return offset <= size && length <= size - offset;
}

static bool coff_amd64_test_parse(const uint8_t *bytes, size_t size,
                                  coff_amd64_test_image_t *image) {
    if (!bytes || !image || size < LD_COFF_HEADER_SIZE) return false;
    memset(image, 0, sizeof(*image));
    image->bytes = bytes;
    image->size = size;
    image->section_count = coff_amd64_test_u16(bytes + 2U);
    image->symbol_offset = coff_amd64_test_u32(bytes + 8U);
    image->symbol_count = coff_amd64_test_u32(bytes + 12U);
    const uint64_t string_offset =
            (uint64_t) image->symbol_offset +
            (uint64_t) image->symbol_count * LD_COFF_SYMBOL_SIZE;
    if (string_offset > UINT32_MAX ||
        !coff_amd64_test_range(size, (size_t) string_offset, 4U))
        return false;
    image->string_offset = (uint32_t) string_offset;
    image->string_size = coff_amd64_test_u32(bytes + image->string_offset);
    return image->string_size >= 4U &&
           coff_amd64_test_range(size, image->string_offset,
                                 image->string_size);
}

static bool coff_amd64_test_string(const coff_amd64_test_image_t *image,
                                   uint32_t offset, const char **value) {
    if (!image || !value || offset < 4U || offset >= image->string_size)
        return false;
    const char *text = (const char *) image->bytes + image->string_offset +
                       offset;
    const size_t remaining = image->string_size - offset;
    if (!memchr(text, '\0', remaining)) return false;
    *value = text;
    return true;
}

static bool coff_amd64_test_section_name(
        const coff_amd64_test_image_t *image, const uint8_t *header,
        char short_name[LD_COFF_NAME_SIZE + 1U], const char **name) {
    memcpy(short_name, header, LD_COFF_NAME_SIZE);
    short_name[LD_COFF_NAME_SIZE] = '\0';
    if (header[0] != '/') {
        *name = short_name;
        return true;
    }
    char *end = NULL;
    unsigned long offset = strtoul(short_name + 1U, &end, 10);
    if (end == short_name + 1U || *end != '\0' || offset > UINT32_MAX)
        return false;
    return coff_amd64_test_string(image, (uint32_t) offset, name);
}

static bool coff_amd64_test_find_section(
        const coff_amd64_test_image_t *image, const char *name,
        uint16_t *index, uint32_t *header_offset) {
    for (uint16_t i = 0U; i < image->section_count; i++) {
        const uint32_t offset = LD_COFF_HEADER_SIZE +
                                (uint32_t) i * LD_COFF_SECTION_HEADER_SIZE;
        if (!coff_amd64_test_range(image->size, offset,
                                   LD_COFF_SECTION_HEADER_SIZE))
            return false;
        char short_name[LD_COFF_NAME_SIZE + 1U];
        const char *actual = NULL;
        if (!coff_amd64_test_section_name(image, image->bytes + offset,
                                          short_name, &actual))
            return false;
        if (strcmp(actual, name) == 0) {
            if (index) *index = (uint16_t) (i + 1U);
            if (header_offset) *header_offset = offset;
            return true;
        }
    }
    return false;
}

static bool coff_amd64_test_symbol_name(
        const coff_amd64_test_image_t *image, const uint8_t *record,
        char short_name[LD_COFF_NAME_SIZE + 1U], const char **name) {
    memcpy(short_name, record, LD_COFF_NAME_SIZE);
    short_name[LD_COFF_NAME_SIZE] = '\0';
    if (coff_amd64_test_u32(record) != 0U) {
        *name = short_name;
        return true;
    }
    return coff_amd64_test_string(image, coff_amd64_test_u32(record + 4U),
                                  name);
}

static bool coff_amd64_test_find_symbol(
        const coff_amd64_test_image_t *image, const char *name,
        coff_amd64_test_symbol_t *result) {
    uint32_t index = 0U;
    while (index < image->symbol_count) {
        const uint64_t offset = (uint64_t) image->symbol_offset +
                                (uint64_t) index * LD_COFF_SYMBOL_SIZE;
        if (offset > UINT32_MAX ||
            !coff_amd64_test_range(image->size, (size_t) offset,
                                   LD_COFF_SYMBOL_SIZE))
            return false;
        const uint8_t *record = image->bytes + (size_t) offset;
        char short_name[LD_COFF_NAME_SIZE + 1U];
        const char *actual = NULL;
        if (!coff_amd64_test_symbol_name(image, record, short_name, &actual))
            return false;
        const uint8_t aux_count = record[17];
        if ((uint64_t) index + 1U + aux_count > image->symbol_count)
            return false;
        if (strcmp(actual, name) == 0) {
            result->index = index;
            result->offset = (uint32_t) offset;
            result->aux_count = aux_count;
            return true;
        }
        index += 1U + aux_count;
    }
    return false;
}

static amd64_opcode_inst_t coff_amd64_test_matched_instruction;

/*
 * The adapter test deliberately supplies a tiny deterministic encoder. This
 * isolates object/unwind behavior from opcode-table initialization while
 * preserving the exact lengths and valid x86-64 bytes of Nature's prologue.
 */
amd64_opcode_inst_t *amd64_asm_inst_encoding(amd64_asm_inst_t instruction,
                                             uint8_t *data, uint8_t *count,
                                             closure_t *closure) {
    (void) closure;
    if (strcmp(instruction.name, "push") == 0) {
        data[0] = 0x55;
        *count = 1U;
    } else if (strcmp(instruction.name, "pop") == 0) {
        data[0] = 0x5d;
        *count = 1U;
    } else if (strcmp(instruction.name, "mov") == 0 &&
               instruction.operands[0]->type == AMD64_ASM_OPERAND_TYPE_REG &&
               instruction.operands[1]->type == AMD64_ASM_OPERAND_TYPE_REG) {
        const uint8_t bytes[] = {0x48, 0x89, 0xe5};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "lea") == 0 &&
               instruction.operands[1]->type ==
                       AMD64_ASM_OPERAND_TYPE_SIB_REG) {
        const asm_sib_reg_t *slot = instruction.operands[1]->value;
        const uint8_t bytes[] = {0x48, 0x8d, 0x6c, 0x24,
                                 (uint8_t) slot->disp};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "sub") == 0) {
        const uint8_t bytes[] = {0x48, 0x83, 0xec, 0x40};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "add") == 0 &&
               instruction.operands[0]->type ==
                       AMD64_ASM_OPERAND_TYPE_REG &&
               instruction.operands[1]->type ==
                       AMD64_ASM_OPERAND_TYPE_UINT32 &&
               ((asm_uint32_t *) instruction.operands[1]->value)->value ==
                       0U) {
        const uint8_t bytes[] = {0x49, 0x81, 0xc3, 0x00,
                                 0x00, 0x00, 0x00};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "add") == 0) {
        const uint8_t bytes[] = {0x48, 0x83, 0xc4, 0x40};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "mov") == 0 &&
               instruction.operands[0]->type ==
                       AMD64_ASM_OPERAND_TYPE_SIB_REG) {
        const uint8_t bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x20};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "mov") == 0 &&
               instruction.operands[1]->type ==
                       AMD64_ASM_OPERAND_TYPE_SIB_REG) {
        const uint8_t bytes[] = {0x48, 0x8b, 0x5c, 0x24, 0x20};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "mov") == 0 &&
               instruction.operands[1]->type ==
                       AMD64_ASM_OPERAND_TYPE_RIP_RELATIVE) {
        const uint8_t bytes[] = {0x48, 0x8b, 0x05, 0x00,
                                 0x00, 0x00, 0x00};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "mov") == 0 &&
               instruction.operands[1]->type ==
                       AMD64_ASM_OPERAND_TYPE_SEG_OFFSET) {
        const uint8_t bytes[] = {0x65, 0x48, 0x8b, 0x1c, 0x25,
                                 0x58, 0x00, 0x00, 0x00};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "mov") == 0 &&
               instruction.operands[1]->type ==
                       AMD64_ASM_OPERAND_TYPE_INDIRECT_REG) {
        const uint8_t bytes[] = {0x49, 0x8b, 0x03};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "movxmm128") == 0 &&
               instruction.operands[0]->type ==
                       AMD64_ASM_OPERAND_TYPE_SIB_REG) {
        const uint8_t bytes[] = {0x44, 0x0f, 0x11, 0x7c, 0x24, 0x30};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "movxmm128") == 0 &&
               instruction.operands[1]->type ==
                       AMD64_ASM_OPERAND_TYPE_SIB_REG) {
        const uint8_t bytes[] = {0x44, 0x0f, 0x10, 0x7c, 0x24, 0x30};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else if (strcmp(instruction.name, "ret") == 0) {
        data[0] = 0xc3;
        *count = 1U;
    } else if (strcmp(instruction.name, "call") == 0) {
        const uint8_t bytes[] = {0xe8, 0x00, 0x00, 0x00, 0x00};
        memcpy(data, bytes, sizeof(bytes));
        *count = sizeof(bytes);
    } else {
        *count = 0U;
        return NULL;
    }
    return &coff_amd64_test_matched_instruction;
}

static void coff_amd64_test_plan_action(uint8_t *bytes, uint8_t kind,
                                        uint8_t reg, uint16_t flags,
                                        uint32_t value) {
    bytes[0] = kind;
    bytes[1] = reg;
    coff_amd64_test_put_u16(bytes + 2U, flags);
    coff_amd64_test_put_u32(bytes + 4U, value);
}

static bool coff_amd64_test_unwind_object(void) {
    list_t *previous_caller_list = ct_caller_list;
    ct_caller_list = ct_list_new(sizeof(caller_t));
    COFF_AMD64_TEST_CHECK(ct_caller_list != NULL);

    uint8_t plan[20U + 5U * 8U] = {0};
    memcpy(plan, "NWU1", 4U);
    coff_amd64_test_put_u16(plan + 4U, 20U);
    coff_amd64_test_put_u16(plan + 6U, 5U);
    coff_amd64_test_put_u32(plan + 8U, 64U);
    coff_amd64_test_put_u32(plan + 12U, 32U);
    plan[16] = 5U;
    coff_amd64_test_plan_action(plan + 20U, 1U, 5U, 0U, 0U);
    coff_amd64_test_plan_action(plan + 28U, 3U, 0U, 0U, 64U);
    coff_amd64_test_plan_action(plan + 36U, 2U, 5U, 0U, 64U);
    coff_amd64_test_plan_action(plan + 44U, 4U, 3U, 0U, 32U);
    coff_amd64_test_plan_action(plan + 52U, 5U, 15U, 0U, 48U);

    uint8_t regular_data[] = {0xde, 0xad, 0xbe, 0xef};
    asm_global_symbol_t private_plan = {
            .name = "__nature_unwind_plan$entry",
            .size = sizeof(plan),
            .value = plan,
    };
    asm_global_symbol_t regular_symbol = {
            .name = "regular_data",
            .size = sizeof(regular_data),
            .value = regular_data,
    };
    void *global_items[] = {&private_plan, &regular_symbol};
    slice_t globals = {
            .count = 2,
            .capacity = 2,
            .take = global_items,
    };

    reg_t rbp_reg = {.index = 5U};
    reg_t rsp_reg = {.index = 4U};
    reg_t rbx_reg = {.index = 3U};
    reg_t rax_reg = {.index = 0U};
    reg_t xmm15_reg = {.index = 15U};
    asm_symbol_t entry_symbol = {
            .name = "entry",
            .is_local = false,
            .is_tls = false,
    };
    asm_symbol_t data_symbol = {
            .name = "regular_data",
            .is_local = false,
            .is_tls = false,
    };
    asm_symbol_t exit_symbol = {
            .name = "ExitProcess",
            .is_local = false,
            .is_tls = false,
    };
    asm_uint32_t frame_immediate = {.value = 64U};
    asm_sib_reg_t rbx_slot = {
            .base = &rsp_reg,
            .index = NULL,
            .scale = 0U,
            .disp = 32,
    };
    asm_sib_reg_t frame_pointer = {
            .base = &rsp_reg,
            .index = NULL,
            .scale = 0U,
            .disp = 64,
    };
    asm_sib_reg_t xmm_slot = {
            .base = &rsp_reg,
            .index = NULL,
            .scale = 0U,
            .disp = 48,
    };

    amd64_asm_operand_t label_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .value = &entry_symbol,
    };
    amd64_asm_operand_t rbp_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = 8U,
            .value = &rbp_reg,
    };
    amd64_asm_operand_t rsp_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = 8U,
            .value = &rsp_reg,
    };
    amd64_asm_operand_t frame_pointer_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = 8U,
            .value = &frame_pointer,
    };
    amd64_asm_operand_t rbx_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = 8U,
            .value = &rbx_reg,
    };
    amd64_asm_operand_t rax_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = 8U,
            .value = &rax_reg,
    };
    amd64_asm_operand_t xmm15_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_FREG,
            .size = 8U,
            .value = &xmm15_reg,
    };
    amd64_asm_operand_t frame_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_UINT32,
            .size = 4U,
            .value = &frame_immediate,
    };
    amd64_asm_operand_t rbx_slot_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = 8U,
            .value = &rbx_slot,
    };
    amd64_asm_operand_t xmm_slot_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = 8U,
            .value = &xmm_slot,
    };
    amd64_asm_operand_t data_symbol_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .size = 8U,
            .value = &data_symbol,
    };
    amd64_asm_operand_t exit_symbol_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .value = &exit_symbol,
    };

    amd64_asm_inst_t label = {
            .name = "label",
            .count = 1U,
            .operands = {&label_operand}};
    amd64_asm_inst_t push = {
            .name = "push",
            .count = 1U,
            .operands = {&rbp_operand}};
    amd64_asm_inst_t set_frame = {
            .name = "lea",
            .count = 2U,
            .operands = {&rbp_operand, &frame_pointer_operand},
    };
    amd64_asm_inst_t allocate = {
            .name = "sub",
            .count = 2U,
            .operands = {&rsp_operand, &frame_operand},
    };
    amd64_asm_inst_t save_rbx = {
            .name = "mov",
            .count = 2U,
            .operands = {&rbx_slot_operand, &rbx_operand},
    };
    amd64_asm_inst_t save_xmm = {
            .name = "movxmm128",
            .count = 2U,
            .operands = {&xmm_slot_operand, &xmm15_operand},
    };
    amd64_asm_inst_t load_data = {
            .name = "mov",
            .count = 2U,
            .operands = {&rax_operand, &data_symbol_operand},
    };
    amd64_asm_inst_t call_exit = {
            .name = "call",
            .count = 1U,
            .line = 17U,
            .column = 9U,
            .operands = {&exit_symbol_operand},
    };
    amd64_asm_inst_t restore_xmm = {
            .name = "movxmm128",
            .count = 2U,
            .operands = {&xmm15_operand, &xmm_slot_operand},
    };
    amd64_asm_inst_t restore_rbx = {
            .name = "mov",
            .count = 2U,
            .operands = {&rbx_operand, &rbx_slot_operand},
    };
    amd64_asm_inst_t deallocate = {
            .name = "add",
            .count = 2U,
            .operands = {&rsp_operand, &frame_operand},
    };
    amd64_asm_inst_t pop = {
            .name = "pop",
            .count = 1U,
            .operands = {&rbp_operand}};
    amd64_asm_inst_t ret = {.name = "ret", .count = 0U};
    void *operation_items[] = {
            &label,
            &push,
            &allocate,
            &set_frame,
            &save_rbx,
            &save_xmm,
            &load_data,
            &call_exit,
            &restore_xmm,
            &restore_rbx,
            &deallocate,
            &pop,
            &ret,
    };
    slice_t operations = {
            .count = (int) (sizeof(operation_items) /
                            sizeof(operation_items[0])),
            .capacity = (int) (sizeof(operation_items) /
                               sizeof(operation_items[0])),
            .take = operation_items,
    };
    closure_t closure = {
            .linkident = "entry",
            .asm_operations = &operations,
    };
    void *closure_items[] = {&closure};
    slice_t closures = {.count = 1, .capacity = 1, .take = closure_items};
    module_t module;
    memset(&module, 0, sizeof(module));
    module.source_path = "coff_amd64_test.n";
    module.asm_global_symbols = &globals;
    module.closures = &closures;

    coff_object_t *object = coff_object_create_amd64(module.source_path);
    COFF_AMD64_TEST_CHECK(object != NULL);
    char error[512] = {0};
    COFF_AMD64_TEST_CHECK(coff_encode_amd64_module_ex(
                                  object, &module, error, sizeof(error)) ==
                          COFF_WRITER_OK);
    COFF_AMD64_TEST_CHECK(error[0] == '\0');
    COFF_AMD64_TEST_CHECK(closure.text_count == 50U);
    COFF_AMD64_TEST_CHECK(ct_caller_list->length == 1U);
    caller_t *caller = ct_list_value(ct_caller_list, 0U);
    COFF_AMD64_TEST_CHECK(caller != NULL);
    COFF_AMD64_TEST_CHECK(caller->data == &closure);
    COFF_AMD64_TEST_CHECK(caller->offset == 33U);
    COFF_AMD64_TEST_CHECK(caller->line == 17U);
    COFF_AMD64_TEST_CHECK(caller->column == 9U);
    COFF_AMD64_TEST_CHECK(strcmp(
                                  ct_strtable_data +
                                          caller->target_name_offset,
                                  "ExitProcess") == 0);
    COFF_AMD64_TEST_CHECK(coff_section_size(coff_object_data(object)) == 4U);
    COFF_AMD64_TEST_CHECK(!coff_object_find_symbol(
            object, "__nature_unwind_plan$entry", NULL));

    uint8_t *image_bytes = NULL;
    size_t image_size = 0U;
    COFF_AMD64_TEST_CHECK(coff_object_serialize(
                                  object, &image_bytes, &image_size) ==
                          COFF_WRITER_OK);
    coff_amd64_test_image_t image;
    COFF_AMD64_TEST_CHECK(
            coff_amd64_test_parse(image_bytes, image_size, &image));
    COFF_AMD64_TEST_CHECK(image.section_count == 6U);

    uint16_t pdata_index = 0U;
    uint16_t xdata_index = 0U;
    uint32_t text_header = 0U;
    uint32_t pdata_header = 0U;
    uint32_t xdata_header = 0U;
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_section(
            &image, ".text", NULL, &text_header));
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_section(
            &image, ".pdata$entry", &pdata_index, &pdata_header));
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_section(
            &image, ".xdata$entry", &xdata_index, &xdata_header));
    COFF_AMD64_TEST_CHECK(pdata_index == 5U && xdata_index == 6U);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u32(image_bytes + pdata_header +
                                              16U) == 12U);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u16(image_bytes + pdata_header +
                                              32U) == 3U);
    COFF_AMD64_TEST_CHECK((coff_amd64_test_u32(
                                   image_bytes + pdata_header + 36U) &
                           LD_COFF_SCN_LNK_COMDAT) != 0U);
    COFF_AMD64_TEST_CHECK((coff_amd64_test_u32(
                                   image_bytes + xdata_header + 36U) &
                           LD_COFF_SCN_LNK_COMDAT) != 0U);

    const uint32_t text_relocations =
            coff_amd64_test_u32(image_bytes + text_header + 24U);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u16(image_bytes + text_header +
                                              32U) == 2U);
    COFF_AMD64_TEST_CHECK(
            coff_amd64_test_range(image_size, text_relocations, 20U));

    const uint32_t pdata_raw =
            coff_amd64_test_u32(image_bytes + pdata_header + 20U);
    const uint32_t pdata_relocations =
            coff_amd64_test_u32(image_bytes + pdata_header + 24U);
    const uint32_t xdata_raw =
            coff_amd64_test_u32(image_bytes + xdata_header + 20U);
    COFF_AMD64_TEST_CHECK(
            coff_amd64_test_range(image_size, pdata_raw, 12U));
    COFF_AMD64_TEST_CHECK(
            coff_amd64_test_range(image_size, pdata_relocations, 30U));
    COFF_AMD64_TEST_CHECK(
            coff_amd64_test_range(image_size, xdata_raw, 20U));
    static const uint8_t zero_pdata[12] = {0};
    COFF_AMD64_TEST_CHECK(
            memcmp(image_bytes + pdata_raw, zero_pdata, 12U) == 0);
    static const uint8_t expected_unwind[] = {
            0x01, 0x15, 0x07, 0x45, /* v1, prolog 21, 7 slots, RBP+64 */
            0x15, 0xf8, 0x03, 0x00, /* SAVE_XMM128 XMM15, [rsp+48] */
            0x0f, 0x34, 0x04, 0x00, /* SAVE_NONVOL RBX, [rsp+32] */
            0x0a, 0x03, /* SET_FPREG */
            0x05, 0x72, /* ALLOC_SMALL 64 */
            0x01, 0x50, /* PUSH_NONVOL RBP */
            0x00, 0x00, /* even-slot padding */
    };
    COFF_AMD64_TEST_CHECK(sizeof(expected_unwind) == 20U);
    COFF_AMD64_TEST_CHECK(memcmp(image_bytes + xdata_raw, expected_unwind,
                                 sizeof(expected_unwind)) == 0);

    for (uint32_t i = 0U; i < 3U; i++) {
        const uint8_t *relocation =
                image_bytes + pdata_relocations + i * LD_COFF_RELOCATION_SIZE;
        COFF_AMD64_TEST_CHECK(coff_amd64_test_u32(relocation) == i * 4U);
        COFF_AMD64_TEST_CHECK(coff_amd64_test_u16(relocation + 8U) ==
                              LD_COFF_REL_AMD64_ADDR32NB);
    }

    coff_amd64_test_symbol_t pdata_section_symbol;
    coff_amd64_test_symbol_t xdata_section_symbol;
    coff_amd64_test_symbol_t function_symbol;
    coff_amd64_test_symbol_t end_symbol;
    coff_amd64_test_symbol_t data_reference_symbol;
    coff_amd64_test_symbol_t exit_reference_symbol;
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_symbol(
            &image, ".pdata$entry", &pdata_section_symbol));
    COFF_AMD64_TEST_CHECK(pdata_section_symbol.aux_count == 1U);
    const uint8_t *pdata_aux = image_bytes + pdata_section_symbol.offset +
                               LD_COFF_SYMBOL_SIZE;
    COFF_AMD64_TEST_CHECK(pdata_aux[14] == LD_COFF_COMDAT_ANY);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_symbol(
            &image, ".xdata$entry", &xdata_section_symbol));
    COFF_AMD64_TEST_CHECK(xdata_section_symbol.aux_count == 1U);
    const uint8_t *xdata_aux = image_bytes + xdata_section_symbol.offset +
                               LD_COFF_SYMBOL_SIZE;
    COFF_AMD64_TEST_CHECK(xdata_aux[14] == LD_COFF_COMDAT_ASSOCIATIVE);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u16(xdata_aux + 12U) ==
                          pdata_index);
    COFF_AMD64_TEST_CHECK(
            coff_amd64_test_find_symbol(&image, "entry", &function_symbol));
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u16(
                                  image_bytes + function_symbol.offset + 12U) ==
                          1U);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_symbol(
            &image, "__nature_unwind_end$entry", &end_symbol));
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u32(image_bytes + end_symbol.offset +
                                              8U) == 50U);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_symbol(
            &image, "regular_data", &data_reference_symbol));
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_symbol(
            &image, "ExitProcess", &exit_reference_symbol));

    const uint8_t *data_relocation = image_bytes + text_relocations;
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u32(data_relocation) == 24U);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u32(data_relocation + 4U) ==
                          data_reference_symbol.index);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u16(data_relocation + 8U) ==
                          LD_COFF_REL_AMD64_REL32);
    const uint8_t *call_relocation =
            image_bytes + text_relocations + LD_COFF_RELOCATION_SIZE;
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u32(call_relocation) == 29U);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u32(call_relocation + 4U) ==
                          exit_reference_symbol.index);
    COFF_AMD64_TEST_CHECK(coff_amd64_test_u16(call_relocation + 8U) ==
                          LD_COFF_REL_AMD64_REL32);

    COFF_AMD64_TEST_CHECK(coff_object_write_file(
                                  object,
                                  "/tmp/nature-coff-amd64-unwind.obj") ==
                          COFF_WRITER_OK);
    free(image_bytes);
    coff_object_destroy(object);
    ct_list_free(ct_caller_list);
    ct_caller_list = previous_caller_list;
    return true;
}

static bool coff_amd64_test_tls_object(void) {
    uint8_t plan[20U + 5U * 8U] = {0};
    memcpy(plan, "NWU1", 4U);
    coff_amd64_test_put_u16(plan + 4U, 20U);
    coff_amd64_test_put_u16(plan + 6U, 5U);
    coff_amd64_test_put_u32(plan + 8U, 80U);
    coff_amd64_test_put_u32(plan + 12U, 32U);
    plan[16] = 5U;
    coff_amd64_test_plan_action(plan + 20U, 1U, 5U, 0U, 0U);
    coff_amd64_test_plan_action(plan + 28U, 3U, 0U, 0U, 80U);
    coff_amd64_test_plan_action(plan + 36U, 2U, 5U, 0U, 80U);
    coff_amd64_test_plan_action(plan + 44U, 4U, 3U, 0U, 32U);
    coff_amd64_test_plan_action(plan + 52U, 5U, 15U, 0U, 48U);

    asm_global_symbol_t private_plan = {
            .name = "__nature_unwind_plan$tls_entry",
            .size = sizeof(plan),
            .value = plan,
    };
    void *global_items[] = {&private_plan};
    slice_t globals = {.count = 1, .capacity = 1, .take = global_items};

    reg_t rbp_reg = {.index = 5U, .size = QWORD};
    reg_t rsp_reg = {.index = 4U, .size = QWORD};
    reg_t rbx_reg = {.index = 3U, .size = QWORD};
    reg_t ebx_reg = {.index = 3U, .size = DWORD};
    reg_t rax_reg = {.index = 0U, .size = QWORD};
    reg_t r10_reg = {.index = 10U, .size = QWORD};
    reg_t r10d_reg = {.index = 10U, .size = DWORD};
    reg_t r11_reg = {.index = 11U, .size = QWORD};
    reg_t r11d_reg = {.index = 11U, .size = DWORD};
    reg_t xmm15_reg = {.index = 15U, .size = QWORD};
    reg_t *old_rbp = rbp;
    reg_t *old_rsp = rsp;
    reg_t *old_rbx = rbx;
    reg_t *old_ebx = ebx;
    reg_t *old_r10 = r10;
    reg_t *old_r10d = r10d;
    reg_t *old_r11 = r11;
    reg_t *old_r11d = r11d;
    rbp = &rbp_reg;
    rsp = &rsp_reg;
    rbx = &rbx_reg;
    ebx = &ebx_reg;
    r10 = &r10_reg;
    r10d = &r10d_reg;
    r11 = &r11_reg;
    r11d = &r11d_reg;

    asm_symbol_t entry_symbol = {
            .name = "tls_entry",
            .is_local = false,
    };
    asm_symbol_t tls_symbol = {
            .name = "tls_value",
            .is_local = false,
            .is_tls = true,
    };
    asm_uint32_t frame_immediate = {.value = 80U};
    asm_sib_reg_t rbx_slot = {
            .base = &rsp_reg,
            .disp = 32,
    };
    asm_sib_reg_t frame_pointer = {
            .base = &rsp_reg,
            .disp = 80,
    };
    asm_sib_reg_t xmm_slot = {
            .base = &rsp_reg,
            .disp = 48,
    };
    amd64_asm_operand_t label_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .value = &entry_symbol,
    };
    amd64_asm_operand_t rbp_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = QWORD,
            .value = &rbp_reg,
    };
    amd64_asm_operand_t rsp_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = QWORD,
            .value = &rsp_reg,
    };
    amd64_asm_operand_t frame_pointer_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = QWORD,
            .value = &frame_pointer,
    };
    amd64_asm_operand_t rbx_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = QWORD,
            .value = &rbx_reg,
    };
    amd64_asm_operand_t rax_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = QWORD,
            .value = &rax_reg,
    };
    amd64_asm_operand_t xmm15_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_FREG,
            .size = QWORD,
            .value = &xmm15_reg,
    };
    amd64_asm_operand_t frame_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_UINT32,
            .size = DWORD,
            .value = &frame_immediate,
    };
    amd64_asm_operand_t rbx_slot_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = QWORD,
            .value = &rbx_slot,
    };
    amd64_asm_operand_t xmm_slot_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = QWORD,
            .value = &xmm_slot,
    };
    amd64_asm_operand_t tls_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .size = QWORD,
            .value = &tls_symbol,
    };

    amd64_asm_inst_t label = {
            .name = "label", .operands = {&label_operand}, .count = 1U};
    amd64_asm_inst_t push = {
            .name = "push", .operands = {&rbp_operand}, .count = 1U};
    amd64_asm_inst_t set_frame = {
            .name = "lea",
            .operands = {&rbp_operand, &frame_pointer_operand},
            .count = 2U,
    };
    amd64_asm_inst_t allocate = {
            .name = "sub",
            .operands = {&rsp_operand, &frame_operand},
            .count = 2U,
    };
    amd64_asm_inst_t save_rbx = {
            .name = "mov",
            .operands = {&rbx_slot_operand, &rbx_operand},
            .count = 2U,
    };
    amd64_asm_inst_t save_xmm = {
            .name = "movxmm128",
            .operands = {&xmm_slot_operand, &xmm15_operand},
            .count = 2U,
    };
    amd64_asm_inst_t load_tls = {
            .name = "mov",
            .operands = {&rax_operand, &tls_operand},
            .count = 2U,
    };
    amd64_asm_inst_t restore_xmm = {
            .name = "movxmm128",
            .operands = {&xmm15_operand, &xmm_slot_operand},
            .count = 2U,
    };
    amd64_asm_inst_t restore_rbx = {
            .name = "mov",
            .operands = {&rbx_operand, &rbx_slot_operand},
            .count = 2U,
    };
    amd64_asm_inst_t deallocate = {
            .name = "add",
            .operands = {&rsp_operand, &frame_operand},
            .count = 2U,
    };
    amd64_asm_inst_t pop = {
            .name = "pop", .operands = {&rbp_operand}, .count = 1U};
    amd64_asm_inst_t ret = {.name = "ret", .count = 0U};
    void *operation_items[] = {
            &label,       &push,        &allocate,   &set_frame,
            &save_rbx,    &save_xmm,    &load_tls,   &restore_xmm,
            &restore_rbx, &deallocate,  &pop,        &ret,
    };
    slice_t operations = {
            .count = (int) (sizeof(operation_items) /
                            sizeof(operation_items[0])),
            .capacity = (int) (sizeof(operation_items) /
                               sizeof(operation_items[0])),
            .take = operation_items,
    };
    closure_t closure = {
            .linkident = "tls_entry",
            .asm_operations = &operations,
    };
    void *closure_items[] = {&closure};
    slice_t closures = {.count = 1, .capacity = 1, .take = closure_items};
    module_t module;
    memset(&module, 0, sizeof(module));
    module.source_path = "coff_amd64_tls_test.n";
    module.asm_global_symbols = &globals;
    module.closures = &closures;

    coff_object_t *object = coff_object_create_amd64(module.source_path);
    COFF_AMD64_TEST_CHECK(object != NULL);
    char error[512] = {0};
    COFF_AMD64_TEST_CHECK(coff_encode_amd64_module_ex(
                                  object, &module, error, sizeof(error)) ==
                          COFF_WRITER_OK);
    COFF_AMD64_TEST_CHECK(error[0] == '\0');
    uint8_t *image_bytes = NULL;
    size_t image_size = 0U;
    COFF_AMD64_TEST_CHECK(coff_object_serialize(
                                  object, &image_bytes, &image_size) ==
                          COFF_WRITER_OK);
    coff_amd64_test_image_t image;
    COFF_AMD64_TEST_CHECK(
            coff_amd64_test_parse(image_bytes, image_size, &image));
    uint32_t text_header = 0U;
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_section(
            &image, ".text", NULL, &text_header));
    uint32_t text_relocations =
            coff_amd64_test_u32(image_bytes + text_header + 24U);
    uint16_t relocation_count =
            coff_amd64_test_u16(image_bytes + text_header + 32U);
    COFF_AMD64_TEST_CHECK(relocation_count == 2U);
    coff_amd64_test_symbol_t tls_index_symbol;
    coff_amd64_test_symbol_t tls_value_symbol;
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_symbol(
            &image, "_tls_index", &tls_index_symbol));
    COFF_AMD64_TEST_CHECK(coff_amd64_test_find_symbol(
            &image, "tls_value", &tls_value_symbol));
    bool found_index = false;
    bool found_tls = false;
    for (uint16_t i = 0U; i < relocation_count; i++) {
        const uint8_t *relocation = image_bytes + text_relocations +
                                    (size_t) i * LD_COFF_RELOCATION_SIZE;
        uint32_t symbol_index = coff_amd64_test_u32(relocation + 4U);
        uint16_t type = coff_amd64_test_u16(relocation + 8U);
        if (symbol_index == tls_index_symbol.index &&
            type == LD_COFF_REL_AMD64_REL32)
            found_index = true;
        if (symbol_index == tls_value_symbol.index &&
            type == LD_COFF_REL_AMD64_SECREL)
            found_tls = true;
    }
    COFF_AMD64_TEST_CHECK(found_index && found_tls);
    COFF_AMD64_TEST_CHECK(tls_operand.type ==
                          AMD64_ASM_OPERAND_TYPE_SYMBOL);
    COFF_AMD64_TEST_CHECK(tls_operand.value == &tls_symbol);

    free(image_bytes);
    coff_object_destroy(object);
    rbp = old_rbp;
    rsp = old_rsp;
    rbx = old_rbx;
    ebx = old_ebx;
    r10 = old_r10;
    r10d = old_r10d;
    r11 = old_r11;
    r11d = old_r11d;
    return true;
}

int coff_amd64_run_tests(void) {
    return coff_amd64_test_unwind_object() &&
                           coff_amd64_test_tls_object()
                   ? 0
                   : 1;
}

int main(void) {
    const int result = coff_amd64_run_tests();
    if (result == 0)
        fprintf(stderr, "COFF AMD64 unwind tests passed\n");
    return result;
}
#endif
