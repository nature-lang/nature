#include "custom_links.h"

symdef_t *rt_symdef_ptr = NULL;

fndef_t *rt_fndef_ptr = NULL;

table_t *rt_rtype_table = NULL;

// - symdef
uint64_t ct_symdef_size = 0; // 数量
uint8_t *ct_symdef_data = 0; // 序列化后的 data 大小
uint64_t ct_symdef_count = 0;
symdef_t *ct_symdef_list = 0;

// - fndef
uint64_t ct_fndef_size = 0;
uint8_t *ct_fndef_data = NULL;
uint64_t ct_fndef_count = 0;
fndef_t *ct_fndef_list = NULL;


// - rtype
uint64_t ct_rtype_count = 0; // 从 list 中提取而来
uint8_t *ct_rtype_data = NULL;
uint64_t ct_rtype_size = 0; // rtype + gc_bits + element_kinds 的总数据量大小, sh_size 预申请需要该值，已经在 reflect_type 时计算完毕
list_t *ct_rtype_vec = NULL;
table_t *ct_rtype_table = NULL; // 避免 rtype_vec 重复写入
