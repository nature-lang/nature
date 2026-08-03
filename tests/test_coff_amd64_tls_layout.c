/*
 * Windows AMD64 TLS layout test for Nature's COFF adapter and PE linker.
 *
 * The fixture is encoded through coff_encode_amd64_module_ex(), defines the
 * referenced variable in .tls$NAT, and is then linked against Nature's bundled
 * MinGW-w64 sysroot without invoking an external linker. The test validates
 * the resulting PE directories and relocations. On a native Windows test host
 * it also executes the image to validate loader TLS behavior.
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "src/binary/coff/coff_amd64.h"
#include "src/binary/encoding/amd64/opcode.h"
#include "src/build/config.h"
#include "src/ld/coff_format.h"
#include "src/ld/ld.h"
#include "src/native/amd64.h"
#include "src/register/register.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NATURE_SOURCE_DIR
#error "NATURE_SOURCE_DIR must name the Nature source tree"
#endif

typedef struct {
    char message[4096];
} diagnostic_capture_t;

typedef struct {
    char name[LD_COFF_NAME_SIZE + 1U];
    uint32_t virtual_size;
    uint32_t rva;
    uint32_t raw_size;
    uint32_t file_offset;
} pe_section_t;

typedef struct {
    uint32_t field_offset;
    const char *symbol_name;
    uint16_t type;
} pending_relocation_t;

typedef struct {
    uint8_t bytes[512];
    uint32_t size;
    pending_relocation_t relocations[24];
    size_t relocation_count;
} code_builder_t;

static void diagnostic(void *context, ld_diag_level_t level,
                       const char *message) {
    (void) level;
    diagnostic_capture_t *capture = context;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

static void put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void code_emit(code_builder_t *builder, const void *bytes,
                      size_t size) {
    assert(size <= sizeof(builder->bytes) - builder->size);
    memcpy(builder->bytes + builder->size, bytes, size);
    builder->size += (uint32_t) size;
}

static void code_emit_u8(code_builder_t *builder, uint8_t value) {
    code_emit(builder, &value, sizeof(value));
}

static uint32_t code_emit_u32(code_builder_t *builder, uint32_t value) {
    uint32_t offset = builder->size;
    uint8_t bytes[4];
    put_u32(bytes, value);
    code_emit(builder, bytes, sizeof(bytes));
    return offset;
}

static uint32_t code_emit_relocation(code_builder_t *builder,
                                     const char *symbol_name,
                                     uint16_t type) {
    assert(builder->relocation_count <
           sizeof(builder->relocations) / sizeof(builder->relocations[0]));
    uint32_t field_offset = code_emit_u32(builder, 0U);
    pending_relocation_t *relocation =
            &builder->relocations[builder->relocation_count++];
    relocation->field_offset = field_offset;
    relocation->symbol_name = symbol_name;
    relocation->type = type;
    return field_offset;
}

static uint32_t code_emit_jcc(code_builder_t *builder, uint8_t condition) {
    code_emit_u8(builder, 0x0fU);
    code_emit_u8(builder, condition);
    return code_emit_u32(builder, 0U);
}

static void code_patch_rel32(code_builder_t *builder, uint32_t field_offset,
                             uint32_t target_offset) {
    assert(field_offset <= builder->size &&
           4U <= builder->size - field_offset);
    int64_t displacement =
            (int64_t) target_offset - ((int64_t) field_offset + 4);
    assert(displacement >= INT32_MIN && displacement <= INT32_MAX);
    put_u32(builder->bytes + field_offset,
            (uint32_t) (int32_t) displacement);
}

static void code_emit_return(code_builder_t *builder, uint32_t result,
                             uint8_t frame_size) {
    code_emit_u8(builder, 0xb8U); /* mov eax, result */
    code_emit_u32(builder, result);
    static const uint8_t add_rsp[] = {0x48U, 0x83U, 0xc4U};
    code_emit(builder, add_rsp, sizeof(add_rsp));
    code_emit_u8(builder, frame_size);
    code_emit_u8(builder, 0xc3U); /* ret */
}

static void code_emit_call(code_builder_t *builder, const char *symbol_name) {
    code_emit_u8(builder, 0xe8U);
    code_emit_relocation(builder, symbol_name, LD_COFF_REL_AMD64_REL32);
}

static void append_function(coff_object_t *object, coff_section_t *text,
                            const char *name, code_builder_t *builder) {
    uint32_t function_offset = UINT32_MAX;
    assert(coff_section_append(text, builder->bytes, builder->size, 16U,
                               &function_offset) == COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, name, text, function_offset,
                   COFF_SYMBOL_TYPE_FUNCTION, LD_COFF_STORAGE_CLASS_EXTERNAL,
                   NULL) == COFF_WRITER_OK);
    for (size_t i = 0U; i < builder->relocation_count; i++) {
        pending_relocation_t *relocation = &builder->relocations[i];
        uint32_t symbol_index = COFF_SYMBOL_INDEX_NONE;
        assert(coff_object_get_or_add_symbol_reference(
                       object, relocation->symbol_name, true,
                       relocation->type == LD_COFF_REL_AMD64_REL32
                               ? COFF_SYMBOL_TYPE_FUNCTION
                               : 0U,
                       &symbol_index) == COFF_WRITER_OK);
        assert(coff_section_add_relocation(
                       text, function_offset + relocation->field_offset,
                       symbol_index, relocation->type) == COFF_WRITER_OK);
    }
}

