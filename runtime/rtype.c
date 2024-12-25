#include "rtype.h"

struct sc_map_64v rt_rtype_map;

// GC_RTYPE(TYPE_STRUCT, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
rtype_t linkco_rtype; // rtype 预生成

// 添加 hash table gc_rtype(TYPE_UINT8, 0);
rtype_t string_element_rtype;

// 添加 hash table  (GC_RTYPE(TYPE_STRING, 5, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);)
rtype_t string_rtype;

// GC_RTYPE(TYPE_STRUCT, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
rtype_t errort_trace_rtype;

// GC_RTYPE(TYPE_STRUCT, 3, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
rtype_t errort_rtype;

// GC_RTYPE(TYPE_GC_ENV, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
rtype_t envs_rtype;

// GC_RTYPE(TYPE_GC_ENV_VALUE, 1, TYPE_GC_SCAN)
rtype_t env_value_rtype;

//  GC_RTYPE(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN)
rtype_t std_arg_rtype;

// GC_RTYPE(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
rtype_t os_env_rtype;

// GC_RTYPE(TYPE_VEC, 5, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN)
rtype_t vec_rtype;

//   GC_RTYPE(TYPE_GC_FN, 12, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
// TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
// TYPE_GC_SCAN);
rtype_t fn_rtype;