#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_

#include "utils/helper.h"
#include "src/types.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "src/build/config.h"
#include "utils/type.h"

char *reg_table_key(lir_flag_t alloc_type, uint8_t index, uint8_t size);

reg_t *reg_find(lir_flag_t alloc_type, uint8_t index, size_t size);

reg_t *covert_alloc_reg(reg_t *reg);

reg_t *reg_new(char *name, uint8_t index, lir_flag_t alloc_type, uint8_t size, uint8_t reg_id);

lir_flag_t type_kind_trans_alloc(type_kind kind);

#endif //NATURE_SRC_REGISTER_REGISTER_H_