static void append_tls_store(coff_object_t *object, coff_section_t *text) {
    code_builder_t code = {0};
    static const uint8_t load_tls_array[] = {
            0x65U, 0x48U, 0x8bU, 0x04U, 0x25U,
            0x58U, 0x00U, 0x00U, 0x00U, /* mov rax, gs:[0x58] */
            0x44U, 0x8bU, 0x15U, /* mov r10d, [_tls_index] */
    };
    code_emit(&code, load_tls_array, sizeof(load_tls_array));
    code_emit_relocation(&code, "_tls_index", LD_COFF_REL_AMD64_REL32);
    static const uint8_t load_tls_block[] = {
            0x4aU, 0x8bU, 0x04U, 0xd0U, /* mov rax, [rax+r10*8] */
            0x48U, 0x81U, 0xc0U, /* add rax, tls_value@secrel */
    };
    code_emit(&code, load_tls_block, sizeof(load_tls_block));
    code_emit_relocation(&code, "tls_value", LD_COFF_REL_AMD64_SECREL);
    static const uint8_t store_and_return[] = {
            0x89U, 0x08U, /* mov [rax], ecx */
            0xc3U, /* ret */
    };
    code_emit(&code, store_and_return, sizeof(store_and_return));
    append_function(object, text, "tls_store", &code);
}

static void append_tls_callback(coff_object_t *object, coff_section_t *text) {
    code_builder_t code = {0};
    static const uint8_t compare_process_attach[] = {
            0x83U, 0xfaU, 0x01U, /* cmp edx, DLL_PROCESS_ATTACH */
    };
    code_emit(&code, compare_process_attach, sizeof(compare_process_attach));
    uint32_t process_attach = code_emit_jcc(&code, 0x84U); /* je */
    static const uint8_t compare_thread_attach[] = {
            0x83U, 0xfaU, 0x02U, /* cmp edx, DLL_THREAD_ATTACH */
    };
    code_emit(&code, compare_thread_attach, sizeof(compare_thread_attach));
    uint32_t thread_attach = code_emit_jcc(&code, 0x84U); /* je */
    code_emit_u8(&code, 0xc3U); /* ret */

    uint32_t process_target = code.size;
    static const uint8_t mov_process_count[] = {
            0xc7U, 0x05U, /* mov dword [process_count], 1 */
    };
    code_emit(&code, mov_process_count, sizeof(mov_process_count));
    code_emit_relocation(&code, "callback_process_count",
                         LD_COFF_REL_AMD64_REL32_4);
    code_emit_u32(&code, 1U);
    static const uint8_t mov_process_sequence[] = {
            0xc7U, 0x05U, /* mov dword [callback_sequence], 1 */
    };
    code_emit(&code, mov_process_sequence, sizeof(mov_process_sequence));
    code_emit_relocation(&code, "callback_sequence",
                         LD_COFF_REL_AMD64_REL32_4);
    code_emit_u32(&code, 1U);
    code_emit_u8(&code, 0xc3U); /* ret */

    uint32_t thread_target = code.size;
    static const uint8_t check_process_first[] = {
            0x83U, 0x3dU, /* cmp dword [callback_sequence], 1 */
    };
    code_emit(&code, check_process_first, sizeof(check_process_first));
    code_emit_relocation(&code, "callback_sequence",
                         LD_COFF_REL_AMD64_REL32_1);
    code_emit_u8(&code, 1U);
    uint32_t bad_order = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t mov_thread_count[] = {
            0xc7U, 0x05U, /* mov dword [thread_count], 1 */
    };
    code_emit(&code, mov_thread_count, sizeof(mov_thread_count));
    code_emit_relocation(&code, "callback_thread_count",
                         LD_COFF_REL_AMD64_REL32_4);
    code_emit_u32(&code, 1U);
    static const uint8_t mov_thread_sequence[] = {
            0xc7U, 0x05U, /* mov dword [callback_sequence], 12 */
    };
    code_emit(&code, mov_thread_sequence, sizeof(mov_thread_sequence));
    code_emit_relocation(&code, "callback_sequence",
                         LD_COFF_REL_AMD64_REL32_4);
    code_emit_u32(&code, 12U);
    code_emit_u8(&code, 0xc3U); /* ret */

    uint32_t bad_order_target = code.size;
    static const uint8_t mov_bad_sequence[] = {
            0xc7U, 0x05U, /* mov dword [callback_sequence], -1 */
    };
    code_emit(&code, mov_bad_sequence, sizeof(mov_bad_sequence));
    code_emit_relocation(&code, "callback_sequence",
                         LD_COFF_REL_AMD64_REL32_4);
    code_emit_u32(&code, UINT32_MAX);
    code_emit_u8(&code, 0xc3U); /* ret */

    code_patch_rel32(&code, process_attach, process_target);
    code_patch_rel32(&code, thread_attach, thread_target);
    code_patch_rel32(&code, bad_order, bad_order_target);
    append_function(object, text, "tls_callback", &code);
}

