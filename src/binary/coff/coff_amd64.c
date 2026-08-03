/*
 * Nature AMD64 instruction-to-COFF adapter.
 *
 * COFF relocation selection is a semantic C port of LLVM's AMD64 Windows
 * object-writer behavior. Nature's instruction rewriting mirrors the existing
 * ELF/Mach-O AMD64 assembler paths.
 *
 * Sources:
 *   llvm/lib/MC/WinCOFFObjectWriter.cpp
 *   llvm/lib/Target/X86/MCTargetDesc/X86WinCOFFObjectWriter.cpp
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "coff_amd64.h"

#include "src/binary/encoding/amd64/opcode.h"
#include "src/lir.h"
#include "src/register/arch/amd64.h"
#include "utils/custom_links.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COFF_AMD64_MAX_INSTRUCTION_SIZE 30U
#define COFF_AMD64_UNWIND_PLAN_PREFIX "__nature_unwind_plan$"
#define COFF_AMD64_UNWIND_PLAN_PREFIX_SIZE \
    (sizeof(COFF_AMD64_UNWIND_PLAN_PREFIX) - 1U)

enum {
    COFF_AMD64_UNWIND_PLAN_HEADER_SIZE = 20,
    COFF_AMD64_UNWIND_PLAN_ACTION_SIZE = 8,

    COFF_AMD64_PLAN_PUSH_NONVOL = 1,
    COFF_AMD64_PLAN_SET_FPREG = 2,
    COFF_AMD64_PLAN_ALLOC = 3,
    COFF_AMD64_PLAN_SAVE_NONVOL = 4,
    COFF_AMD64_PLAN_SAVE_XMM128 = 5,

    COFF_AMD64_UWOP_PUSH_NONVOL = 0,
    COFF_AMD64_UWOP_ALLOC_LARGE = 1,
    COFF_AMD64_UWOP_ALLOC_SMALL = 2,
    COFF_AMD64_UWOP_SET_FPREG = 3,
    COFF_AMD64_UWOP_SAVE_NONVOL = 4,
    COFF_AMD64_UWOP_SAVE_NONVOL_FAR = 5,
    COFF_AMD64_UWOP_SAVE_XMM128 = 8,
    COFF_AMD64_UWOP_SAVE_XMM128_FAR = 9,
};

typedef struct {
    uint8_t kind;
    uint8_t reg;
    uint16_t flags;
    uint32_t value;
    uint8_t code_offset;
    bool matched;
} coff_amd64_unwind_action_t;

typedef struct {
    const char *function_name;
    uint32_t frame_size;
    uint32_t outgoing_size;
    uint8_t frame_register;
    uint8_t flags;
    uint16_t action_count;
    coff_amd64_unwind_action_t *actions;
    uint16_t matched_count;
    bool emitted;
} coff_amd64_unwind_plan_t;

typedef struct {
    coff_object_t *object;
    module_t *module;
    coff_section_t *text;
    coff_section_t *data;
    coff_amd64_unwind_plan_t *unwind_plans;
    size_t unwind_plan_count;
    char *error;
    size_t error_capacity;
} coff_amd64_context_t;

static coff_writer_status_t coff_amd64_fail(
        coff_amd64_context_t *context, coff_writer_status_t status,
        const char *format, ...) {
    if (context && context->error && context->error_capacity != 0U) {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(context->error, context->error_capacity, format, arguments);
        va_end(arguments);
    }
    return status;
}

static coff_writer_status_t coff_amd64_writer_status(
        coff_amd64_context_t *context, coff_writer_status_t status) {
    if (status == COFF_WRITER_OK || !context || !context->error ||
        context->error_capacity == 0U)
        return status;
    const char *message = coff_object_last_error(context->object);
    if (message && *message)
        snprintf(context->error, context->error_capacity, "%s", message);
    else
        snprintf(context->error, context->error_capacity, "%s",
                 coff_writer_status_string(status));
    return status;
}

static uint16_t coff_amd64_read_u16(const uint8_t *bytes) {
    return (uint16_t) bytes[0] | (uint16_t) ((uint16_t) bytes[1] << 8U);
}

static uint32_t coff_amd64_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | (uint32_t) bytes[1] << 8U |
           (uint32_t) bytes[2] << 16U | (uint32_t) bytes[3] << 24U;
}

static void coff_amd64_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void coff_amd64_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static bool coff_amd64_is_unwind_plan_name(const char *name) {
    return name &&
           strncmp(name, COFF_AMD64_UNWIND_PLAN_PREFIX,
                   COFF_AMD64_UNWIND_PLAN_PREFIX_SIZE) == 0;
}

static char *coff_amd64_join_name(const char *prefix, const char *name) {
    if (!prefix || !name) return NULL;
    const size_t prefix_size = strlen(prefix);
    const size_t name_size = strlen(name);
    if (name_size > SIZE_MAX - prefix_size - 1U) return NULL;
    char *result = malloc(prefix_size + name_size + 1U);
    if (!result) return NULL;
    memcpy(result, prefix, prefix_size);
    memcpy(result + prefix_size, name, name_size + 1U);
    return result;
}

static void coff_amd64_unwind_plans_deinit(coff_amd64_context_t *context) {
    if (!context) return;
    for (size_t i = 0U; i < context->unwind_plan_count; i++)
        free(context->unwind_plans[i].actions);
    free(context->unwind_plans);
    context->unwind_plans = NULL;
    context->unwind_plan_count = 0U;
}

static coff_amd64_unwind_plan_t *coff_amd64_find_unwind_plan(
        coff_amd64_context_t *context, const char *function_name) {
    if (!context || !function_name) return NULL;
    for (size_t i = 0U; i < context->unwind_plan_count; i++) {
        if (strcmp(context->unwind_plans[i].function_name, function_name) ==
            0)
            return &context->unwind_plans[i];
    }
    return NULL;
}

static coff_writer_status_t coff_amd64_validate_unwind_action(
        coff_amd64_context_t *context, const char *function_name,
        coff_amd64_unwind_plan_t *plan,
        coff_amd64_unwind_action_t *action, uint16_t action_index) {
    if (action->reg > 15U) {
        return coff_amd64_fail(
                context, COFF_WRITER_INVALID_ARGUMENT,
                "unwind plan for '%s' action %u has invalid register %u",
                function_name, action_index, action->reg);
    }
    switch (action->kind) {
        case COFF_AMD64_PLAN_PUSH_NONVOL:
            if (action->flags != 0U || action->value != 0U)
                goto invalid_action;
            break;
        case COFF_AMD64_PLAN_SET_FPREG:
            if (action->flags != 0U || action->value != plan->frame_size ||
                action->reg != plan->frame_register)
                goto invalid_action;
            break;
        case COFF_AMD64_PLAN_ALLOC:
            if ((action->flags & ~UINT16_C(1)) != 0U || action->reg != 0U ||
                action->value != plan->frame_size || action->value < 8U ||
                (action->value & 7U) != 0U)
                goto invalid_action;
            break;
        case COFF_AMD64_PLAN_SAVE_NONVOL:
            if (action->flags != 0U || (action->value & 7U) != 0U ||
                action->value > plan->frame_size ||
                plan->frame_size - action->value < 8U)
                goto invalid_action;
            break;
        case COFF_AMD64_PLAN_SAVE_XMM128:
            if (action->flags != 0U || (action->value & 15U) != 0U ||
                action->value > plan->frame_size ||
                plan->frame_size - action->value < 16U)
                goto invalid_action;
            break;
        default:
            goto invalid_action;
    }
    return COFF_WRITER_OK;

invalid_action:
    return coff_amd64_fail(
            context, COFF_WRITER_INVALID_ARGUMENT,
            "unwind plan for '%s' has invalid action %u", function_name,
            action_index);
}

static coff_writer_status_t coff_amd64_parse_unwind_plan(
        coff_amd64_context_t *context, const asm_global_symbol_t *symbol,
        coff_amd64_unwind_plan_t *plan) {
    if (!symbol->value || symbol->size < COFF_AMD64_UNWIND_PLAN_HEADER_SIZE) {
        return coff_amd64_fail(context, COFF_WRITER_INVALID_ARGUMENT,
                               "unwind plan '%s' is truncated", symbol->name);
    }
    const uint8_t *bytes = symbol->value;
    const char *function_name =
            symbol->name + COFF_AMD64_UNWIND_PLAN_PREFIX_SIZE;
    const uint16_t header_size = coff_amd64_read_u16(bytes + 4U);
    const uint16_t action_count = coff_amd64_read_u16(bytes + 6U);
    const size_t actions_size =
            (size_t) action_count * COFF_AMD64_UNWIND_PLAN_ACTION_SIZE;
    if (!*function_name || memcmp(bytes, "NWU1", 4U) != 0 ||
        header_size != COFF_AMD64_UNWIND_PLAN_HEADER_SIZE ||
        actions_size > SIZE_MAX - header_size ||
        symbol->size != header_size + actions_size || action_count == 0U ||
        coff_amd64_read_u16(bytes + 18U) != 0U || bytes[16] > 15U ||
        (bytes[17] & ~UINT8_C(1)) != 0U) {
        return coff_amd64_fail(context, COFF_WRITER_INVALID_ARGUMENT,
                               "unwind plan '%s' has an invalid header",
                               symbol->name);
    }
    if (coff_amd64_find_unwind_plan(context, function_name)) {
        return coff_amd64_fail(context, COFF_WRITER_DUPLICATE,
                               "duplicate unwind plan for '%s'",
                               function_name);
    }
    memset(plan, 0, sizeof(*plan));
    plan->function_name = function_name;
    plan->action_count = action_count;
    plan->frame_size = coff_amd64_read_u32(bytes + 8U);
    plan->outgoing_size = coff_amd64_read_u32(bytes + 12U);
    plan->frame_register = bytes[16];
    plan->flags = bytes[17];
    if ((plan->frame_size & 7U) != 0U ||
        plan->outgoing_size > plan->frame_size) {
        return coff_amd64_fail(context, COFF_WRITER_INVALID_ARGUMENT,
                               "unwind plan for '%s' has invalid frame sizes",
                               function_name);
    }
    plan->actions = calloc(action_count, sizeof(*plan->actions));
    if (!plan->actions) {
        return coff_amd64_fail(context, COFF_WRITER_OUT_OF_MEMORY,
                               "out of memory parsing unwind plan for '%s'",
                               function_name);
    }
    const uint8_t *action_bytes = bytes + header_size;
    for (uint16_t i = 0U; i < action_count; i++) {
        coff_amd64_unwind_action_t *action = &plan->actions[i];
        action->kind = action_bytes[0];
        action->reg = action_bytes[1];
        action->flags = coff_amd64_read_u16(action_bytes + 2U);
        action->value = coff_amd64_read_u32(action_bytes + 4U);
        coff_writer_status_t status = coff_amd64_validate_unwind_action(
                context, function_name, plan, action, i);
        if (status != COFF_WRITER_OK) return status;
        action_bytes += COFF_AMD64_UNWIND_PLAN_ACTION_SIZE;
    }
    if (action_count < 3U ||
        plan->actions[0].kind != COFF_AMD64_PLAN_PUSH_NONVOL ||
        plan->actions[1].kind != COFF_AMD64_PLAN_ALLOC ||
        plan->actions[2].kind != COFF_AMD64_PLAN_SET_FPREG ||
        (plan->actions[1].flags & 1U) != (plan->flags & 1U)) {
        return coff_amd64_fail(
                context, COFF_WRITER_INVALID_ARGUMENT,
                "unwind plan for '%s' has an invalid prologue prefix",
                function_name);
    }
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_amd64_collect_unwind_plans(
        coff_amd64_context_t *context) {
    size_t count = 0U;
    for (int i = 0; i < context->module->asm_global_symbols->count; i++) {
        const asm_global_symbol_t *symbol =
                context->module->asm_global_symbols->take[i];
        if (symbol && coff_amd64_is_unwind_plan_name(symbol->name)) count++;
    }
    if (count == 0U) return COFF_WRITER_OK;
    context->unwind_plans = calloc(count, sizeof(*context->unwind_plans));
    if (!context->unwind_plans) {
        return coff_amd64_fail(context, COFF_WRITER_OUT_OF_MEMORY,
                               "out of memory collecting AMD64 unwind plans");
    }
    for (int i = 0; i < context->module->asm_global_symbols->count; i++) {
        const asm_global_symbol_t *symbol =
                context->module->asm_global_symbols->take[i];
        if (!symbol || !coff_amd64_is_unwind_plan_name(symbol->name))
            continue;
        coff_amd64_unwind_plan_t *plan =
                &context->unwind_plans[context->unwind_plan_count];
        coff_writer_status_t status =
                coff_amd64_parse_unwind_plan(context, symbol, plan);
        if (status != COFF_WRITER_OK) {
            free(plan->actions);
            plan->actions = NULL;
            return status;
        }
        context->unwind_plan_count++;
    }
    return COFF_WRITER_OK;
}

static bool coff_amd64_is_call(const char *name) {
    return name && strcmp(name, "call") == 0;
}

static void coff_amd64_record_caller(
        closure_t *closure, const amd64_asm_inst_t *operation,
        const asm_symbol_t *target, uint64_t closure_size) {
    if (!ct_caller_list || !coff_amd64_is_call(operation->name) ||
        (target && is_rtcall((char *) target->name)))
        return;

    caller_t caller = {
            .offset = closure_size,
            .line = operation->line,
            .column = operation->column,
            .data = closure,
    };
    if (target) caller.target_name_offset = strtable_put(target->name);
    ct_list_push(ct_caller_list, &caller);
}

static bool coff_amd64_is_jump(const char *name) {
    return name && name[0] == 'j';
}

static bool coff_amd64_is_label(const char *name) {
    return name && strcmp(name, "label") == 0;
}

static amd64_asm_operand_t *coff_amd64_find_symbol_operand(
        const amd64_asm_inst_t *operation) {
    if (!operation) return NULL;
    for (uint8_t i = 0U; i < operation->count; i++) {
        amd64_asm_operand_t *operand = operation->operands[i];
        if (operand && operand->type == AMD64_ASM_OPERAND_TYPE_SYMBOL)
            return operand;
    }
    return NULL;
}

static bool coff_amd64_is_immediate_operand(
        const amd64_asm_operand_t *operand) {
    if (!operand) return false;
    switch (operand->type) {
        case AMD64_ASM_OPERAND_TYPE_UINT:
        case AMD64_ASM_OPERAND_TYPE_UINT8:
        case AMD64_ASM_OPERAND_TYPE_UINT16:
        case AMD64_ASM_OPERAND_TYPE_UINT32:
        case AMD64_ASM_OPERAND_TYPE_UINT64:
        case AMD64_ASM_OPERAND_TYPE_INT8:
        case AMD64_ASM_OPERAND_TYPE_INT32:
            return true;
        default:
            return false;
    }
}

static coff_writer_status_t coff_amd64_force_rel32(
        coff_amd64_context_t *context, amd64_asm_operand_t *operand) {
    asm_uint32_t *relative = calloc(1U, sizeof(*relative));
    if (!relative)
        return coff_amd64_fail(context, COFF_WRITER_OUT_OF_MEMORY,
                               "out of memory lowering AMD64 rel32 operand");
    operand->type = AMD64_ASM_OPERAND_TYPE_UINT32;
    operand->size = 4U;
    operand->value = relative;
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_amd64_force_rip_relative(
        coff_amd64_context_t *context, amd64_asm_operand_t *operand) {
    asm_rip_relative_t *relative = calloc(1U, sizeof(*relative));
    if (!relative)
        return coff_amd64_fail(
                context, COFF_WRITER_OUT_OF_MEMORY,
                "out of memory lowering AMD64 RIP-relative operand");
    operand->type = AMD64_ASM_OPERAND_TYPE_RIP_RELATIVE;
    operand->value = relative;
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_amd64_preflight(
        coff_amd64_context_t *context) {
    if (!context || !context->module || !context->module->closures ||
        !context->module->asm_global_symbols) {
        return coff_amd64_fail(context, COFF_WRITER_INVALID_ARGUMENT,
                               "AMD64 COFF module is not initialized");
    }
    for (int i = 0; i < context->module->asm_global_symbols->count; i++) {
        const asm_global_symbol_t *symbol =
                context->module->asm_global_symbols->take[i];
        if (!symbol || !symbol->name || !*symbol->name) {
            return coff_amd64_fail(
                    context, COFF_WRITER_INVALID_ARGUMENT,
                    "AMD64 COFF module contains an invalid global symbol");
        }
        if (symbol->size > UINT32_MAX) {
            return coff_amd64_fail(
                    context, COFF_WRITER_OVERFLOW,
                    "AMD64 COFF global symbol '%s' exceeds 4 GiB",
                    symbol->name);
        }
    }
    coff_writer_status_t plan_status =
            coff_amd64_collect_unwind_plans(context);
    if (plan_status != COFF_WRITER_OK) return plan_status;
    for (int i = 0; i < context->module->closures->count; i++) {
        const closure_t *closure = context->module->closures->take[i];
        if (!closure || !closure->asm_operations) {
            return coff_amd64_fail(
                    context, COFF_WRITER_INVALID_ARGUMENT,
                    "AMD64 COFF module contains an invalid closure");
        }
        for (int j = 0; j < closure->asm_operations->count; j++) {
            const amd64_asm_inst_t *operation =
                    closure->asm_operations->take[j];
            if (!operation || !operation->name || !*operation->name) {
                return coff_amd64_fail(
                        context, COFF_WRITER_INVALID_ARGUMENT,
                        "AMD64 COFF closure contains an invalid instruction");
            }
            amd64_asm_operand_t *operand =
                    coff_amd64_find_symbol_operand(operation);
            if (!operand) {
                if (coff_amd64_is_label(operation->name)) {
                    return coff_amd64_fail(
                            context, COFF_WRITER_INVALID_ARGUMENT,
                            "AMD64 label instruction has no symbol operand");
                }
                continue;
            }
            const asm_symbol_t *symbol = operand->value;
            if (!symbol || !symbol->name || !*symbol->name) {
                return coff_amd64_fail(
                        context, COFF_WRITER_INVALID_ARGUMENT,
                        "AMD64 instruction has an invalid symbol operand");
            }
        }
    }
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_amd64_emit_globals(
        coff_amd64_context_t *context) {
    for (int i = 0; i < context->module->asm_global_symbols->count; i++) {
        const asm_global_symbol_t *symbol =
                context->module->asm_global_symbols->take[i];
        if (coff_amd64_is_unwind_plan_name(symbol->name)) continue;
        uint32_t offset = 0U;
        coff_writer_status_t status = coff_section_append(
                context->data, symbol->value, symbol->size, 1U, &offset);
        if (status != COFF_WRITER_OK)
            return coff_amd64_writer_status(context, status);
        status = coff_object_define_symbol(
                context->object, symbol->name, context->data, offset, 0U,
                LD_COFF_STORAGE_CLASS_EXTERNAL, NULL);
        if (status != COFF_WRITER_OK)
            return coff_amd64_writer_status(context, status);
    }
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_amd64_define_label(
        coff_amd64_context_t *context, amd64_asm_inst_t *operation) {
    amd64_asm_operand_t *operand =
            coff_amd64_find_symbol_operand(operation);
    asm_symbol_t *symbol = operand ? operand->value : NULL;
    if (!symbol) {
        return coff_amd64_fail(context, COFF_WRITER_INVALID_ARGUMENT,
                               "AMD64 label has no symbol");
    }
    const uint8_t storage_class = symbol->is_local
                                          ? LD_COFF_STORAGE_CLASS_STATIC
                                          : LD_COFF_STORAGE_CLASS_EXTERNAL;
    coff_writer_status_t status = coff_object_define_symbol(
            context->object, symbol->name, context->text,
            coff_section_size(context->text), COFF_SYMBOL_TYPE_FUNCTION,
            storage_class, NULL);
    return coff_amd64_writer_status(context, status);
}

static coff_writer_status_t coff_amd64_relocation_type(
        coff_amd64_context_t *context, const amd64_asm_inst_t *operation,
        uint8_t encoded_size, bool control_transfer, uint32_t *field_offset,
        uint16_t *relocation_type) {
    uint32_t trailing = 0U;
    if (!control_transfer && operation->count != 0U) {
        const amd64_asm_operand_t *last =
                operation->operands[operation->count - 1U];
        if (coff_amd64_is_immediate_operand(last)) trailing = last->size;
    }
    if (trailing > 5U || encoded_size < 4U + trailing) {
        return coff_amd64_fail(
                context, COFF_WRITER_UNSUPPORTED,
                "AMD64 RIP relocation for '%s' has %u trailing bytes; COFF "
                "supports at most five",
                operation->name, trailing);
    }
    *field_offset = (uint32_t) encoded_size - 4U - trailing;
    *relocation_type =
            (uint16_t) (LD_COFF_REL_AMD64_REL32 + trailing);
    return COFF_WRITER_OK;
}

static bool coff_amd64_register_operand(const amd64_asm_operand_t *operand,
                                        uint8_t *index) {
    if (!operand ||
        (operand->type != AMD64_ASM_OPERAND_TYPE_REG &&
         operand->type != AMD64_ASM_OPERAND_TYPE_FREG) ||
        !operand->value)
        return false;
    const reg_t *reg = operand->value;
    if (index) *index = reg->index;
    return true;
}

static bool coff_amd64_rsp_slot_operand(const amd64_asm_operand_t *operand,
                                        uint32_t *offset) {
    if (!operand || operand->type != AMD64_ASM_OPERAND_TYPE_SIB_REG ||
        !operand->value)
        return false;
    const asm_sib_reg_t *slot = operand->value;
    if (!slot->base || slot->base->index != 4U || slot->index ||
        slot->disp < 0)
        return false;
    if (offset) *offset = (uint32_t) slot->disp;
    return true;
}

static bool coff_amd64_instruction_matches_action(
        const amd64_asm_inst_t *operation,
        const coff_amd64_unwind_action_t *action) {
    uint8_t first_reg = 0U;
    uint8_t second_reg = 0U;
    uint32_t stack_offset = 0U;
    switch (action->kind) {
        case COFF_AMD64_PLAN_PUSH_NONVOL:
            return strcmp(operation->name, "push") == 0 &&
                   operation->count == 1U &&
                   coff_amd64_register_operand(operation->operands[0],
                                               &first_reg) &&
                   first_reg == action->reg;
        case COFF_AMD64_PLAN_SET_FPREG:
            return strcmp(operation->name, "lea") == 0 &&
                   operation->count == 2U &&
                   coff_amd64_register_operand(operation->operands[0],
                                               &first_reg) &&
                   coff_amd64_rsp_slot_operand(operation->operands[1],
                                               &stack_offset) &&
                   first_reg == action->reg &&
                   stack_offset == action->value;
        case COFF_AMD64_PLAN_ALLOC:
            return strcmp(operation->name, "sub") == 0 &&
                   operation->count == 2U &&
                   coff_amd64_register_operand(operation->operands[0],
                                               &first_reg) &&
                   first_reg == 4U;
        case COFF_AMD64_PLAN_SAVE_NONVOL:
            return strcmp(operation->name, "mov") == 0 &&
                   operation->count == 2U &&
                   coff_amd64_rsp_slot_operand(operation->operands[0],
                                               &stack_offset) &&
                   coff_amd64_register_operand(operation->operands[1],
                                               &second_reg) &&
                   stack_offset == action->value &&
                   second_reg == action->reg;
        case COFF_AMD64_PLAN_SAVE_XMM128:
            return strcmp(operation->name, "movxmm128") == 0 &&
                   operation->count == 2U &&
                   coff_amd64_rsp_slot_operand(operation->operands[0],
                                               &stack_offset) &&
                   coff_amd64_register_operand(operation->operands[1],
                                               &second_reg) &&
                   stack_offset == action->value &&
                   second_reg == action->reg;
        default:
            return false;
    }
}

static coff_writer_status_t coff_amd64_note_unwind_instruction(
        coff_amd64_context_t *context, coff_amd64_unwind_plan_t *plan,
        const amd64_asm_inst_t *operation, uint64_t instruction_end) {
    if (!plan || plan->matched_count == plan->action_count)
        return COFF_WRITER_OK;
    coff_amd64_unwind_action_t *action =
            &plan->actions[plan->matched_count];
    if (!coff_amd64_instruction_matches_action(operation, action)) {
        if (action->kind == COFF_AMD64_PLAN_ALLOC &&
            (action->flags & 1U) != 0U &&
            (strcmp(operation->name, "mov") == 0 ||
             strcmp(operation->name, "call") == 0))
            return COFF_WRITER_OK;
        return coff_amd64_fail(
                context, COFF_WRITER_INVALID_STATE,
                "instruction '%s' does not match unwind action %u for '%s'",
                operation->name, plan->matched_count, plan->function_name);
    }
    if (instruction_end > UINT8_MAX) {
        return coff_amd64_fail(
                context, COFF_WRITER_UNSUPPORTED,
                "prologue for '%s' exceeds the Windows 255-byte unwind limit",
                plan->function_name);
    }
    action->code_offset = (uint8_t) instruction_end;
    action->matched = true;
    plan->matched_count++;
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_amd64_emit_encoded_instruction(
        coff_amd64_context_t *context, closure_t *closure,
        amd64_asm_inst_t *operation, uint64_t *closure_size,
        coff_amd64_unwind_plan_t *unwind_plan,
        uint32_t *instruction_offset_out, uint8_t *encoded_size_out) {
    uint8_t encoded[COFF_AMD64_MAX_INSTRUCTION_SIZE] = {0};
    uint8_t encoded_size = 0U;
    amd64_opcode_inst_t *matched = amd64_asm_inst_encoding(
            *operation, encoded, &encoded_size, closure);
    if (!matched || encoded_size == 0U ||
        encoded_size > COFF_AMD64_MAX_INSTRUCTION_SIZE) {
        return coff_amd64_fail(
                context, COFF_WRITER_INVALID_STATE,
                "cannot encode AMD64 instruction '%s' for COFF",
                operation->name);
    }
    uint32_t instruction_offset = 0U;
    coff_writer_status_t status = coff_section_append(
            context->text, encoded, encoded_size, 1U, &instruction_offset);
    if (status != COFF_WRITER_OK)
        return coff_amd64_writer_status(context, status);
    if (*closure_size > UINT64_MAX - encoded_size) {
        return coff_amd64_fail(context, COFF_WRITER_OVERFLOW,
                               "AMD64 closure size overflows");
    }
    *closure_size += encoded_size;
    status = coff_amd64_note_unwind_instruction(
            context, unwind_plan, operation, *closure_size);
    if (status != COFF_WRITER_OK) return status;
    if (instruction_offset_out) *instruction_offset_out = instruction_offset;
    if (encoded_size_out) *encoded_size_out = encoded_size;
    return COFF_WRITER_OK;
}

static bool coff_amd64_operand_uses_gp_register(
        const amd64_asm_operand_t *operand, uint8_t index) {
    if (!operand || !operand->value) return false;
    switch (operand->type) {
        case AMD64_ASM_OPERAND_TYPE_REG:
            return ((const reg_t *) operand->value)->index == index;
        case AMD64_ASM_OPERAND_TYPE_INDIRECT_REG:
            return ((const asm_indirect_reg_t *) operand->value)->reg->index ==
                   index;
        case AMD64_ASM_OPERAND_TYPE_DISP_REG:
            return ((const asm_disp_reg_t *) operand->value)->reg->index ==
                   index;
        case AMD64_ASM_OPERAND_TYPE_SIB_REG: {
            const asm_sib_reg_t *sib = operand->value;
            return (sib->base && sib->base->index == index) ||
                   (sib->index && sib->index->index == index);
        }
        default:
            return false;
    }
}

static bool coff_amd64_operation_uses_gp_register(
        const amd64_asm_inst_t *operation, uint8_t index) {
    for (uint8_t i = 0U; i < operation->count; i++) {
        if (coff_amd64_operand_uses_gp_register(operation->operands[i],
                                                index))
            return true;
    }
    return false;
}

static bool coff_amd64_tls_scratch_offset(
        const coff_amd64_unwind_plan_t *plan, uint32_t *offset) {
    if (!plan || !offset) return false;
    uint64_t end = plan->outgoing_size;
    for (uint16_t i = 0U; i < plan->action_count; i++) {
        const coff_amd64_unwind_action_t *action = &plan->actions[i];
        uint32_t width = 0U;
        if (action->kind == COFF_AMD64_PLAN_SAVE_NONVOL)
            width = 8U;
        else if (action->kind == COFF_AMD64_PLAN_SAVE_XMM128)
            width = 16U;
        if (width != 0U && action->value + (uint64_t) width > end)
            end = action->value + (uint64_t) width;
    }
    if (end > UINT32_MAX || end + 16U > plan->frame_size) return false;
    *offset = (uint32_t) end;
    return true;
}

static amd64_asm_operand_t coff_amd64_make_register_operand(reg_t *reg) {
    return (amd64_asm_operand_t) {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = reg->size,
            .value = reg,
    };
}

static amd64_asm_operand_t coff_amd64_sib_operand(
        asm_sib_reg_t *sib, uint8_t size) {
    return (amd64_asm_operand_t) {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = size,
            .value = sib,
    };
}

static coff_writer_status_t coff_amd64_emit_instruction(
        coff_amd64_context_t *context, closure_t *closure,
        amd64_asm_inst_t *operation, uint64_t *closure_size,
        coff_amd64_unwind_plan_t *unwind_plan);

static coff_writer_status_t coff_amd64_emit_tls_instruction(
        coff_amd64_context_t *context, closure_t *closure,
        amd64_asm_inst_t *operation, amd64_asm_operand_t *tls_operand,
        const asm_symbol_t *tls_symbol, uint64_t *closure_size,
        coff_amd64_unwind_plan_t *unwind_plan) {
    uint32_t scratch_offset = 0U;
    if (!coff_amd64_tls_scratch_offset(unwind_plan, &scratch_offset)) {
        return coff_amd64_fail(
                context, COFF_WRITER_INVALID_STATE,
                "Windows AMD64 TLS access to '%s' requires a fixed scratch "
                "slot in the unwind plan",
                tls_symbol->name);
    }
    struct {
        reg_t *qword;
        reg_t *dword;
    } candidates[] = {
            {rbx, ebx},
            {r11, r11d},
            {r10, r10d},
    };
    reg_t *base = NULL;
    reg_t *index = NULL;
    reg_t *index32 = NULL;
    for (size_t i = 0U; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (coff_amd64_operation_uses_gp_register(
                    operation, candidates[i].qword->index))
            continue;
        if (!base) {
            base = candidates[i].qword;
            continue;
        }
        index = candidates[i].qword;
        index32 = candidates[i].dword;
        break;
    }
    if (!base || !index || !index32) {
        return coff_amd64_fail(
                context, COFF_WRITER_UNSUPPORTED,
                "Windows AMD64 TLS access to '%s' leaves fewer than two "
                "scratch registers",
                tls_symbol->name);
    }

    asm_sib_reg_t spill_sib = {
            .base = rsp,
            .index = NULL,
            .scale = 0U,
            .disp = (int32_t) scratch_offset,
    };
    amd64_asm_operand_t spill =
            coff_amd64_sib_operand(&spill_sib, QWORD);
    amd64_asm_operand_t base_op =
            coff_amd64_make_register_operand(base);
    amd64_asm_inst_t save_base = {
            .name = "mov",
            .operands = {&spill, &base_op},
            .count = 2U,
    };
    coff_writer_status_t status = coff_amd64_emit_encoded_instruction(
            context, closure, &save_base, closure_size, unwind_plan, NULL,
            NULL);
    if (status != COFF_WRITER_OK) return status;

    asm_sib_reg_t index_spill_sib = {
            .base = rsp,
            .index = NULL,
            .scale = 0U,
            .disp = (int32_t) scratch_offset + 8,
    };
    amd64_asm_operand_t index_spill =
            coff_amd64_sib_operand(&index_spill_sib, QWORD);
    amd64_asm_operand_t index_op =
            coff_amd64_make_register_operand(index);
    amd64_asm_inst_t save_index = {
            .name = "mov",
            .operands = {&index_spill, &index_op},
            .count = 2U,
    };
    status = coff_amd64_emit_encoded_instruction(
            context, closure, &save_index, closure_size, unwind_plan, NULL,
            NULL);
    if (status != COFF_WRITER_OK) return status;

    asm_seg_offset_t tls_array = {.name = "gs", .offset = 0x58};
    amd64_asm_operand_t tls_array_op = {
            .type = AMD64_ASM_OPERAND_TYPE_SEG_OFFSET,
            .size = QWORD,
            .value = &tls_array,
    };
    amd64_asm_inst_t load_array = {
            .name = "mov",
            .operands = {&base_op, &tls_array_op},
            .count = 2U,
    };
    status = coff_amd64_emit_encoded_instruction(
            context, closure, &load_array, closure_size, unwind_plan, NULL,
            NULL);
    if (status != COFF_WRITER_OK) return status;

    asm_symbol_t index_symbol = {
            .name = "_tls_index",
            .is_local = false,
            .is_tls = false,
    };
    amd64_asm_operand_t index_reference = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .size = DWORD,
            .value = &index_symbol,
    };
    amd64_asm_operand_t index32_op =
            coff_amd64_make_register_operand(index32);
    amd64_asm_inst_t load_index = {
            .name = "mov",
            .operands = {&index32_op, &index_reference},
            .count = 2U,
    };
    status = coff_amd64_emit_instruction(
            context, closure, &load_index, closure_size, unwind_plan);
    if (status != COFF_WRITER_OK) return status;

    asm_sib_reg_t block_sib = {
            .base = base,
            .index = index,
            .scale = 8U,
            .disp = 0,
    };
    amd64_asm_operand_t block =
            coff_amd64_sib_operand(&block_sib, QWORD);
    amd64_asm_inst_t load_block = {
            .name = "mov",
            .operands = {&index_op, &block},
            .count = 2U,
    };
    status = coff_amd64_emit_encoded_instruction(
            context, closure, &load_block, closure_size, unwind_plan, NULL,
            NULL);
    if (status != COFF_WRITER_OK) return status;

    asm_uint32_t zero = {.value = 0U};
    amd64_asm_operand_t tls_offset = {
            .type = AMD64_ASM_OPERAND_TYPE_UINT32,
            .size = DWORD,
            .value = &zero,
    };
    amd64_asm_inst_t add_offset = {
            .name = "add",
            .operands = {&index_op, &tls_offset},
            .count = 2U,
    };
    uint32_t add_instruction_offset = 0U;
    uint8_t add_instruction_size = 0U;
    status = coff_amd64_emit_encoded_instruction(
            context, closure, &add_offset, closure_size, unwind_plan,
            &add_instruction_offset, &add_instruction_size);
    if (status != COFF_WRITER_OK) return status;
    if (add_instruction_size < 4U ||
        add_instruction_offset >
                UINT32_MAX - (uint32_t) (add_instruction_size - 4U)) {
        return coff_amd64_fail(context, COFF_WRITER_OVERFLOW,
                               "AMD64 COFF TLS relocation offset overflows");
    }
    uint32_t tls_symbol_index = COFF_SYMBOL_INDEX_NONE;
    status = coff_object_get_or_add_symbol_reference(
            context->object, tls_symbol->name, !tls_symbol->is_local, 0U,
            &tls_symbol_index);
    if (status != COFF_WRITER_OK)
        return coff_amd64_writer_status(context, status);
    status = coff_section_add_relocation_with_addend(
            context->text,
            add_instruction_offset + add_instruction_size - 4U,
            tls_symbol_index, LD_COFF_REL_AMD64_SECREL, 0);
    if (status != COFF_WRITER_OK)
        return coff_amd64_writer_status(context, status);

    asm_indirect_reg_t indirect = {.reg = index};
    amd64_asm_operand_type original_type = tls_operand->type;
    void *original_value = tls_operand->value;
    tls_operand->type = AMD64_ASM_OPERAND_TYPE_INDIRECT_REG;
    tls_operand->value = &indirect;
    status = coff_amd64_emit_encoded_instruction(
            context, closure, operation, closure_size, unwind_plan, NULL,
            NULL);
    tls_operand->type = original_type;
    tls_operand->value = original_value;
    if (status != COFF_WRITER_OK) return status;

    amd64_asm_inst_t restore_index = {
            .name = "mov",
            .operands = {&index_op, &index_spill},
            .count = 2U,
    };
    status = coff_amd64_emit_encoded_instruction(
            context, closure, &restore_index, closure_size, unwind_plan, NULL,
            NULL);
    if (status != COFF_WRITER_OK) return status;
    amd64_asm_inst_t restore_base = {
            .name = "mov",
            .operands = {&base_op, &spill},
            .count = 2U,
    };
    return coff_amd64_emit_encoded_instruction(
            context, closure, &restore_base, closure_size, unwind_plan, NULL,
            NULL);
}

static coff_writer_status_t coff_amd64_emit_instruction(
        coff_amd64_context_t *context, closure_t *closure,
        amd64_asm_inst_t *operation, uint64_t *closure_size,
        coff_amd64_unwind_plan_t *unwind_plan) {
    amd64_asm_operand_t *operand =
            coff_amd64_find_symbol_operand(operation);
    asm_symbol_t *symbol = operand ? operand->value : NULL;
    if (symbol && symbol->is_tls)
        return coff_amd64_emit_tls_instruction(
                context, closure, operation, operand, symbol, closure_size,
                unwind_plan);

    const bool control_transfer =
            symbol && (coff_amd64_is_call(operation->name) ||
                       coff_amd64_is_jump(operation->name));
    uint32_t symbol_index = COFF_SYMBOL_INDEX_NONE;
    coff_writer_status_t status = COFF_WRITER_OK;
    if (symbol) {
        status = coff_object_get_or_add_symbol_reference(
                context->object, symbol->name, !symbol->is_local,
                control_transfer ? COFF_SYMBOL_TYPE_FUNCTION : 0U,
                &symbol_index);
        if (status != COFF_WRITER_OK)
            return coff_amd64_writer_status(context, status);
        status = control_transfer
                         ? coff_amd64_force_rel32(context, operand)
                         : coff_amd64_force_rip_relative(context, operand);
        if (status != COFF_WRITER_OK) return status;
    }

    uint32_t instruction_offset = 0U;
    uint8_t encoded_size = 0U;
    status = coff_amd64_emit_encoded_instruction(
            context, closure, operation, closure_size, unwind_plan,
            &instruction_offset, &encoded_size);
    if (status != COFF_WRITER_OK) return status;
    coff_amd64_record_caller(closure, operation, symbol, *closure_size);
    if (!symbol) return COFF_WRITER_OK;

    uint32_t field_in_instruction = 0U;
    uint16_t relocation_type = 0U;
    status = coff_amd64_relocation_type(
            context, operation, encoded_size, control_transfer,
            &field_in_instruction, &relocation_type);
    if (status != COFF_WRITER_OK) return status;
    if (field_in_instruction > UINT32_MAX - instruction_offset) {
        return coff_amd64_fail(context, COFF_WRITER_OVERFLOW,
                               "AMD64 COFF relocation offset overflows");
    }
    status = coff_section_add_relocation_with_addend(
            context->text, instruction_offset + field_in_instruction,
            symbol_index, relocation_type, 0);
    return status == COFF_WRITER_OK
                   ? COFF_WRITER_OK
                   : coff_amd64_writer_status(context, status);
}

static bool coff_amd64_use_frame_register(
        const coff_amd64_unwind_plan_t *plan) {
    if (!plan || plan->frame_register == 0U || plan->frame_size > 240U ||
        (plan->frame_size & 15U) != 0U)
        return false;
    for (uint16_t i = 0U; i < plan->action_count; i++) {
        if (plan->actions[i].kind == COFF_AMD64_PLAN_SET_FPREG) return true;
    }
    return false;
}

static size_t coff_amd64_unwind_action_slots(
        const coff_amd64_unwind_action_t *action, bool use_frame_register) {
    switch (action->kind) {
        case COFF_AMD64_PLAN_PUSH_NONVOL:
            return 1U;
        case COFF_AMD64_PLAN_SET_FPREG:
            return use_frame_register ? 1U : 0U;
        case COFF_AMD64_PLAN_ALLOC:
            if (action->value <= 128U) return 1U;
            return action->value / 8U <= UINT16_MAX ? 2U : 3U;
        case COFF_AMD64_PLAN_SAVE_NONVOL:
            return action->value / 8U <= UINT16_MAX ? 2U : 3U;
        case COFF_AMD64_PLAN_SAVE_XMM128:
            return action->value / 16U <= UINT16_MAX ? 2U : 3U;
        default:
            return 0U;
    }
}

static coff_writer_status_t coff_amd64_encode_unwind_info(
        coff_amd64_context_t *context, coff_amd64_unwind_plan_t *plan,
        uint8_t **result, size_t *result_size) {
    *result = NULL;
    *result_size = 0U;
    if (plan->matched_count != plan->action_count) {
        return coff_amd64_fail(
                context, COFF_WRITER_INVALID_STATE,
                "unwind plan for '%s' matched %u of %u actions",
                plan->function_name, plan->matched_count, plan->action_count);
    }
    const bool use_frame_register =
            coff_amd64_use_frame_register(plan);
    size_t slot_count = 0U;
    uint8_t previous_offset = 0U;
    for (uint16_t i = 0U; i < plan->action_count; i++) {
        const coff_amd64_unwind_action_t *action = &plan->actions[i];
        if (!action->matched || action->code_offset < previous_offset) {
            return coff_amd64_fail(
                    context, COFF_WRITER_INVALID_STATE,
                    "unwind action offsets for '%s' are not monotonic",
                    plan->function_name);
        }
        previous_offset = action->code_offset;
        const size_t action_slots =
                coff_amd64_unwind_action_slots(action, use_frame_register);
        if (action_slots > UINT8_MAX - slot_count) {
            return coff_amd64_fail(
                    context, COFF_WRITER_UNSUPPORTED,
                    "unwind codes for '%s' exceed the Windows 255-slot limit",
                    plan->function_name);
        }
        slot_count += action_slots;
    }
    const size_t padded_slot_count = (slot_count + 1U) & ~(size_t) 1U;
    if (padded_slot_count > (SIZE_MAX - 4U) / 2U) {
        return coff_amd64_fail(context, COFF_WRITER_OVERFLOW,
                               "unwind info size overflows");
    }
    const size_t size = 4U + padded_slot_count * 2U;
    uint8_t *bytes = calloc(1U, size);
    if (!bytes) {
        return coff_amd64_fail(context, COFF_WRITER_OUT_OF_MEMORY,
                               "out of memory encoding unwind info for '%s'",
                               plan->function_name);
    }
    bytes[0] = 1U; /* Version 1, no exception/termination/chained flags. */
    bytes[1] = previous_offset;
    bytes[2] = (uint8_t) slot_count;
    if (use_frame_register) {
        bytes[3] = (uint8_t) (plan->frame_register |
                              ((plan->frame_size / 16U) << 4U));
    }

    uint8_t *codes = bytes + 4U;
    size_t cursor = 0U;
    for (size_t i = plan->action_count; i > 0U; i--) {
        const coff_amd64_unwind_action_t *action = &plan->actions[i - 1U];
        if (action->kind == COFF_AMD64_PLAN_SET_FPREG &&
            !use_frame_register)
            continue;
        uint8_t unwind_op = 0U;
        uint8_t op_info = 0U;
        switch (action->kind) {
            case COFF_AMD64_PLAN_PUSH_NONVOL:
                unwind_op = COFF_AMD64_UWOP_PUSH_NONVOL;
                op_info = action->reg;
                break;
            case COFF_AMD64_PLAN_SET_FPREG:
                unwind_op = COFF_AMD64_UWOP_SET_FPREG;
                break;
            case COFF_AMD64_PLAN_ALLOC:
                if (action->value <= 128U) {
                    unwind_op = COFF_AMD64_UWOP_ALLOC_SMALL;
                    op_info = (uint8_t) ((action->value - 8U) / 8U);
                } else if (action->value / 8U <= UINT16_MAX) {
                    unwind_op = COFF_AMD64_UWOP_ALLOC_LARGE;
                    op_info = 0U;
                } else {
                    unwind_op = COFF_AMD64_UWOP_ALLOC_LARGE;
                    op_info = 1U;
                }
                break;
            case COFF_AMD64_PLAN_SAVE_NONVOL:
                unwind_op = action->value / 8U <= UINT16_MAX
                                    ? COFF_AMD64_UWOP_SAVE_NONVOL
                                    : COFF_AMD64_UWOP_SAVE_NONVOL_FAR;
                op_info = action->reg;
                break;
            case COFF_AMD64_PLAN_SAVE_XMM128:
                unwind_op = action->value / 16U <= UINT16_MAX
                                    ? COFF_AMD64_UWOP_SAVE_XMM128
                                    : COFF_AMD64_UWOP_SAVE_XMM128_FAR;
                op_info = action->reg;
                break;
            default:
                free(bytes);
                return coff_amd64_fail(
                        context, COFF_WRITER_INVALID_STATE,
                        "unknown unwind action while encoding '%s'",
                        plan->function_name);
        }
        codes[cursor * 2U] = action->code_offset;
        codes[cursor * 2U + 1U] =
                (uint8_t) (unwind_op | (op_info << 4U));
        cursor++;
        if (action->kind == COFF_AMD64_PLAN_ALLOC &&
            action->value > 128U) {
            if (op_info == 0U) {
                coff_amd64_write_u16(codes + cursor * 2U,
                                     (uint16_t) (action->value / 8U));
                cursor++;
            } else {
                coff_amd64_write_u32(codes + cursor * 2U, action->value);
                cursor += 2U;
            }
        } else if (action->kind == COFF_AMD64_PLAN_SAVE_NONVOL) {
            if (unwind_op == COFF_AMD64_UWOP_SAVE_NONVOL) {
                coff_amd64_write_u16(codes + cursor * 2U,
                                     (uint16_t) (action->value / 8U));
                cursor++;
            } else {
                coff_amd64_write_u32(codes + cursor * 2U, action->value);
                cursor += 2U;
            }
        } else if (action->kind == COFF_AMD64_PLAN_SAVE_XMM128) {
            if (unwind_op == COFF_AMD64_UWOP_SAVE_XMM128) {
                coff_amd64_write_u16(codes + cursor * 2U,
                                     (uint16_t) (action->value / 16U));
                cursor++;
            } else {
                coff_amd64_write_u32(codes + cursor * 2U, action->value);
                cursor += 2U;
            }
        }
    }
    if (cursor != slot_count) {
        free(bytes);
        return coff_amd64_fail(context, COFF_WRITER_INVALID_STATE,
                               "unwind slot count mismatch for '%s'",
                               plan->function_name);
    }
    *result = bytes;
    *result_size = size;
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_amd64_emit_unwind_sections(
        coff_amd64_context_t *context, coff_amd64_unwind_plan_t *plan) {
    uint8_t *unwind_info = NULL;
    size_t unwind_info_size = 0U;
    coff_writer_status_t status = coff_amd64_encode_unwind_info(
            context, plan, &unwind_info, &unwind_info_size);
    if (status != COFF_WRITER_OK) return status;

    char *pdata_name = coff_amd64_join_name(".pdata$", plan->function_name);
    char *xdata_name = coff_amd64_join_name(".xdata$", plan->function_name);
    char *end_name =
            coff_amd64_join_name("__nature_unwind_end$", plan->function_name);
    char *xdata_symbol = coff_amd64_join_name(
            "__nature_unwind_info$", plan->function_name);
    char *pdata_leader =
            coff_amd64_join_name("__nature_pdata$", plan->function_name);
    if (!pdata_name || !xdata_name || !end_name || !xdata_symbol ||
        !pdata_leader) {
        status = coff_amd64_fail(
                context, COFF_WRITER_OUT_OF_MEMORY,
                "out of memory naming unwind sections for '%s'",
                plan->function_name);
        goto cleanup;
    }

    coff_section_t *pdata = NULL;
    coff_section_t *xdata = NULL;
    const uint32_t characteristics =
            LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ;
    status = coff_object_add_section(context->object, pdata_name,
                                     characteristics, 4U, &pdata);
    if (status != COFF_WRITER_OK) goto writer_failure;
    status = coff_object_add_section(context->object, xdata_name,
                                     characteristics, 4U, &xdata);
    if (status != COFF_WRITER_OK) goto writer_failure;
    uint32_t xdata_offset = 0U;
    status = coff_section_append(xdata, unwind_info, unwind_info_size, 4U,
                                 &xdata_offset);
    if (status != COFF_WRITER_OK) goto writer_failure;
    uint32_t pdata_offset = 0U;
    status = coff_section_append_zeros(pdata, 12U, 4U, &pdata_offset);
    if (status != COFF_WRITER_OK) goto writer_failure;
    if (xdata_offset != 0U || pdata_offset != 0U) {
        status = coff_amd64_fail(context, COFF_WRITER_INVALID_STATE,
                                 "new unwind sections are not empty");
        goto cleanup;
    }

    uint32_t function_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t end_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t xdata_index = COFF_SYMBOL_INDEX_NONE;
    status = coff_object_get_or_add_symbol_reference(
            context->object, plan->function_name, true,
            COFF_SYMBOL_TYPE_FUNCTION, &function_index);
    if (status != COFF_WRITER_OK) goto writer_failure;
    status = coff_object_define_symbol(
            context->object, end_name, context->text,
            coff_section_size(context->text), COFF_SYMBOL_TYPE_FUNCTION,
            LD_COFF_STORAGE_CLASS_STATIC, &end_index);
    if (status != COFF_WRITER_OK) goto writer_failure;
    status = coff_object_add_defined_symbol(
            context->object, xdata_symbol, xdata, 0U, 0U,
            LD_COFF_STORAGE_CLASS_STATIC, &xdata_index);
    if (status != COFF_WRITER_OK) goto writer_failure;
    status = coff_section_add_relocation_with_addend(
            pdata, 0U, function_index, LD_COFF_REL_AMD64_ADDR32NB, 0);
    if (status != COFF_WRITER_OK) goto writer_failure;
    status = coff_section_add_relocation_with_addend(
            pdata, 4U, end_index, LD_COFF_REL_AMD64_ADDR32NB, 0);
    if (status != COFF_WRITER_OK) goto writer_failure;
    status = coff_section_add_relocation_with_addend(
            pdata, 8U, xdata_index, LD_COFF_REL_AMD64_ADDR32NB, 0);
    if (status != COFF_WRITER_OK) goto writer_failure;

    status = coff_object_mark_comdat(
            context->object, pdata, LD_COFF_COMDAT_ANY, NULL, pdata_leader,
            0U, NULL, NULL);
    if (status != COFF_WRITER_OK) goto writer_failure;
    status = coff_object_mark_comdat(
            context->object, xdata, LD_COFF_COMDAT_ASSOCIATIVE, pdata, NULL,
            0U, NULL, NULL);
    if (status != COFF_WRITER_OK) goto writer_failure;
    plan->emitted = true;
    goto cleanup;

writer_failure:
    status = coff_amd64_writer_status(context, status);

cleanup:
    free(unwind_info);
    free(pdata_name);
    free(xdata_name);
    free(end_name);
    free(xdata_symbol);
    free(pdata_leader);
    return status;
}

