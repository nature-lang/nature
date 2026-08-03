/*
 * Portable AMD64 COFF relocatable-object builder.
 *
 * The layout, long-name table, relocation-overflow record, section-definition
 * aux record, and weak-external aux record follow LLVM's COFF object writer.
 *
 * Sources:
 *   llvm/lib/MC/WinCOFFObjectWriter.cpp
 *   llvm/include/llvm/Object/COFF.h
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "coff_writer.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COFF_MAX_STANDARD_SECTIONS UINT32_C(65279)
#define COFF_ERROR_CAPACITY 512U

typedef struct {
    uint32_t offset;
    uint32_t symbol_index;
    uint16_t type;
} coff_relocation_t;

typedef enum {
    COFF_AUX_RAW = 0,
    COFF_AUX_SECTION_DEFINITION,
    COFF_AUX_WEAK_EXTERNAL,
} coff_aux_kind_t;

typedef struct {
    coff_aux_kind_t kind;
    union {
        uint8_t raw[LD_COFF_SYMBOL_SIZE];
        struct {
            struct coff_section *section;
            struct coff_section *associative_parent;
            uint32_t checksum;
            uint8_t selection;
        } section_definition;
        struct {
            uint32_t fallback_symbol_index;
            uint32_t characteristics;
        } weak_external;
    } value;
} coff_aux_record_t;

typedef struct {
    char *name;
    uint32_t value;
    int32_t section_number;
    uint16_t type;
    uint8_t storage_class;
    uint8_t aux_count;
    coff_aux_record_t *aux_records;
    uint32_t raw_index;
} coff_symbol_t;

struct coff_section {
    struct coff_object *object;
    char *name;
    uint32_t index;
    uint32_t characteristics;
    uint32_t alignment;
    uint32_t logical_size;
    uint8_t *data;
    size_t data_capacity;
    coff_relocation_t *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    bool uninitialized;
    bool comdat;
};

struct coff_object {
    char *source_name;
    coff_section_t **sections;
    size_t section_count;
    size_t section_capacity;
    coff_symbol_t *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    uint32_t symbol_record_count;
    coff_section_t *text;
    coff_section_t *rdata;
    coff_section_t *data;
    coff_section_t *bss;
    char error[COFF_ERROR_CAPACITY];
};

typedef struct {
    const char **names;
    uint32_t *offsets;
    size_t count;
    size_t capacity;
    uint8_t *bytes;
    size_t size;
    size_t capacity_bytes;
} coff_string_table_t;

typedef struct {
    uint32_t raw_pointer;
    uint32_t relocation_pointer;
    uint16_t relocation_count;
    uint32_t characteristics;
} coff_section_layout_t;

static void coff_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void coff_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void coff_write_u64(uint8_t *bytes, uint64_t value) {
    coff_write_u32(bytes, (uint32_t) value);
    coff_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

static coff_writer_status_t coff_fail(coff_object_t *object,
                                      coff_writer_status_t status,
                                      const char *format, ...) {
    if (object) {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(object->error, sizeof(object->error), format, arguments);
        va_end(arguments);
    }
    return status;
}

static coff_writer_status_t coff_fail_const(const coff_object_t *object,
                                            coff_writer_status_t status,
                                            const char *format, ...) {
    if (object) {
        coff_object_t *mutable_object = (coff_object_t *) object;
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(mutable_object->error, sizeof(mutable_object->error), format,
                  arguments);
        va_end(arguments);
    }
    return status;
}

static bool coff_add_size(size_t left, size_t right, size_t *result) {
    if (!result || right > SIZE_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool coff_mul_size(size_t left, size_t right, size_t *result) {
    if (!result || (left != 0U && right > SIZE_MAX / left)) return false;
    *result = left * right;
    return true;
}

static bool coff_is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool coff_align_size(size_t value, uint32_t alignment,
                            size_t *result) {
    if (!result || !coff_is_power_of_two(alignment)) return false;
    const size_t mask = (size_t) alignment - 1U;
    if (value > SIZE_MAX - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static char *coff_strdup(const char *value) {
    if (!value) return NULL;
    size_t length = strlen(value);
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length + 1U);
    return copy;
}

static bool coff_grow_array(void **array, size_t *capacity, size_t needed,
                            size_t element_size) {
    if (!array || !capacity || element_size == 0U) return false;
    if (needed <= *capacity) return true;
    size_t next = *capacity ? *capacity : 8U;
    while (next < needed) {
        if (next > SIZE_MAX / 2U) {
            next = needed;
            break;
        }
        next *= 2U;
    }
    if (next > SIZE_MAX / element_size) return false;
    void *resized = realloc(*array, next * element_size);
    if (!resized) return false;
    *array = resized;
    *capacity = next;
    return true;
}

static uint32_t coff_alignment_characteristic(uint32_t alignment) {
    if (!coff_is_power_of_two(alignment) || alignment > 8192U) return 0U;
    uint32_t exponent = 0U;
    for (uint32_t value = alignment; value > 1U; value >>= 1U) exponent++;
    return (exponent + 1U) << 20U;
}

static coff_writer_status_t coff_update_section_alignment(
        coff_section_t *section, uint32_t alignment) {
    if (!section || !coff_is_power_of_two(alignment) || alignment > 8192U) {
        return coff_fail(section ? section->object : NULL,
                         COFF_WRITER_INVALID_ARGUMENT,
                         "COFF section alignment must be a power of two "
                         "between 1 and 8192 bytes");
    }
    if (alignment <= section->alignment) return COFF_WRITER_OK;
    section->alignment = alignment;
    section->characteristics &= ~LD_COFF_SCN_ALIGN_MASK;
    section->characteristics |= coff_alignment_characteristic(alignment);
    return COFF_WRITER_OK;
}

static bool coff_symbol_section_number_valid(int32_t number) {
    return number >= LD_COFF_SYM_DEBUG &&
           number <= (int32_t) COFF_MAX_STANDARD_SECTIONS;
}

static coff_symbol_t *coff_find_symbol_by_raw_index(
        const coff_object_t *object, uint32_t raw_index) {
    if (!object) return NULL;
    for (size_t i = 0U; i < object->symbol_count; i++) {
        if (object->symbols[i].raw_index == raw_index)
            return &object->symbols[i];
    }
    return NULL;
}

static coff_symbol_t *coff_find_symbol_by_name(const coff_object_t *object,
                                               const char *name) {
    if (!object || !name) return NULL;
    for (size_t i = 0U; i < object->symbol_count; i++) {
        if (strcmp(object->symbols[i].name, name) == 0)
            return &object->symbols[i];
    }
    return NULL;
}

static void coff_remove_last_symbol(coff_object_t *object) {
    if (!object || object->symbol_count == 0U) return;
    coff_symbol_t *symbol = &object->symbols[object->symbol_count - 1U];
    object->symbol_record_count = symbol->raw_index;
    free(symbol->name);
    free(symbol->aux_records);
    memset(symbol, 0, sizeof(*symbol));
    object->symbol_count--;
}

static coff_writer_status_t coff_add_symbol_internal(
        coff_object_t *object, const char *name, uint32_t value,
        int32_t section_number, uint16_t type, uint8_t storage_class,
        const coff_aux_record_t *aux_records, uint8_t aux_count,
        uint32_t *symbol_index) {
    if (!object || !name || !coff_symbol_section_number_valid(section_number) ||
        (aux_count != 0U && !aux_records)) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid COFF symbol description");
    }
    const uint64_t new_record_count =
            (uint64_t) object->symbol_record_count + 1U + aux_count;
    if (new_record_count > UINT32_MAX) {
        return coff_fail(object, COFF_WRITER_OVERFLOW,
                         "COFF symbol table exceeds 32-bit record count");
    }
    if (!coff_grow_array((void **) &object->symbols,
                         &object->symbol_capacity, object->symbol_count + 1U,
                         sizeof(*object->symbols))) {
        return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                         "out of memory adding COFF symbol '%s'", name);
    }
    char *name_copy = coff_strdup(name);
    if (!name_copy) {
        return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                         "out of memory copying COFF symbol '%s'", name);
    }
    coff_aux_record_t *aux_copy = NULL;
    if (aux_count != 0U) {
        aux_copy = malloc((size_t) aux_count * sizeof(*aux_copy));
        if (!aux_copy) {
            free(name_copy);
            return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                             "out of memory adding auxiliary records for '%s'",
                             name);
        }
        memcpy(aux_copy, aux_records,
               (size_t) aux_count * sizeof(*aux_copy));
    }
    coff_symbol_t *symbol = &object->symbols[object->symbol_count++];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = name_copy;
    symbol->value = value;
    symbol->section_number = section_number;
    symbol->type = type;
    symbol->storage_class = storage_class;
    symbol->aux_count = aux_count;
    symbol->aux_records = aux_copy;
    symbol->raw_index = object->symbol_record_count;
    object->symbol_record_count = (uint32_t) new_record_count;
    if (symbol_index) *symbol_index = symbol->raw_index;
    return COFF_WRITER_OK;
}

coff_object_t *coff_object_create(const char *source_name) {
    coff_object_t *object = calloc(1U, sizeof(*object));
    if (!object) return NULL;
    if (source_name) {
        object->source_name = coff_strdup(source_name);
        if (!object->source_name) {
            free(object);
            return NULL;
        }
    }
    return object;
}

coff_object_t *coff_object_create_amd64(const char *source_name) {
    coff_object_t *object = coff_object_create(source_name);
    if (!object) return NULL;
    if (coff_object_add_standard_sections(object) != COFF_WRITER_OK ||
        (source_name && *source_name &&
         coff_object_add_file_symbol(object, source_name, NULL) !=
                 COFF_WRITER_OK)) {
        coff_object_destroy(object);
        return NULL;
    }
    return object;
}

void coff_object_destroy(coff_object_t *object) {
    if (!object) return;
    for (size_t i = 0U; i < object->section_count; i++) {
        coff_section_t *section = object->sections[i];
        if (!section) continue;
        free(section->name);
        free(section->data);
        free(section->relocations);
        free(section);
    }
    for (size_t i = 0U; i < object->symbol_count; i++) {
        free(object->symbols[i].name);
        free(object->symbols[i].aux_records);
    }
    free(object->sections);
    free(object->symbols);
    free(object->source_name);
    free(object);
}

const char *coff_object_last_error(const coff_object_t *object) {
    return object && object->error[0] ? object->error : "";
}

const char *coff_writer_status_string(coff_writer_status_t status) {
    switch (status) {
        case COFF_WRITER_OK:
            return "success";
        case COFF_WRITER_INVALID_ARGUMENT:
            return "invalid argument";
        case COFF_WRITER_OUT_OF_MEMORY:
            return "out of memory";
        case COFF_WRITER_OVERFLOW:
            return "integer or file-format overflow";
        case COFF_WRITER_DUPLICATE:
            return "duplicate definition";
        case COFF_WRITER_INVALID_STATE:
            return "invalid builder state";
        case COFF_WRITER_UNSUPPORTED:
            return "unsupported COFF feature";
        case COFF_WRITER_IO_ERROR:
            return "I/O error";
    }
    return "unknown COFF writer error";
}

coff_writer_status_t coff_object_add_section(
        coff_object_t *object, const char *name, uint32_t characteristics,
        uint32_t alignment, coff_section_t **result) {
    if (!object || !name || !*name || !coff_is_power_of_two(alignment) ||
        alignment > 8192U) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid COFF section name or alignment");
    }
    if (object->section_count >= COFF_MAX_STANDARD_SECTIONS) {
        return coff_fail(object, COFF_WRITER_OVERFLOW,
                         "standard COFF object has too many sections");
    }
    if (coff_object_find_section(object, name)) {
        return coff_fail(object, COFF_WRITER_DUPLICATE,
                         "duplicate COFF section '%s'", name);
    }
    const uint32_t content_kinds =
            characteristics & (LD_COFF_SCN_CNT_CODE |
                               LD_COFF_SCN_CNT_INITIALIZED_DATA |
                               LD_COFF_SCN_CNT_UNINITIALIZED_DATA);
    if (content_kinds != 0U && (content_kinds & (content_kinds - 1U)) != 0U) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "COFF section '%s' has conflicting content kinds",
                         name);
    }
    if (!coff_grow_array((void **) &object->sections,
                         &object->section_capacity, object->section_count + 1U,
                         sizeof(*object->sections))) {
        return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                         "out of memory adding COFF section '%s'", name);
    }
    coff_section_t *section = calloc(1U, sizeof(*section));
    if (!section) {
        return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                         "out of memory adding COFF section '%s'", name);
    }
    section->name = coff_strdup(name);
    if (!section->name) {
        free(section);
        return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                         "out of memory copying COFF section '%s'", name);
    }
    section->object = object;
    section->index = (uint32_t) object->section_count + 1U;
    section->alignment = alignment;
    section->characteristics =
            (characteristics & ~LD_COFF_SCN_ALIGN_MASK) |
            coff_alignment_characteristic(alignment);
    section->uninitialized =
            (characteristics & LD_COFF_SCN_CNT_UNINITIALIZED_DATA) != 0U;
    object->sections[object->section_count++] = section;
    if (result) *result = section;
    return COFF_WRITER_OK;
}

coff_writer_status_t coff_object_add_standard_sections(
        coff_object_t *object) {
    if (!object) return COFF_WRITER_INVALID_ARGUMENT;
    if (object->section_count != 0U) {
        return coff_fail(object, COFF_WRITER_INVALID_STATE,
                         "standard COFF sections must be added to an empty "
                         "object");
    }
    coff_writer_status_t status = coff_object_add_section(
            object, ".text",
            LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                    LD_COFF_SCN_MEM_READ,
            16U, &object->text);
    if (status == COFF_WRITER_OK)
        status = coff_object_add_section(
                object, ".rdata",
                LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ, 8U,
                &object->rdata);
    if (status == COFF_WRITER_OK)
        status = coff_object_add_section(
                object, ".data",
                LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
                        LD_COFF_SCN_MEM_WRITE,
                8U, &object->data);
    if (status == COFF_WRITER_OK)
        status = coff_object_add_section(
                object, ".bss",
                LD_COFF_SCN_CNT_UNINITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
                        LD_COFF_SCN_MEM_WRITE,
                8U, &object->bss);
    return status;
}

coff_section_t *coff_object_find_section(const coff_object_t *object,
                                         const char *name) {
    if (!object || !name) return NULL;
    for (size_t i = 0U; i < object->section_count; i++) {
        if (strcmp(object->sections[i]->name, name) == 0)
            return object->sections[i];
    }
    return NULL;
}

coff_section_t *coff_object_text(const coff_object_t *object) {
    return object ? object->text : NULL;
}

coff_section_t *coff_object_rdata(const coff_object_t *object) {
    return object ? object->rdata : NULL;
}

coff_section_t *coff_object_data(const coff_object_t *object) {
    return object ? object->data : NULL;
}

coff_section_t *coff_object_bss(const coff_object_t *object) {
    return object ? object->bss : NULL;
}

uint32_t coff_section_index(const coff_section_t *section) {
    return section ? section->index : 0U;
}

const char *coff_section_name(const coff_section_t *section) {
    return section ? section->name : NULL;
}

uint32_t coff_section_characteristics(const coff_section_t *section) {
    return section ? section->characteristics : 0U;
}

uint32_t coff_section_alignment(const coff_section_t *section) {
    return section ? section->alignment : 0U;
}

uint32_t coff_section_size(const coff_section_t *section) {
    return section ? section->logical_size : 0U;
}

size_t coff_section_relocation_count(const coff_section_t *section) {
    return section ? section->relocation_count : 0U;
}

coff_writer_status_t coff_section_append(
        coff_section_t *section, const void *data, size_t size,
        uint32_t alignment, uint32_t *offset) {
    if (!section || (section->uninitialized && data) ||
        !coff_is_power_of_two(alignment) || alignment > 8192U) {
        return coff_fail(section ? section->object : NULL,
                         COFF_WRITER_INVALID_ARGUMENT,
                         "invalid append to COFF section");
    }
    size_t aligned;
    if (!coff_align_size(section->logical_size, alignment, &aligned) ||
        size > UINT32_MAX || aligned > UINT32_MAX - size) {
        return coff_fail(section->object, COFF_WRITER_OVERFLOW,
                         "COFF section '%s' exceeds 4 GiB", section->name);
    }
    const size_t end = aligned + size;
    coff_writer_status_t status =
            coff_update_section_alignment(section, alignment);
    if (status != COFF_WRITER_OK) return status;
    if (!section->uninitialized && end != 0U) {
        if (!coff_grow_array((void **) &section->data,
                             &section->data_capacity, end, sizeof(uint8_t))) {
            return coff_fail(section->object, COFF_WRITER_OUT_OF_MEMORY,
                             "out of memory growing COFF section '%s'",
                             section->name);
        }
        if (aligned > section->logical_size) {
            memset(section->data + section->logical_size, 0,
                   aligned - section->logical_size);
        }
        if (size != 0U) {
            if (data)
                memcpy(section->data + aligned, data, size);
            else
                memset(section->data + aligned, 0, size);
        }
    }
    section->logical_size = (uint32_t) end;
    if (offset) *offset = (uint32_t) aligned;
    return COFF_WRITER_OK;
}

coff_writer_status_t coff_section_append_zeros(
        coff_section_t *section, size_t size, uint32_t alignment,
        uint32_t *offset) {
    return coff_section_append(section, NULL, size, alignment, offset);
}

coff_writer_status_t coff_section_write(
        coff_section_t *section, uint32_t offset, const void *data,
        size_t size) {
    if (!section || (!data && size != 0U) || section->uninitialized ||
        offset > section->logical_size ||
        size > (size_t) section->logical_size - offset) {
        return coff_fail(section ? section->object : NULL,
                         COFF_WRITER_INVALID_ARGUMENT,
                         "invalid write to COFF section");
    }
    if (size != 0U) memcpy(section->data + offset, data, size);
    return COFF_WRITER_OK;
}

coff_writer_status_t coff_object_add_symbol(
        coff_object_t *object, const coff_symbol_desc_t *symbol,
        uint32_t *symbol_index) {
    if (!object || !symbol || !symbol->name ||
        (symbol->aux_count != 0U && !symbol->aux_records)) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid raw COFF symbol");
    }
    coff_aux_record_t *aux = NULL;
    if (symbol->aux_count != 0U) {
        aux = calloc(symbol->aux_count, sizeof(*aux));
        if (!aux) {
            return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                             "out of memory decoding auxiliary records");
        }
        for (uint8_t i = 0U; i < symbol->aux_count; i++) {
            aux[i].kind = COFF_AUX_RAW;
            memcpy(aux[i].value.raw,
                   symbol->aux_records + (size_t) i * LD_COFF_SYMBOL_SIZE,
                   LD_COFF_SYMBOL_SIZE);
        }
    }
    coff_writer_status_t status = coff_add_symbol_internal(
            object, symbol->name, symbol->value, symbol->section_number,
            symbol->type, symbol->storage_class, aux, symbol->aux_count,
            symbol_index);
    free(aux);
    return status;
}

coff_writer_status_t coff_object_add_defined_symbol(
        coff_object_t *object, const char *name, coff_section_t *section,
        uint32_t value, uint16_t type, uint8_t storage_class,
        uint32_t *symbol_index) {
    if (!object || !name || !section || section->object != object ||
        value > section->logical_size) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid defined COFF symbol '%s'",
                         name ? name : "");
    }
    return coff_add_symbol_internal(object, name, value,
                                    (int32_t) section->index, type,
                                    storage_class, NULL, 0U, symbol_index);
}

coff_writer_status_t coff_object_define_symbol(
        coff_object_t *object, const char *name, coff_section_t *section,
        uint32_t value, uint16_t type, uint8_t storage_class,
        uint32_t *symbol_index) {
    if (!object || !name || !section || section->object != object ||
        value > section->logical_size) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid COFF symbol definition '%s'",
                         name ? name : "");
    }
    coff_symbol_t *symbol = coff_find_symbol_by_name(object, name);
    if (!symbol) {
        return coff_object_add_defined_symbol(object, name, section, value,
                                              type, storage_class,
                                              symbol_index);
    }
    if (symbol->section_number != LD_COFF_SYM_UNDEFINED ||
        symbol->aux_count != 0U) {
        if (symbol->section_number == (int32_t) section->index &&
            symbol->value == value && symbol->type == type &&
            symbol->storage_class == storage_class) {
            if (symbol_index) *symbol_index = symbol->raw_index;
            return COFF_WRITER_OK;
        }
        return coff_fail(object, COFF_WRITER_DUPLICATE,
                         "duplicate COFF symbol definition '%s'", name);
    }
    if (symbol->storage_class != storage_class &&
        symbol->storage_class != LD_COFF_STORAGE_CLASS_EXTERNAL) {
        return coff_fail(object, COFF_WRITER_DUPLICATE,
                         "COFF symbol '%s' changes storage class", name);
    }
    symbol->section_number = (int32_t) section->index;
    symbol->value = value;
    symbol->type = type;
    symbol->storage_class = storage_class;
    if (symbol_index) *symbol_index = symbol->raw_index;
    return COFF_WRITER_OK;
}

coff_writer_status_t coff_object_get_or_add_symbol_reference(
        coff_object_t *object, const char *name, bool external, uint16_t type,
        uint32_t *symbol_index) {
    if (!object || !name || !*name) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid COFF symbol reference");
    }
    coff_symbol_t *symbol = coff_find_symbol_by_name(object, name);
    if (symbol) {
        if (symbol_index) *symbol_index = symbol->raw_index;
        return COFF_WRITER_OK;
    }
    return coff_add_symbol_internal(
            object, name, 0U, LD_COFF_SYM_UNDEFINED, type,
            external ? LD_COFF_STORAGE_CLASS_EXTERNAL
                     : LD_COFF_STORAGE_CLASS_STATIC,
            NULL, 0U, symbol_index);
}

bool coff_object_find_symbol(const coff_object_t *object, const char *name,
                             uint32_t *symbol_index) {
    coff_symbol_t *symbol = coff_find_symbol_by_name(object, name);
    if (!symbol) return false;
    if (symbol_index) *symbol_index = symbol->raw_index;
    return true;
}

size_t coff_object_symbol_record_count(const coff_object_t *object) {
    return object ? object->symbol_record_count : 0U;
}

coff_writer_status_t coff_object_add_file_symbol(coff_object_t *object,
                                                 const char *file_name,
                                                 uint32_t *symbol_index) {
    if (!object || !file_name || !*file_name) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "COFF file symbol has an empty filename");
    }
    const size_t length = strlen(file_name);
    size_t count = (length + LD_COFF_SYMBOL_SIZE - 1U) /
                   LD_COFF_SYMBOL_SIZE;
    if (count == 0U) count = 1U;
    if (count > UINT8_MAX) {
        return coff_fail(object, COFF_WRITER_OVERFLOW,
                         "COFF file symbol filename is too long");
    }
    size_t raw_size;
    if (!coff_mul_size(count, LD_COFF_SYMBOL_SIZE, &raw_size)) {
        return coff_fail(object, COFF_WRITER_OVERFLOW,
                         "COFF file symbol auxiliary size overflows");
    }
    uint8_t *raw = calloc(1U, raw_size);
    coff_aux_record_t *aux = calloc(count, sizeof(*aux));
    if (!raw || !aux) {
        free(raw);
        free(aux);
        return coff_fail(object, COFF_WRITER_OUT_OF_MEMORY,
                         "out of memory creating COFF file symbol");
    }
    memcpy(raw, file_name, length);
    for (size_t i = 0U; i < count; i++) {
        aux[i].kind = COFF_AUX_RAW;
        memcpy(aux[i].value.raw, raw + i * LD_COFF_SYMBOL_SIZE,
               LD_COFF_SYMBOL_SIZE);
    }
    coff_writer_status_t status = coff_add_symbol_internal(
            object, ".file", 0U, LD_COFF_SYM_DEBUG, 0U,
            LD_COFF_STORAGE_CLASS_FILE, aux, (uint8_t) count, symbol_index);
    free(raw);
    free(aux);
    return status;
}

coff_writer_status_t coff_object_add_weak_external(
        coff_object_t *object, const char *name,
        uint32_t fallback_symbol_index, uint32_t search_characteristics,
        uint32_t *symbol_index) {
    if (!object || !name || !*name ||
        !coff_find_symbol_by_raw_index(object, fallback_symbol_index) ||
        search_characteristics < LD_COFF_WEAK_SEARCH_NOLIBRARY ||
        search_characteristics > LD_COFF_WEAK_ANTI_DEPENDENCY) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid weak external COFF symbol");
    }
    if (coff_find_symbol_by_name(object, name)) {
        return coff_fail(object, COFF_WRITER_DUPLICATE,
                         "duplicate weak external COFF symbol '%s'", name);
    }
    coff_aux_record_t aux;
    memset(&aux, 0, sizeof(aux));
    aux.kind = COFF_AUX_WEAK_EXTERNAL;
    aux.value.weak_external.fallback_symbol_index = fallback_symbol_index;
    aux.value.weak_external.characteristics = search_characteristics;
    return coff_add_symbol_internal(
            object, name, 0U, LD_COFF_SYM_UNDEFINED, 0U,
            LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL, &aux, 1U, symbol_index);
}

coff_writer_status_t coff_object_mark_comdat(
        coff_object_t *object, coff_section_t *section, uint8_t selection,
        coff_section_t *associative_parent, const char *leader_name,
        uint32_t checksum, uint32_t *section_symbol_index,
        uint32_t *leader_symbol_index) {
    if (section_symbol_index)
        *section_symbol_index = COFF_SYMBOL_INDEX_NONE;
    if (leader_symbol_index) *leader_symbol_index = COFF_SYMBOL_INDEX_NONE;
    if (!object || !section || section->object != object || section->comdat ||
        selection < LD_COFF_COMDAT_NODUPLICATES ||
        selection > LD_COFF_COMDAT_LARGEST) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "invalid COFF COMDAT description");
    }
    if (selection == LD_COFF_COMDAT_ASSOCIATIVE) {
        if (!associative_parent || associative_parent->object != object ||
            associative_parent == section) {
            return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                             "associative COFF COMDAT has invalid parent");
        }
    } else if (associative_parent || !leader_name || !*leader_name) {
        return coff_fail(object, COFF_WRITER_INVALID_ARGUMENT,
                         "non-associative COFF COMDAT requires a leader");
    }
    if (selection != LD_COFF_COMDAT_ASSOCIATIVE &&
        coff_find_symbol_by_name(object, leader_name)) {
        return coff_fail(object, COFF_WRITER_DUPLICATE,
                         "duplicate COFF COMDAT leader '%s'", leader_name);
    }

    coff_aux_record_t aux;
    memset(&aux, 0, sizeof(aux));
    aux.kind = COFF_AUX_SECTION_DEFINITION;
    aux.value.section_definition.section = section;
    aux.value.section_definition.associative_parent = associative_parent;
    aux.value.section_definition.checksum = checksum;
    aux.value.section_definition.selection = selection;

    coff_writer_status_t status = coff_add_symbol_internal(
            object, section->name, 0U, (int32_t) section->index, 0U,
            LD_COFF_STORAGE_CLASS_STATIC, &aux, 1U,
            section_symbol_index);
    if (status != COFF_WRITER_OK) return status;

    section->comdat = true;
    section->characteristics |= LD_COFF_SCN_LNK_COMDAT;
    if (selection != LD_COFF_COMDAT_ASSOCIATIVE) {
        status = coff_object_add_defined_symbol(
                object, leader_name, section, 0U, 0U,
                LD_COFF_STORAGE_CLASS_EXTERNAL, leader_symbol_index);
        if (status != COFF_WRITER_OK) {
            coff_remove_last_symbol(object);
            section->comdat = false;
            section->characteristics &= ~LD_COFF_SCN_LNK_COMDAT;
            if (section_symbol_index)
                *section_symbol_index = COFF_SYMBOL_INDEX_NONE;
        }
    }
    return status;
}

static size_t coff_relocation_width(uint16_t type) {
    switch (type) {
        case LD_COFF_REL_AMD64_ABSOLUTE:
            return 0U;
        case LD_COFF_REL_AMD64_ADDR64:
            return 8U;
        case LD_COFF_REL_AMD64_SECTION:
            return 2U;
        case LD_COFF_REL_AMD64_ADDR32:
        case LD_COFF_REL_AMD64_ADDR32NB:
        case LD_COFF_REL_AMD64_REL32:
        case LD_COFF_REL_AMD64_REL32_1:
        case LD_COFF_REL_AMD64_REL32_2:
        case LD_COFF_REL_AMD64_REL32_3:
        case LD_COFF_REL_AMD64_REL32_4:
        case LD_COFF_REL_AMD64_REL32_5:
        case LD_COFF_REL_AMD64_SECREL:
            return 4U;
        default:
            return SIZE_MAX;
    }
}

static coff_writer_status_t coff_validate_relocation(
        coff_section_t *section, uint32_t offset, uint32_t symbol_index,
        uint16_t type) {
    if (!section || section->uninitialized) {
        return coff_fail(section ? section->object : NULL,
                         COFF_WRITER_INVALID_ARGUMENT,
                         "COFF relocation cannot target uninitialized data");
    }
    const size_t width = coff_relocation_width(type);
    if (width == SIZE_MAX) {
        return coff_fail(section->object, COFF_WRITER_UNSUPPORTED,
                         "unsupported AMD64 COFF relocation 0x%x", type);
    }
    if (!coff_find_symbol_by_raw_index(section->object, symbol_index)) {
        return coff_fail(section->object, COFF_WRITER_INVALID_ARGUMENT,
                         "COFF relocation references auxiliary or missing "
                         "symbol index %u",
                         symbol_index);
    }
    if (offset > section->logical_size ||
        width > (size_t) section->logical_size - offset) {
        return coff_fail(section->object, COFF_WRITER_INVALID_ARGUMENT,
                         "COFF relocation field extends past section '%s'",
                         section->name);
    }
    if (section->relocation_count >= UINT32_MAX - 1U) {
        return coff_fail(section->object, COFF_WRITER_OVERFLOW,
                         "COFF relocation count exceeds overflow format");
    }
    return COFF_WRITER_OK;
}

coff_writer_status_t coff_section_add_relocation(
        coff_section_t *section, uint32_t offset, uint32_t symbol_index,
        uint16_t type) {
    coff_writer_status_t status =
            coff_validate_relocation(section, offset, symbol_index, type);
    if (status != COFF_WRITER_OK) return status;
    if (!coff_grow_array((void **) &section->relocations,
                         &section->relocation_capacity,
                         section->relocation_count + 1U,
                         sizeof(*section->relocations))) {
        return coff_fail(section->object, COFF_WRITER_OUT_OF_MEMORY,
                         "out of memory adding relocation to '%s'",
                         section->name);
    }
    coff_relocation_t *relocation =
            &section->relocations[section->relocation_count++];
    relocation->offset = offset;
    relocation->symbol_index = symbol_index;
    relocation->type = type;
    return COFF_WRITER_OK;
}

coff_writer_status_t coff_section_add_relocation_with_addend(
        coff_section_t *section, uint32_t offset, uint32_t symbol_index,
        uint16_t type, int64_t implicit_addend) {
    coff_writer_status_t status =
            coff_validate_relocation(section, offset, symbol_index, type);
    if (status != COFF_WRITER_OK) return status;
    const size_t width = coff_relocation_width(type);
    uint8_t encoded[8] = {0};
    if (width == 0U) {
        if (implicit_addend != 0) {
            return coff_fail(section->object, COFF_WRITER_INVALID_ARGUMENT,
                             "ABSOLUTE relocation cannot carry an addend");
        }
    } else if (width == 2U) {
        if (implicit_addend < INT16_MIN || implicit_addend > UINT16_MAX) {
            return coff_fail(section->object, COFF_WRITER_OVERFLOW,
                             "16-bit COFF relocation addend overflows");
        }
        coff_write_u16(encoded, (uint16_t) implicit_addend);
    } else if (width == 4U) {
        if (implicit_addend < INT32_MIN || implicit_addend > UINT32_MAX) {
            return coff_fail(section->object, COFF_WRITER_OVERFLOW,
                             "32-bit COFF relocation addend overflows");
        }
        coff_write_u32(encoded, (uint32_t) implicit_addend);
    } else {
        coff_write_u64(encoded, (uint64_t) implicit_addend);
    }
    if (width != 0U) memcpy(section->data + offset, encoded, width);
    return coff_section_add_relocation(section, offset, symbol_index, type);
}

static void coff_string_table_deinit(coff_string_table_t *table) {
    if (!table) return;
    free(table->names);
    free(table->offsets);
    free(table->bytes);
    memset(table, 0, sizeof(*table));
}

static coff_writer_status_t coff_string_table_init(
        coff_string_table_t *table, const coff_object_t *object) {
    memset(table, 0, sizeof(*table));
    table->bytes = calloc(1U, 4U);
    if (!table->bytes) {
        return coff_fail_const(object, COFF_WRITER_OUT_OF_MEMORY,
                               "out of memory creating COFF string table");
    }
    table->size = 4U;
    table->capacity_bytes = 4U;
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_string_table_add(
        coff_string_table_t *table, const coff_object_t *object,
        const char *name, uint32_t *offset) {
    if (!table || !object || !name || !offset) {
        return coff_fail_const(object, COFF_WRITER_INVALID_ARGUMENT,
                               "invalid COFF string-table entry");
    }
    for (size_t i = 0U; i < table->count; i++) {
        if (strcmp(table->names[i], name) == 0) {
            *offset = table->offsets[i];
            return COFF_WRITER_OK;
        }
    }
    const size_t length = strlen(name) + 1U;
    size_t next_size;
    if (!coff_add_size(table->size, length, &next_size) ||
        next_size > UINT32_MAX) {
        return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                               "COFF string table exceeds 4 GiB");
    }
    if (!coff_grow_array((void **) &table->bytes,
                         &table->capacity_bytes, next_size, sizeof(uint8_t))) {
        return coff_fail_const(object, COFF_WRITER_OUT_OF_MEMORY,
                               "out of memory growing COFF string table");
    }
    const size_t needed = table->count + 1U;
    if (needed > table->capacity) {
        size_t next = table->capacity ? table->capacity * 2U : 8U;
        if (next < needed) next = needed;
        if (next > SIZE_MAX / sizeof(*table->names) ||
            next > SIZE_MAX / sizeof(*table->offsets)) {
            return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                                   "COFF string-table index overflows");
        }
        const char **new_names = malloc(next * sizeof(*table->names));
        uint32_t *new_offsets = malloc(next * sizeof(*table->offsets));
        if (!new_names || !new_offsets) {
            free(new_names);
            free(new_offsets);
            return coff_fail_const(object, COFF_WRITER_OUT_OF_MEMORY,
                                   "out of memory indexing COFF strings");
        }
        if (table->count != 0U) {
            memcpy(new_names, table->names,
                   table->count * sizeof(*table->names));
            memcpy(new_offsets, table->offsets,
                   table->count * sizeof(*table->offsets));
        }
        free(table->names);
        free(table->offsets);
        table->names = new_names;
        table->offsets = new_offsets;
        table->capacity = next;
    }
    const uint32_t new_offset = (uint32_t) table->size;
    memcpy(table->bytes + table->size, name, length);
    table->names[table->count] = name;
    table->offsets[table->count] = new_offset;
    table->count++;
    table->size = next_size;
    *offset = new_offset;
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_build_string_table(
        const coff_object_t *object, coff_string_table_t *table) {
    coff_writer_status_t status = coff_string_table_init(table, object);
    if (status != COFF_WRITER_OK) return status;
    uint32_t ignored;
    for (size_t i = 0U; i < object->section_count; i++) {
        if (strlen(object->sections[i]->name) > LD_COFF_NAME_SIZE) {
            status = coff_string_table_add(table, object,
                                           object->sections[i]->name,
                                           &ignored);
            if (status != COFF_WRITER_OK) return status;
        }
    }
    for (size_t i = 0U; i < object->symbol_count; i++) {
        if (strlen(object->symbols[i].name) > LD_COFF_NAME_SIZE) {
            status = coff_string_table_add(table, object,
                                           object->symbols[i].name,
                                           &ignored);
            if (status != COFF_WRITER_OK) return status;
        }
    }
    coff_write_u32(table->bytes, (uint32_t) table->size);
    return COFF_WRITER_OK;
}

static bool coff_string_table_find(const coff_string_table_t *table,
                                   const char *name, uint32_t *offset) {
    for (size_t i = 0U; table && i < table->count; i++) {
        if (strcmp(table->names[i], name) == 0) {
            if (offset) *offset = table->offsets[i];
            return true;
        }
    }
    return false;
}

static coff_writer_status_t coff_encode_section_name(
        const coff_object_t *object, const coff_string_table_t *strings,
        const char *name, uint8_t destination[LD_COFF_NAME_SIZE]) {
    memset(destination, 0, LD_COFF_NAME_SIZE);
    const size_t length = strlen(name);
    if (length <= LD_COFF_NAME_SIZE) {
        memcpy(destination, name, length);
        return COFF_WRITER_OK;
    }
    uint32_t offset;
    if (!coff_string_table_find(strings, name, &offset)) {
        return coff_fail_const(object, COFF_WRITER_INVALID_STATE,
                               "missing long COFF section name '%s'", name);
    }
    char encoded[LD_COFF_NAME_SIZE + 1U];
    const int count = snprintf(encoded, sizeof(encoded), "/%u", offset);
    if (count <= 0 || count > LD_COFF_NAME_SIZE) {
        return coff_fail_const(
                object, COFF_WRITER_OVERFLOW,
                "long COFF section-name offset cannot fit in 8 bytes");
    }
    memcpy(destination, encoded, (size_t) count);
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_encode_symbol_name(
        const coff_object_t *object, const coff_string_table_t *strings,
        const char *name, uint8_t destination[LD_COFF_NAME_SIZE]) {
    memset(destination, 0, LD_COFF_NAME_SIZE);
    const size_t length = strlen(name);
    if (length <= LD_COFF_NAME_SIZE) {
        memcpy(destination, name, length);
        return COFF_WRITER_OK;
    }
    uint32_t offset;
    if (!coff_string_table_find(strings, name, &offset)) {
        return coff_fail_const(object, COFF_WRITER_INVALID_STATE,
                               "missing long COFF symbol name '%s'", name);
    }
    coff_write_u32(destination, 0U);
    coff_write_u32(destination + 4U, offset);
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_validate_for_serialization(
        const coff_object_t *object) {
    if (!object) return COFF_WRITER_INVALID_ARGUMENT;
    if (object->section_count > COFF_MAX_STANDARD_SECTIONS ||
        object->section_count > UINT16_MAX) {
        return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                               "standard COFF section count overflows");
    }
    for (size_t i = 0U; i < object->symbol_count; i++) {
        const coff_symbol_t *symbol = &object->symbols[i];
        if (symbol->section_number > 0 &&
            (uint32_t) symbol->section_number > object->section_count) {
            return coff_fail_const(object, COFF_WRITER_INVALID_STATE,
                                   "COFF symbol '%s' names a missing section",
                                   symbol->name);
        }
        if (symbol->section_number == LD_COFF_SYM_UNDEFINED &&
            symbol->storage_class == LD_COFF_STORAGE_CLASS_STATIC) {
            return coff_fail_const(
                    object, COFF_WRITER_INVALID_STATE,
                    "local COFF symbol '%s' was referenced but never defined",
                    symbol->name);
        }
    }
    return COFF_WRITER_OK;
}

static coff_writer_status_t coff_assign_layout(
        const coff_object_t *object, coff_section_layout_t *layouts,
        size_t *symbol_table_offset, size_t string_table_size,
        size_t *file_size) {
    size_t section_headers_size;
    if (!coff_mul_size(object->section_count, LD_COFF_SECTION_HEADER_SIZE,
                       &section_headers_size)) {
        return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                               "COFF section-header table size overflows");
    }
    size_t cursor;
    if (!coff_add_size(LD_COFF_HEADER_SIZE, section_headers_size, &cursor)) {
        return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                               "COFF object header size overflows");
    }
    for (size_t i = 0U; i < object->section_count; i++) {
        const coff_section_t *section = object->sections[i];
        coff_section_layout_t *layout = &layouts[i];
        memset(layout, 0, sizeof(*layout));
        layout->characteristics = section->characteristics;
        if (!section->uninitialized && section->logical_size != 0U) {
            if (cursor > UINT32_MAX) {
                return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                                       "COFF raw-data pointer overflows");
            }
            layout->raw_pointer = (uint32_t) cursor;
            if (!coff_add_size(cursor, section->logical_size, &cursor)) {
                return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                                       "COFF raw-data size overflows");
            }
        }
        if (section->relocation_count != 0U) {
            if (cursor > UINT32_MAX) {
                return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                                       "COFF relocation pointer overflows");
            }
            layout->relocation_pointer = (uint32_t) cursor;
            const bool overflow = section->relocation_count >= UINT16_MAX;
            layout->relocation_count =
                    overflow ? UINT16_MAX
                             : (uint16_t) section->relocation_count;
            if (overflow)
                layout->characteristics |= LD_COFF_SCN_LNK_NRELOC_OVFL;
            const size_t record_count =
                    section->relocation_count + (overflow ? 1U : 0U);
            size_t relocation_size;
            if (!coff_mul_size(record_count, LD_COFF_RELOCATION_SIZE,
                               &relocation_size) ||
                !coff_add_size(cursor, relocation_size, &cursor)) {
                return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                                       "COFF relocation table size overflows");
            }
        }
    }
    if (cursor > UINT32_MAX) {
        return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                               "COFF symbol-table pointer overflows");
    }
    *symbol_table_offset = cursor;
    size_t symbol_bytes;
    if (!coff_mul_size(object->symbol_record_count, LD_COFF_SYMBOL_SIZE,
                       &symbol_bytes) ||
        !coff_add_size(cursor, symbol_bytes, &cursor) ||
        !coff_add_size(cursor, string_table_size, &cursor)) {
        return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                               "COFF symbol or string table size overflows");
    }
    if (cursor > UINT32_MAX) {
        return coff_fail_const(object, COFF_WRITER_OVERFLOW,
                               "standard COFF object exceeds 4 GiB");
    }
    *file_size = cursor;
    return COFF_WRITER_OK;
}

static void coff_encode_aux_record(const coff_aux_record_t *aux,
                                   uint8_t bytes[LD_COFF_SYMBOL_SIZE]) {
    memset(bytes, 0, LD_COFF_SYMBOL_SIZE);
    switch (aux->kind) {
        case COFF_AUX_RAW:
            memcpy(bytes, aux->value.raw, LD_COFF_SYMBOL_SIZE);
            break;
        case COFF_AUX_SECTION_DEFINITION: {
            const coff_section_t *section =
                    aux->value.section_definition.section;
            coff_write_u32(bytes, section->logical_size);
            const uint16_t relocation_count =
                    section->relocation_count >= UINT16_MAX
                            ? UINT16_MAX
                            : (uint16_t) section->relocation_count;
            coff_write_u16(bytes + 4U, relocation_count);
            coff_write_u16(bytes + 6U, 0U);
            coff_write_u32(bytes + 8U,
                           aux->value.section_definition.checksum);
            uint32_t number = 0U;
            if (aux->value.section_definition.associative_parent)
                number = aux->value.section_definition.associative_parent
                                 ->index;
            coff_write_u16(bytes + 12U, (uint16_t) number);
            bytes[14] = aux->value.section_definition.selection;
            bytes[15] = 0U;
            coff_write_u16(bytes + 16U, 0U);
            break;
        }
        case COFF_AUX_WEAK_EXTERNAL:
            coff_write_u32(
                    bytes,
                    aux->value.weak_external.fallback_symbol_index);
            coff_write_u32(bytes + 4U,
                           aux->value.weak_external.characteristics);
            break;
    }
}

coff_writer_status_t coff_object_serialize(const coff_object_t *object,
                                           uint8_t **image,
                                           size_t *image_size) {
    if (!object || !image || !image_size) {
        return coff_fail_const(object, COFF_WRITER_INVALID_ARGUMENT,
                               "invalid COFF serialization output");
    }
    *image = NULL;
    *image_size = 0U;
    coff_writer_status_t status = coff_validate_for_serialization(object);
    if (status != COFF_WRITER_OK) return status;

    coff_string_table_t strings;
    status = coff_build_string_table(object, &strings);
    if (status != COFF_WRITER_OK) {
        coff_string_table_deinit(&strings);
        return status;
    }
    coff_section_layout_t *layouts = NULL;
    if (object->section_count != 0U) {
        layouts = calloc(object->section_count, sizeof(*layouts));
        if (!layouts) {
            coff_string_table_deinit(&strings);
            return coff_fail_const(object, COFF_WRITER_OUT_OF_MEMORY,
                                   "out of memory laying out COFF sections");
        }
    }
    size_t symbol_table_offset = 0U;
    size_t total_size = 0U;
    status = coff_assign_layout(object, layouts, &symbol_table_offset,
                                strings.size, &total_size);
    if (status != COFF_WRITER_OK) {
        free(layouts);
        coff_string_table_deinit(&strings);
        return status;
    }
    uint8_t *bytes = calloc(1U, total_size ? total_size : 1U);
    if (!bytes) {
        free(layouts);
        coff_string_table_deinit(&strings);
        return coff_fail_const(object, COFF_WRITER_OUT_OF_MEMORY,
                               "out of memory creating COFF image");
    }

    coff_write_u16(bytes, LD_COFF_MACHINE_AMD64);
    coff_write_u16(bytes + 2U, (uint16_t) object->section_count);
    coff_write_u32(bytes + 4U, 0U);
    coff_write_u32(bytes + 8U, (uint32_t) symbol_table_offset);
    coff_write_u32(bytes + 12U, object->symbol_record_count);
    coff_write_u16(bytes + 16U, 0U);
    coff_write_u16(bytes + 18U, 0U);

    for (size_t i = 0U; i < object->section_count; i++) {
        const coff_section_t *section = object->sections[i];
        const coff_section_layout_t *layout = &layouts[i];
        uint8_t *header = bytes + LD_COFF_HEADER_SIZE +
                          i * LD_COFF_SECTION_HEADER_SIZE;
        status = coff_encode_section_name(object, &strings, section->name,
                                          header);
        if (status != COFF_WRITER_OK) goto fail;
        coff_write_u32(header + 8U, 0U);
        coff_write_u32(header + 12U, 0U);
        coff_write_u32(header + 16U, section->logical_size);
        coff_write_u32(header + 20U, layout->raw_pointer);
        coff_write_u32(header + 24U, layout->relocation_pointer);
        coff_write_u32(header + 28U, 0U);
        coff_write_u16(header + 32U, layout->relocation_count);
        coff_write_u16(header + 34U, 0U);
        coff_write_u32(header + 36U, layout->characteristics);

        if (layout->raw_pointer != 0U && section->logical_size != 0U)
            memcpy(bytes + layout->raw_pointer, section->data,
                   section->logical_size);
        if (layout->relocation_pointer != 0U) {
            uint8_t *relocation_bytes = bytes + layout->relocation_pointer;
            if (section->relocation_count >= UINT16_MAX) {
                coff_write_u32(relocation_bytes,
                               (uint32_t) section->relocation_count + 1U);
                coff_write_u32(relocation_bytes + 4U, 0U);
                coff_write_u16(relocation_bytes + 8U, 0U);
                relocation_bytes += LD_COFF_RELOCATION_SIZE;
            }
            for (size_t j = 0U; j < section->relocation_count; j++) {
                const coff_relocation_t *relocation =
                        &section->relocations[j];
                coff_write_u32(relocation_bytes, relocation->offset);
                coff_write_u32(relocation_bytes + 4U,
                               relocation->symbol_index);
                coff_write_u16(relocation_bytes + 8U, relocation->type);
                relocation_bytes += LD_COFF_RELOCATION_SIZE;
            }
        }
    }

    uint8_t *symbol_bytes = bytes + symbol_table_offset;
    for (size_t i = 0U; i < object->symbol_count; i++) {
        const coff_symbol_t *symbol = &object->symbols[i];
        status = coff_encode_symbol_name(object, &strings, symbol->name,
                                         symbol_bytes);
        if (status != COFF_WRITER_OK) goto fail;
        coff_write_u32(symbol_bytes + 8U, symbol->value);
        coff_write_u16(symbol_bytes + 12U,
                       (uint16_t) symbol->section_number);
        coff_write_u16(symbol_bytes + 14U, symbol->type);
        symbol_bytes[16] = symbol->storage_class;
        symbol_bytes[17] = symbol->aux_count;
        symbol_bytes += LD_COFF_SYMBOL_SIZE;
        for (uint8_t j = 0U; j < symbol->aux_count; j++) {
            coff_encode_aux_record(&symbol->aux_records[j], symbol_bytes);
            symbol_bytes += LD_COFF_SYMBOL_SIZE;
        }
    }
    memcpy(symbol_bytes, strings.bytes, strings.size);

    free(layouts);
    coff_string_table_deinit(&strings);
    *image = bytes;
    *image_size = total_size;
    return COFF_WRITER_OK;

fail:
    free(bytes);
    free(layouts);
    coff_string_table_deinit(&strings);
    return status;
}

coff_writer_status_t coff_object_write_file(const coff_object_t *object,
                                            const char *path) {
    if (!object || !path || !*path) {
        return coff_fail_const(object, COFF_WRITER_INVALID_ARGUMENT,
                               "COFF output path is empty");
    }
    uint8_t *image = NULL;
    size_t image_size = 0U;
    coff_writer_status_t status =
            coff_object_serialize(object, &image, &image_size);
    if (status != COFF_WRITER_OK) return status;
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(image);
        return coff_fail_const(object, COFF_WRITER_IO_ERROR,
                               "cannot open COFF output '%s': %s", path,
                               strerror(errno));
    }
    size_t written = 0U;
    while (written < image_size) {
        size_t count = fwrite(image + written, 1U, image_size - written, file);
        if (count == 0U) {
            status = coff_fail_const(object, COFF_WRITER_IO_ERROR,
                                     "cannot write COFF output '%s'", path);
            break;
        }
        written += count;
    }
    if (fclose(file) != 0 && status == COFF_WRITER_OK) {
        status = coff_fail_const(object, COFF_WRITER_IO_ERROR,
                                 "cannot close COFF output '%s': %s", path,
                                 strerror(errno));
    }
    free(image);
    return status;
}