static void append_callback_data(coff_object_t *object) {
    coff_section_t *data = coff_object_data(object);
    assert(data);
    static const char *const names[] = {
            "callback_process_count",
            "callback_thread_count",
            "callback_sequence",
    };
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
        uint32_t offset = UINT32_MAX;
        assert(coff_section_append_zeros(data, sizeof(uint32_t),
                                         sizeof(uint32_t),
                                         &offset) == COFF_WRITER_OK);
        assert(coff_object_define_symbol(
                       object, names[i], data, offset, 0U,
                       LD_COFF_STORAGE_CLASS_EXTERNAL,
                       NULL) == COFF_WRITER_OK);
    }
}

static void append_callback_pointer(coff_object_t *object) {
    coff_section_t *callbacks = NULL;
    assert(coff_object_add_section(
                   object, ".CRT$XLB",
                   LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ,
                   8U, &callbacks) == COFF_WRITER_OK);
    uint32_t pointer_offset = UINT32_MAX;
    assert(coff_section_append_zeros(callbacks, sizeof(uint64_t),
                                     sizeof(uint64_t),
                                     &pointer_offset) == COFF_WRITER_OK);
    uint32_t callback_symbol = COFF_SYMBOL_INDEX_NONE;
    assert(coff_object_get_or_add_symbol_reference(
                   object, "tls_callback", true, COFF_SYMBOL_TYPE_FUNCTION,
                   &callback_symbol) == COFF_WRITER_OK);
    assert(coff_section_add_relocation(callbacks, pointer_offset,
                                       callback_symbol,
                                       LD_COFF_REL_AMD64_ADDR64) ==
           COFF_WRITER_OK);
}

static void append_worker(coff_object_t *object, coff_section_t *text) {
    code_builder_t code = {0};
    static const uint8_t prologue[] = {
            0x48U, 0x83U, 0xecU, 0x28U, /* sub rsp, 40 */
    };
    code_emit(&code, prologue, sizeof(prologue));
    code_emit_call(&code, "tls_load");
    static const uint8_t compare_initial[] = {
            0x83U, 0xf8U, 0x2aU, /* cmp eax, 42 */
    };
    code_emit(&code, compare_initial, sizeof(compare_initial));
    uint32_t initial_failure = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t set_seven[] = {
            0xb9U, 0x07U, 0x00U, 0x00U, 0x00U, /* mov ecx, 7 */
    };
    code_emit(&code, set_seven, sizeof(set_seven));
    code_emit_call(&code, "tls_store");
    code_emit_call(&code, "tls_load");
    static const uint8_t compare_updated[] = {
            0x83U, 0xf8U, 0x07U, /* cmp eax, 7 */
    };
    code_emit(&code, compare_updated, sizeof(compare_updated));
    uint32_t update_failure = code_emit_jcc(&code, 0x85U); /* jne */
    code_emit_return(&code, 0U, 0x28U);
    uint32_t initial_failure_target = code.size;
    code_emit_return(&code, 10U, 0x28U);
    uint32_t update_failure_target = code.size;
    code_emit_return(&code, 11U, 0x28U);
    code_patch_rel32(&code, initial_failure, initial_failure_target);
    code_patch_rel32(&code, update_failure, update_failure_target);
    append_function(object, text, "tls_worker", &code);
}