static coff_writer_status_t coff_amd64_emit_text(
        coff_amd64_context_t *context) {
    for (int i = 0; i < context->module->closures->count; i++) {
        closure_t *closure = context->module->closures->take[i];
        coff_amd64_unwind_plan_t *unwind_plan =
                coff_amd64_find_unwind_plan(context, closure->linkident);
        uint64_t closure_size = 0U;
        closure->text_count = 0U;
        for (int j = 0; j < closure->asm_operations->count; j++) {
            amd64_asm_inst_t *operation =
                    closure->asm_operations->take[j];
            coff_writer_status_t status;
            if (coff_amd64_is_label(operation->name))
                status = coff_amd64_define_label(context, operation);
            else
                status = coff_amd64_emit_instruction(
                        context, closure, operation, &closure_size,
                        unwind_plan);
            if (status != COFF_WRITER_OK) return status;
        }
        closure->text_count = closure_size;
        if (unwind_plan) {
            coff_writer_status_t status =
                    coff_amd64_emit_unwind_sections(context, unwind_plan);
            if (status != COFF_WRITER_OK) return status;
        }
    }
    return COFF_WRITER_OK;
}

coff_writer_status_t coff_encode_amd64_module_ex(
        coff_object_t *object, module_t *module, char *error,
        size_t error_capacity) {
    if (error && error_capacity != 0U) error[0] = '\0';
    coff_amd64_context_t context = {
            .object = object,
            .module = module,
            .text = coff_object_text(object),
            .data = coff_object_data(object),
            .error = error,
            .error_capacity = error_capacity,
    };
    if (!object || !module || !context.text || !context.data) {
        return coff_amd64_fail(
                &context, COFF_WRITER_INVALID_ARGUMENT,
                "AMD64 COFF adapter requires an object with .text and .data");
    }
    coff_writer_status_t status = coff_amd64_preflight(&context);
    if (status == COFF_WRITER_OK) status = coff_amd64_emit_globals(&context);
    if (status == COFF_WRITER_OK) status = coff_amd64_emit_text(&context);
    coff_amd64_unwind_plans_deinit(&context);
    return status;
}

