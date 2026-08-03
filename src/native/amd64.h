#ifndef NATURE_SRC_LIR_NATIVE_AMD64_H_
#define NATURE_SRC_LIR_NATIVE_AMD64_H_

#include "src/binary/encoding/amd64/asm.h"
#include "src/lir.h"
#include "src/register/register.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/*
 * Compiler-private Windows AMD64 unwind-plan wire contract.
 *
 * Native lowering publishes one symbol named
 *     __nature_unwind_plan$<function-link-ident>
 * for every generated function. The COFF object writer consumes that symbol
 * and translates the abstract actions below into PE .xdata/.pdata records.
 * Multi-byte fields are little-endian byte arrays so the on-wire layout does
 * not depend on host structure padding or host byte order.
 */
#define AMD64_WINDOWS_UNWIND_PLAN_PREFIX "__nature_unwind_plan$"
#define AMD64_WINDOWS_UNWIND_PLAN_MAGIC "NWU1"

enum {
    AMD64_WINDOWS_UNWIND_PLAN_MAGIC_SIZE = 4,
    AMD64_WINDOWS_UNWIND_PLAN_HEADER_SIZE = 20,
    AMD64_WINDOWS_UNWIND_PLAN_ACTION_SIZE = 8,
};

typedef enum amd64_windows_unwind_action_kind {
    AMD64_WINDOWS_UNWIND_PUSH_NONVOL = 1,
    AMD64_WINDOWS_UNWIND_SET_FPREG = 2,
    AMD64_WINDOWS_UNWIND_ALLOC = 3,
    AMD64_WINDOWS_UNWIND_SAVE_NONVOL = 4,
    AMD64_WINDOWS_UNWIND_SAVE_XMM128 = 5,
} amd64_windows_unwind_action_kind_t;

enum {
    /* The frame allocation is preceded by the special __chkstk call. */
    AMD64_WINDOWS_UNWIND_PLAN_FLAG_CHKSTK = 1U << 0,
    AMD64_WINDOWS_UNWIND_ACTION_FLAG_CHKSTK = 1U << 0,
};

typedef struct amd64_windows_unwind_plan_header {
    uint8_t magic[AMD64_WINDOWS_UNWIND_PLAN_MAGIC_SIZE];
    uint8_t header_size_le[2];
    uint8_t action_count_le[2];
    uint8_t frame_size_le[4];
    uint8_t outgoing_call_area_size_le[4];
    uint8_t frame_register;
    uint8_t flags;
    uint8_t reserved_le[2];
} amd64_windows_unwind_plan_header_t;

typedef struct amd64_windows_unwind_plan_action {
    uint8_t kind;
    uint8_t reg;
    uint8_t flags_le[2];
    uint8_t stack_offset_or_allocation_size_le[4];
} amd64_windows_unwind_plan_action_t;

_Static_assert(sizeof(amd64_windows_unwind_plan_header_t) ==
                       AMD64_WINDOWS_UNWIND_PLAN_HEADER_SIZE,
               "Windows AMD64 unwind-plan header layout changed");
_Static_assert(sizeof(amd64_windows_unwind_plan_action_t) ==
                       AMD64_WINDOWS_UNWIND_PLAN_ACTION_SIZE,
               "Windows AMD64 unwind-plan action layout changed");


void amd64_native(closure_t *c);

#endif //NATURE_SRC_LIR_NATIVE_AMD64_H_