static void append_thread_main(coff_object_t *object, coff_section_t *text) {
    code_builder_t code = {0};
    static const uint8_t prologue[] = {
            0x48U, 0x83U, 0xecU, 0x58U, /* sub rsp, 88 */
    };
    code_emit(&code, prologue, sizeof(prologue));
    static const uint8_t check_process_count[] = {
            0x83U, 0x3dU, /* cmp dword [callback_process_count], 1 */
    };
    code_emit(&code, check_process_count, sizeof(check_process_count));
    code_emit_relocation(&code, "callback_process_count",
                         LD_COFF_REL_AMD64_REL32_1);
    code_emit_u8(&code, 1U);
    uint32_t process_callback_failure = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t check_process_sequence[] = {
            0x83U, 0x3dU, /* cmp dword [callback_sequence], 1 */
    };
    code_emit(&code, check_process_sequence, sizeof(check_process_sequence));
    code_emit_relocation(&code, "callback_sequence",
                         LD_COFF_REL_AMD64_REL32_1);
    code_emit_u8(&code, 1U);
    uint32_t process_order_failure = code_emit_jcc(&code, 0x85U); /* jne */
    code_emit_call(&code, "tls_load");
    static const uint8_t compare_initial[] = {
            0x83U, 0xf8U, 0x2aU, /* cmp eax, 42 */
    };
    code_emit(&code, compare_initial, sizeof(compare_initial));
    uint32_t initial_failure = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t set_main_value[] = {
            0xb9U, 0x63U, 0x00U, 0x00U, 0x00U, /* mov ecx, 99 */
    };
    code_emit(&code, set_main_value, sizeof(set_main_value));
    code_emit_call(&code, "tls_store");
    static const uint8_t create_arguments[] = {
            0x31U, 0xc9U, /* xor ecx, ecx */
            0x31U, 0xd2U, /* xor edx, edx */
            0x4cU, 0x8dU, 0x05U, /* lea r8, [tls_worker] */
    };
    code_emit(&code, create_arguments, sizeof(create_arguments));
    code_emit_relocation(&code, "tls_worker", LD_COFF_REL_AMD64_REL32);
    static const uint8_t remaining_create_arguments[] = {
            0x45U, 0x31U, 0xc9U, /* xor r9d, r9d */
            0x48U, 0xc7U, 0x44U, 0x24U, 0x20U, 0x00U, 0x00U, 0x00U,
            0x00U, /* mov qword [rsp+32], 0 */
            0x48U, 0xc7U, 0x44U, 0x24U, 0x28U, 0x00U, 0x00U, 0x00U,
            0x00U, /* mov qword [rsp+40], 0 */
    };
    code_emit(&code, remaining_create_arguments,
              sizeof(remaining_create_arguments));
    code_emit_call(&code, "CreateThread");
    static const uint8_t test_handle[] = {
            0x48U, 0x85U, 0xc0U, /* test rax, rax */
    };
    code_emit(&code, test_handle, sizeof(test_handle));
    uint32_t create_failure = code_emit_jcc(&code, 0x84U); /* je */
    static const uint8_t wait_arguments[] = {
            0x48U, 0x89U, 0x44U, 0x24U, 0x30U, /* mov [rsp+48], rax */
            0x48U, 0x89U, 0xc1U, /* mov rcx, rax */
            0xbaU, 0xffU, 0xffU, 0xffU, 0xffU, /* mov edx, INFINITE */
    };
    code_emit(&code, wait_arguments, sizeof(wait_arguments));
    code_emit_call(&code, "WaitForSingleObject");
    static const uint8_t test_wait[] = {
            0x85U, 0xc0U, /* test eax, eax (WAIT_OBJECT_0) */
    };
    code_emit(&code, test_wait, sizeof(test_wait));
    uint32_t wait_failure = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t get_exit_arguments[] = {
            0x48U, 0x8bU, 0x4cU, 0x24U, 0x30U, /* mov rcx, [rsp+48] */
            0x48U, 0x8dU, 0x54U, 0x24U, 0x38U, /* lea rdx, [rsp+56] */
    };
    code_emit(&code, get_exit_arguments, sizeof(get_exit_arguments));
    code_emit_call(&code, "GetExitCodeThread");
    static const uint8_t test_get_exit[] = {
            0x85U, 0xc0U, /* test eax, eax */
    };
    code_emit(&code, test_get_exit, sizeof(test_get_exit));
    uint32_t get_exit_failure = code_emit_jcc(&code, 0x84U); /* je */
    static const uint8_t compare_worker_result[] = {
            0x83U, 0x7cU, 0x24U, 0x38U, 0x00U, /* cmp dword [rsp+56], 0 */
    };
    code_emit(&code, compare_worker_result, sizeof(compare_worker_result));
    uint32_t worker_failure = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t check_thread_count[] = {
            0x83U, 0x3dU, /* cmp dword [callback_thread_count], 1 */
    };
    code_emit(&code, check_thread_count, sizeof(check_thread_count));
    code_emit_relocation(&code, "callback_thread_count",
                         LD_COFF_REL_AMD64_REL32_1);
    code_emit_u8(&code, 1U);
    uint32_t thread_callback_failure = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t check_thread_sequence[] = {
            0x83U, 0x3dU, /* cmp dword [callback_sequence], 12 */
    };
    code_emit(&code, check_thread_sequence, sizeof(check_thread_sequence));
    code_emit_relocation(&code, "callback_sequence",
                         LD_COFF_REL_AMD64_REL32_1);
    code_emit_u8(&code, 12U);
    uint32_t thread_order_failure = code_emit_jcc(&code, 0x85U); /* jne */
    static const uint8_t close_argument[] = {
            0x48U, 0x8bU, 0x4cU, 0x24U, 0x30U, /* mov rcx, [rsp+48] */
    };
    code_emit(&code, close_argument, sizeof(close_argument));
    code_emit_call(&code, "CloseHandle");
    static const uint8_t test_close[] = {
            0x85U, 0xc0U, /* test eax, eax */
    };
    code_emit(&code, test_close, sizeof(test_close));
    uint32_t close_failure = code_emit_jcc(&code, 0x84U); /* je */
    code_emit_call(&code, "tls_load");
    static const uint8_t compare_main_value[] = {
            0x83U, 0xf8U, 0x63U, /* cmp eax, 99 */
    };
    code_emit(&code, compare_main_value, sizeof(compare_main_value));
    uint32_t isolation_failure = code_emit_jcc(&code, 0x85U); /* jne */
    code_emit_return(&code, 0U, 0x58U);

    uint32_t initial_failure_target = code.size;
    code_emit_return(&code, 1U, 0x58U);
    uint32_t create_failure_target = code.size;
    code_emit_return(&code, 2U, 0x58U);
    uint32_t wait_failure_target = code.size;
    code_emit_return(&code, 3U, 0x58U);
    uint32_t get_exit_failure_target = code.size;
    code_emit_return(&code, 4U, 0x58U);
    uint32_t worker_failure_target = code.size;
    code_emit_return(&code, 5U, 0x58U);
    uint32_t close_failure_target = code.size;
    code_emit_return(&code, 6U, 0x58U);
    uint32_t isolation_failure_target = code.size;
    code_emit_return(&code, 7U, 0x58U);
    uint32_t process_callback_failure_target = code.size;
    code_emit_return(&code, 8U, 0x58U);
    uint32_t thread_callback_failure_target = code.size;
    code_emit_return(&code, 9U, 0x58U);
    code_patch_rel32(&code, initial_failure, initial_failure_target);
    code_patch_rel32(&code, create_failure, create_failure_target);
    code_patch_rel32(&code, wait_failure, wait_failure_target);
    code_patch_rel32(&code, get_exit_failure, get_exit_failure_target);
    code_patch_rel32(&code, worker_failure, worker_failure_target);
    code_patch_rel32(&code, close_failure, close_failure_target);
    code_patch_rel32(&code, isolation_failure, isolation_failure_target);
    code_patch_rel32(&code, process_callback_failure,
                     process_callback_failure_target);
    code_patch_rel32(&code, process_order_failure,
                     process_callback_failure_target);
    code_patch_rel32(&code, thread_callback_failure,
                     thread_callback_failure_target);
    code_patch_rel32(&code, thread_order_failure,
                     thread_callback_failure_target);
    append_function(object, text, "main", &code);
}