coff_writer_status_t coff_encode_amd64_module(coff_object_t *object,
                                              module_t *module) {
    return coff_encode_amd64_module_ex(object, module, NULL, 0U);
}

coff_writer_status_t coff_assembler_module(module_t *module,
                                           const char *output_path,
                                           char *error,
                                           size_t error_capacity) {
    if (error && error_capacity != 0U) error[0] = '\0';
    if (!module || !output_path || !*output_path) {
        if (error && error_capacity != 0U)
            snprintf(error, error_capacity,
                     "COFF module or output path is invalid");
        return COFF_WRITER_INVALID_ARGUMENT;
    }
    coff_object_t *object = coff_object_create_amd64(module->source_path);
    if (!object) {
        if (error && error_capacity != 0U)
            snprintf(error, error_capacity,
                     "out of memory creating AMD64 COFF object");
        return COFF_WRITER_OUT_OF_MEMORY;
    }
    coff_writer_status_t status = coff_encode_amd64_module_ex(
            object, module, error, error_capacity);
    if (status == COFF_WRITER_OK) {
        status = coff_object_write_file(object, output_path);
        if (status != COFF_WRITER_OK && error && error_capacity != 0U)
            snprintf(error, error_capacity, "%s",
                     coff_object_last_error(object));
    }
    if (status == COFF_WRITER_OK) {
        const size_t length = strlen(output_path);
        char *copy = malloc(length + 1U);
        if (!copy) {
            status = COFF_WRITER_OUT_OF_MEMORY;
            if (error && error_capacity != 0U)
                snprintf(error, error_capacity,
                         "out of memory recording COFF object path");
        } else {
            memcpy(copy, output_path, length + 1U);
            module->object_file = copy;
        }
    }
    coff_object_destroy(object);
    return status;
}
