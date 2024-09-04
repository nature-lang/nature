#ifndef NATURE_ARM64_H
#define NATURE_ARM64_H

#include "elf.h"
#include "src/binary/linker.h"

int arm64_gotplt_entry_type(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_AARCH64_PREL32:
        case R_AARCH64_MOVW_UABS_G0_NC:
        case R_AARCH64_MOVW_UABS_G1_NC:
        case R_AARCH64_MOVW_UABS_G2_NC:
        case R_AARCH64_MOVW_UABS_G3:
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_LDST128_ABS_LO12_NC:
        case R_AARCH64_LDST64_ABS_LO12_NC:
        case R_AARCH64_LDST32_ABS_LO12_NC:
        case R_AARCH64_LDST16_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_COPY:
            return NO_GOTPLT_ENTRY;

        case R_AARCH64_ABS32:
        case R_AARCH64_ABS64:
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
            return AUTO_GOTPLT_ENTRY;

        case R_AARCH64_ADR_GOT_PAGE:
        case R_AARCH64_LD64_GOT_LO12_NC:
            return ALWAYS_GOTPLT_ENTRY;
    }
    return -1;
}

int arm64_is_code_relocate(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_AARCH64_ABS32:
        case R_AARCH64_ABS64:
        case R_AARCH64_PREL32:
        case R_AARCH64_MOVW_UABS_G0_NC:
        case R_AARCH64_MOVW_UABS_G1_NC:
        case R_AARCH64_MOVW_UABS_G2_NC:
        case R_AARCH64_MOVW_UABS_G3:
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_ADR_GOT_PAGE:
        case R_AARCH64_LD64_GOT_LO12_NC:
        case R_AARCH64_LDST128_ABS_LO12_NC:
        case R_AARCH64_LDST64_ABS_LO12_NC:
        case R_AARCH64_LDST32_ABS_LO12_NC:
        case R_AARCH64_LDST16_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_COPY:
            return 0;

        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
        case R_AARCH64_JUMP_SLOT:
            return 1;
    }
    return -1;
}

#endif //NATURE_ARM64_H