static uint16_t read_u16(const uint8_t *bytes, size_t size,
                         uint64_t offset) {
    assert(offset <= size && 2U <= (uint64_t) size - offset);
    return (uint16_t) bytes[offset] |
           (uint16_t) ((uint16_t) bytes[offset + 1U] << 8U);
}

static uint32_t read_u32(const uint8_t *bytes, size_t size,
                         uint64_t offset) {
    assert(offset <= size && 4U <= (uint64_t) size - offset);
    return (uint32_t) bytes[offset] |
           (uint32_t) bytes[offset + 1U] << 8U |
           (uint32_t) bytes[offset + 2U] << 16U |
           (uint32_t) bytes[offset + 3U] << 24U;
}

static uint64_t read_u64(const uint8_t *bytes, size_t size,
                         uint64_t offset) {
    return (uint64_t) read_u32(bytes, size, offset) |
           (uint64_t) read_u32(bytes, size, offset + 4U) << 32U;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t) length);
    assert(bytes);
    assert(fread(bytes, 1U, (size_t) length, file) == (size_t) length);
    assert(fclose(file) == 0);
    *size = (size_t) length;
    return bytes;
}

static void plan_action(uint8_t *bytes, uint8_t kind, uint8_t reg,
                        uint32_t value) {
    bytes[0] = kind;
    bytes[1] = reg;
    put_u32(bytes + 4U, value);
}

