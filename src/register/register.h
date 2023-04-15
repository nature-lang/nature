#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_

#include "utils/helper.h"
#include "src/structs.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "src/build/config.h"
#include "utils/type.h"

string reg_table_key(uint8_t index, uint8_t size);

reg_t *reg_find(uint8_t index, size_t size);

reg_t *covert_alloc_reg(reg_t *reg);

reg_t *reg_new(char *name, uint8_t index, vr_flag_t alloc_type, uint8_t size, uint8_t reg_id);

vr_flag_t type_base_trans_alloc(type_kind t);

#endif //NATURE_SRC_REGISTER_REGISTER_H_
