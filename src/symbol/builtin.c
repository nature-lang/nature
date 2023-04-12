#include "builtin.h"

/**
 * 注册到符号表中
 * TODO 未实现 closure 中的 stack_size 和 local_var 逻辑，gc 无法处理这样的 fn
 * void print(...[any] args)
 * @return
 */
ast_fndef_t *builtin_print() {
    // var_decl
    type_list_t *list_decl = NEW(type_list_t);
    list_decl->element_type = type_basic_new(TYPE_ANY);

    ast_var_decl *var_decl = NEW(ast_var_decl);
    var_decl->type = type_basic_new(TYPE_LIST);
    var_decl->type.is_origin = true;
    var_decl->type.list = list_decl; // 第一个参数的类型
    var_decl->ident = "args"; // 第一参数的名称

    ast_fndef_t *fn = NEW(ast_fndef_t);
    fn->name = "print";
    fn->return_type = type_basic_new(TYPE_VOID);
    fn->rest_param = true;
    fn->param_count = 1;
    fn->formal_params[0] = var_decl;
    return fn;
}

/**
 * void println(...[any] args)
 * @return
 */
ast_fndef_t *builtin_println() {
    ast_fndef_t *fn = builtin_print();
    fn->name = "println";
    return fn;
}