static void make_tls_main_object(const char *path) {
    enum {
        FRAME_SIZE = 64,
        OUTGOING_SIZE = 32,
        RBX_SAVE_OFFSET = 32,
        ACTION_COUNT = 4,
    };
    uint8_t plan[20U + ACTION_COUNT * 8U] = {0};
    memcpy(plan, "NWU1", 4U);
    put_u16(plan + 4U, 20U);
    put_u16(plan + 6U, ACTION_COUNT);
    put_u32(plan + 8U, FRAME_SIZE);
    put_u32(plan + 12U, OUTGOING_SIZE);
    plan[16] = rbp->index;
    plan_action(plan + 20U, AMD64_WINDOWS_UNWIND_PUSH_NONVOL, rbp->index,
                0U);
    plan_action(plan + 28U, AMD64_WINDOWS_UNWIND_ALLOC, 0U, FRAME_SIZE);
    plan_action(plan + 36U, AMD64_WINDOWS_UNWIND_SET_FPREG, rbp->index,
                FRAME_SIZE);
    plan_action(plan + 44U, AMD64_WINDOWS_UNWIND_SAVE_NONVOL, rbx->index,
                RBX_SAVE_OFFSET);

    asm_global_symbol_t unwind_plan = {
            .name = "__nature_unwind_plan$tls_load",
            .size = sizeof(plan),
            .value = plan,
    };
    void *global_items[] = {&unwind_plan};
    slice_t globals = {.count = 1, .capacity = 1, .take = global_items};

    asm_symbol_t load_symbol = {.name = "tls_load", .is_local = false};
    asm_symbol_t tls_symbol = {
            .name = "tls_value",
            .is_local = false,
            .is_tls = true,
    };
    asm_uint32_t frame_immediate = {.value = FRAME_SIZE};
    asm_sib_reg_t rbx_slot = {
            .base = rsp,
            .index = NULL,
            .scale = 0U,
            .disp = RBX_SAVE_OFFSET,
    };
    asm_sib_reg_t frame_pointer = {
            .base = rsp,
            .index = NULL,
            .scale = 0U,
            .disp = FRAME_SIZE,
    };

    amd64_asm_operand_t label_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .value = &load_symbol,
    };
    amd64_asm_operand_t rbp_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = QWORD,
            .value = rbp,
    };
    amd64_asm_operand_t rsp_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = QWORD,
            .value = rsp,
    };
    amd64_asm_operand_t frame_pointer_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SIB_REG,
            .size = QWORD,
            .value = &frame_pointer,
    };
    amd64_asm_operand_t rbx_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = QWORD,
            .value = rbx,
    };
    amd64_asm_operand_t eax_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_REG,
            .size = DWORD,
            .value = eax,
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
    amd64_asm_operand_t tls_operand = {
            .type = AMD64_ASM_OPERAND_TYPE_SYMBOL,
            .size = DWORD,
            .value = &tls_symbol,
    };

    amd64_asm_inst_t label = {
            .name = "label",
            .operands = {&label_operand},
            .count = 1U};
    amd64_asm_inst_t push = {
            .name = "push",
            .operands = {&rbp_operand},
            .count = 1U};
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
    amd64_asm_inst_t load_tls = {
            .name = "mov",
            .operands = {&eax_operand, &tls_operand},
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
            .name = "pop",
            .operands = {&rbp_operand},
            .count = 1U};
    amd64_asm_inst_t ret = {.name = "ret", .count = 0U};
    void *operation_items[] = {
            &label,
            &push,
            &allocate,
            &set_frame,
            &save_rbx,
            &load_tls,
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
    closure_t closure;
    memset(&closure, 0, sizeof(closure));
    closure.linkident = "tls_load";
    closure.asm_operations = &operations;
    void *closure_items[] = {&closure};
    slice_t closures = {.count = 1, .capacity = 1, .take = closure_items};
    module_t module;
    memset(&module, 0, sizeof(module));
    module.source_path = "coff_amd64_tls_integration.n";
    module.asm_global_symbols = &globals;
    module.closures = &closures;
    closure.module = &module;

    coff_object_t *object = coff_object_create_amd64(module.source_path);
    assert(object);
    char error[1024] = {0};
    coff_writer_status_t status = coff_encode_amd64_module_ex(
            object, &module, error, sizeof(error));
    if (status != COFF_WRITER_OK) fprintf(stderr, "%s\n", error);
    assert(status == COFF_WRITER_OK);
    assert(tls_operand.type == AMD64_ASM_OPERAND_TYPE_SYMBOL);
    assert(tls_operand.value == &tls_symbol);

    coff_section_t *text = coff_object_text(object);
    assert(text);
    append_callback_data(object);
    append_tls_store(object, text);
    append_tls_callback(object, text);
    append_worker(object, text);
    append_thread_main(object, text);
    append_callback_pointer(object);

    coff_section_t *tls = NULL;
    assert(coff_object_add_section(
                   object, ".tls$NAT",
                   LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
                           LD_COFF_SCN_MEM_WRITE,
                   4U, &tls) == COFF_WRITER_OK);
    static const uint8_t initial_value[] = {42U, 0U, 0U, 0U};
    uint32_t tls_offset = UINT32_MAX;
    assert(coff_section_append(tls, initial_value, sizeof(initial_value), 4U,
                               &tls_offset) == COFF_WRITER_OK);
    assert(tls_offset == 0U);
    assert(coff_object_define_symbol(
                   object, "tls_value", tls, tls_offset, 0U,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) == COFF_WRITER_OK);
    assert(coff_object_write_file(object, path) == COFF_WRITER_OK);
    coff_object_destroy(object);
}

static const pe_section_t *find_section(const pe_section_t *sections,
                                        uint16_t count, const char *name) {
    for (uint16_t i = 0U; i < count; i++)
        if (strcmp(sections[i].name, name) == 0) return &sections[i];
    return NULL;
}

static uint64_t rva_to_file_offset(const pe_section_t *sections,
                                   uint16_t count, uint32_t rva,
                                   uint32_t size) {
    for (uint16_t i = 0U; i < count; i++) {
        const pe_section_t *section = &sections[i];
        if (rva < section->rva) continue;
        uint64_t offset = (uint64_t) rva - section->rva;
        if (offset <= section->raw_size &&
            size <= (uint64_t) section->raw_size - offset)
            return section->file_offset + offset;
    }
    return UINT64_MAX;
}

static bool reloc_contains_dir64(const uint8_t *image, size_t image_size,
                                 const pe_section_t *reloc,
                                 uint32_t target_rva) {
    uint64_t cursor = reloc->file_offset;
    uint64_t end = cursor + reloc->virtual_size;
    assert(end <= image_size);
    while (cursor < end) {
        assert(8U <= end - cursor);
        uint32_t page = read_u32(image, image_size, cursor);
        uint32_t block_size = read_u32(image, image_size, cursor + 4U);
        assert(block_size >= 8U && (block_size & 1U) == 0U);
        assert(block_size <= end - cursor);
        for (uint32_t offset = 8U; offset < block_size; offset += 2U) {
            uint16_t entry = read_u16(image, image_size, cursor + offset);
            uint16_t type = entry >> 12U;
            uint32_t entry_rva = page + (entry & 0x0fffU);
            if (type == LD_PE_BASE_RELOC_DIR64 && entry_rva == target_rva)
                return true;
        }
        cursor += block_size;
    }
    return false;
}

static uint32_t find_map_symbol_rva(const char *path, const char *name,
                                    const char *expected_section) {
    FILE *file = fopen(path, "r");
    assert(file);
    char line[1024];
    uint32_t result = UINT32_MAX;
    while (fgets(line, sizeof(line), file)) {
        unsigned long long va = 0U;
        unsigned rva = 0U;
        char section[64] = {0};
        char symbol[256] = {0};
        if (sscanf(line, "0x%llx 0x%x %63s %255s", &va, &rva, section,
                   symbol) == 4 &&
            strcmp(symbol, name) == 0) {
            (void) va;
            if (expected_section)
                assert(strcmp(section, expected_section) == 0);
            result = rva;
            break;
        }
    }
    assert(fclose(file) == 0);
    assert(result != UINT32_MAX);
    return result;
}

