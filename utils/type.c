#include "type.h"

uint8_t type_kind_sizeof(type_kind t) {
    switch (t) {
        case TYPE_BOOL:
        case TYPE_INT8:
            return 1;
        case TYPE_INT16:
            return 2;
        case TYPE_INT32:
            return 4;
        case TYPE_INT64:
        case TYPE_FLOAT:
            return 8; // 固定大小
        default:
            return POINTER_SIZE;
    }
    return 0;
}


type_t type_with_point(type_t t, uint8_t point) {
    type_t result;
    result.is_origin = t.is_origin;
    result.point = point;
    return result;
}

type_t type_base_new(type_kind kind) {
    type_t result = {
            .is_origin = true,
            .kind = kind,
            .value_ = 0,
    };

    return result;
}

type_t type_new(type_kind kind, void *value) {
    type_t result = {
            .kind = kind,
            .value_ = value
    };
    return result;
}