static void validate_tls_image(const char *image_path,
                               const char *map_path) {
    size_t image_size = 0U;
    uint8_t *image = read_file(image_path, &image_size);
    assert(image[0] == 'M' && image[1] == 'Z');
    uint32_t pe_offset = read_u32(image, image_size, 0x3cU);
    assert(memcmp(image + pe_offset, "PE\0\0", 4U) == 0);
    uint64_t coff = (uint64_t) pe_offset + 4U;
    uint16_t section_count = read_u16(image, image_size, coff + 2U);
    uint64_t optional = coff + LD_PE_COFF_HEADER_SIZE;
    assert(read_u16(image, image_size, optional) ==
           LD_PE_OPTIONAL_MAGIC_PE32_PLUS);
    uint64_t image_base = read_u64(image, image_size, optional + 24U);
    assert(image_base == LD_PE_IMAGE_BASE64);
    uint64_t directories = optional + 112U;
    uint32_t tls_directory_rva = read_u32(
            image, image_size, directories + LD_PE_DIRECTORY_TLS * 8U);
    uint32_t tls_directory_size = read_u32(
            image, image_size, directories + LD_PE_DIRECTORY_TLS * 8U + 4U);
    assert(tls_directory_rva != 0U && tls_directory_size == 40U);
    assert(read_u32(image, image_size,
                    directories + LD_PE_DIRECTORY_IMPORT * 8U) != 0U);
    assert(read_u32(image, image_size,
                    directories + LD_PE_DIRECTORY_IAT * 8U) != 0U);

    uint64_t section_table = optional + LD_PE_OPTIONAL_HEADER64_SIZE;
    pe_section_t *sections = calloc(section_count, sizeof(*sections));
    assert(sections);
    for (uint16_t i = 0U; i < section_count; i++) {
        uint64_t header =
                section_table + (uint64_t) i * LD_PE_SECTION_HEADER_SIZE;
        assert(header <= image_size &&
               LD_PE_SECTION_HEADER_SIZE <= image_size - header);
        memcpy(sections[i].name, image + header, LD_COFF_NAME_SIZE);
        sections[i].name[LD_COFF_NAME_SIZE] = '\0';
        sections[i].virtual_size = read_u32(image, image_size, header + 8U);
        sections[i].rva = read_u32(image, image_size, header + 12U);
        sections[i].raw_size = read_u32(image, image_size, header + 16U);
        sections[i].file_offset = read_u32(image, image_size, header + 20U);
    }
    const pe_section_t *rdata = find_section(sections, section_count, ".rdata");
    const pe_section_t *crt = find_section(sections, section_count, ".CRT");
    const pe_section_t *tls = find_section(sections, section_count, ".tls");
    const pe_section_t *reloc = find_section(sections, section_count, ".reloc");
    assert(rdata && crt && tls && reloc);
    assert(tls_directory_rva >= rdata->rva &&
           tls_directory_rva + tls_directory_size <=
                   rdata->rva + rdata->virtual_size);

    uint64_t tls_directory_offset = rva_to_file_offset(
            sections, section_count, tls_directory_rva, tls_directory_size);
    assert(tls_directory_offset != UINT64_MAX);
    uint64_t raw_start_va =
            read_u64(image, image_size, tls_directory_offset);
    uint64_t raw_end_va =
            read_u64(image, image_size, tls_directory_offset + 8U);
    uint64_t index_va =
            read_u64(image, image_size, tls_directory_offset + 16U);
    uint64_t callbacks_va =
            read_u64(image, image_size, tls_directory_offset + 24U);
    assert(raw_start_va >= image_base && raw_end_va > raw_start_va);
    assert(index_va >= image_base && callbacks_va >= image_base);
    uint32_t raw_start_rva = (uint32_t) (raw_start_va - image_base);
    uint32_t raw_end_rva = (uint32_t) (raw_end_va - image_base);
    uint32_t callbacks_rva = (uint32_t) (callbacks_va - image_base);
    assert(raw_start_rva >= tls->rva &&
           raw_end_rva <= tls->rva + tls->virtual_size);
    assert(callbacks_rva >= crt->rva &&
           callbacks_rva + sizeof(uint64_t) <=
                   crt->rva + crt->virtual_size);

    uint32_t callback_rva =
            find_map_symbol_rva(map_path, "tls_callback", ".text");
    uint64_t callbacks_offset = rva_to_file_offset(
            sections, section_count, callbacks_rva, sizeof(uint64_t));
    assert(callbacks_offset != UINT64_MAX);
    assert(read_u64(image, image_size, callbacks_offset) ==
           image_base + callback_rva);

    uint32_t tls_value_rva =
            find_map_symbol_rva(map_path, "tls_value", ".tls");
    assert(tls_value_rva >= raw_start_rva &&
           tls_value_rva + 4U <= raw_end_rva);
    uint64_t tls_value_offset = rva_to_file_offset(
            sections, section_count, tls_value_rva, 4U);
    assert(tls_value_offset != UINT64_MAX);
    assert(read_u32(image, image_size, tls_value_offset) == 42U);

    assert(reloc_contains_dir64(image, image_size, reloc,
                                tls_directory_rva));
    assert(reloc_contains_dir64(image, image_size, reloc,
                                tls_directory_rva + 8U));
    assert(reloc_contains_dir64(image, image_size, reloc,
                                tls_directory_rva + 16U));
    assert(reloc_contains_dir64(image, image_size, reloc,
                                tls_directory_rva + 24U));
    assert(reloc_contains_dir64(image, image_size, reloc, callbacks_rva));
    assert(find_map_symbol_rva(map_path, "tls_load", ".text") != UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "tls_store", ".text") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "tls_worker", ".text") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "main", ".text") != UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "callback_process_count", ".data") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "callback_thread_count", ".data") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "callback_sequence", ".data") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "CreateThread", ".text") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "WaitForSingleObject", ".text") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "GetExitCodeThread", ".text") !=
           UINT32_MAX);
    assert(find_map_symbol_rva(map_path, "CloseHandle", ".text") !=
           UINT32_MAX);
    free(sections);
    free(image);
}

int main(void) {
    BUILD_OS = OS_WINDOWS;
    BUILD_ARCH = ARCH_AMD64;
    reg_init();
    amd64_opcode_init();

    static const char *names[] = {
            "crt2.obj",
            NULL,
            "libmingw32.lib",
            "compiler_rt.lib",
            "ucrtbase.lib",
            "api-ms-win-crt-conio-l1-1-0.lib",
            "api-ms-win-crt-convert-l1-1-0.lib",
            "api-ms-win-crt-environment-l1-1-0.lib",
            "api-ms-win-crt-filesystem-l1-1-0.lib",
            "api-ms-win-crt-heap-l1-1-0.lib",
            "api-ms-win-crt-locale-l1-1-0.lib",
            "api-ms-win-crt-math-l1-1-0.lib",
            "api-ms-win-crt-multibyte-l1-1-0.lib",
            "api-ms-win-crt-private-l1-1-0.lib",
            "api-ms-win-crt-process-l1-1-0.lib",
            "api-ms-win-crt-runtime-l1-1-0.lib",
            "api-ms-win-crt-stdio-l1-1-0.lib",
            "api-ms-win-crt-string-l1-1-0.lib",
            "api-ms-win-crt-time-l1-1-0.lib",
            "api-ms-win-crt-utility-l1-1-0.lib",
            "advapi32.lib",
            "bcrypt.lib",
            "crypt32.lib",
            "dbghelp.lib",
            "iphlpapi.lib",
            "kernel32.lib",
            "ntdll.lib",
            "ole32.lib",
            "psapi.lib",
            "secur32.lib",
            "shell32.lib",
            "user32.lib",
            "userenv.lib",
            "version.lib",
            "winmm.lib",
            "ws2_32.lib",
    };
    char *directory = temp_dir();
    assert(directory);
    char *object_path = path_join(directory, "nature-coff-amd64-tls.obj");
    char *image_path = path_join(directory, "nature-coff-amd64-tls.exe");
    char *map_path = path_join(directory, "nature-coff-amd64-tls.map");
    assert(object_path && image_path && map_path);
    make_tls_main_object(object_path);
    names[1] = object_path;

    const size_t count = sizeof(names) / sizeof(*names);
    char **inputs = calloc(count, sizeof(*inputs));
    assert(inputs);
    for (size_t i = 0U; i < count; i++) {
        if (i == 1U) {
            inputs[i] = (char *) object_path;
            continue;
        }
        size_t length = strlen(NATURE_SOURCE_DIR) + strlen(names[i]) + 23U;
        inputs[i] = malloc(length);
        assert(inputs[i]);
        int written = snprintf(inputs[i], length, "%s/lib/windows_amd64/%s",
                               NATURE_SOURCE_DIR, names[i]);
        assert(written > 0 && (size_t) written < length);
    }

    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = image_path;
    options.map_path = map_path;
    options.entry_symbol = "mainCRTStartup";
    options.pie = true;
    options.inputs.items = inputs;
    options.inputs.count = count;
    options.inputs.capacity = count;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    int status = ld_link(&options);
    if (status != LD_OK) fprintf(stderr, "%s\n", capture.message);
    assert(status == LD_OK);
    validate_tls_image(image_path, map_path);
#ifdef __WINDOWS
    /* Layout validation cannot prove that the Windows loader invokes TLS
       callbacks or initializes a new thread's TLS block.  Run the generated
       PE on the native Windows CTest runner; main returns nonzero for callback
       ordering, process/thread attach, or _Thread_local failures. */
    char command[PATH_MAX + 3U];
    int command_length = snprintf(command, sizeof(command), "\"%s\"",
                                  image_path);
    assert(command_length > 0 &&
           (size_t) command_length < sizeof(command));
    assert(system(command) == 0);
#endif

    for (size_t i = 0U; i < count; i++)
        if (i != 1U) free(inputs[i]);
    free(inputs);
    free(map_path);
    free(image_path);
    free(object_path);
    free(directory);
    return 0;
}
