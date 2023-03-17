#include "builtin.h"

/**
 * 注册到符号表中
 * TODO 未实现 closure 中的 stack_size 和 local_var 逻辑，gc 无法处理这样的 fn
 * void print(...[any] args)
 * @return
 */
ast_new_fn *builtin_print() {
    // var_decl
    typedecl_list_t *list_decl = NEW(typedecl_list_t);
    list_decl->element_type = type_base_new(TYPE_ANY);

    ast_var_decl *var_decl = NEW(ast_var_decl);
    var_decl->type = type_base_new(TYPE_LIST);
    var_decl->type.is_origin = true;
    var_decl->type.list_decl = list_decl; // 第一个参数的类型
    var_decl->ident = "args"; // 第一参数的名称

    ast_new_fn *fn = NEW(ast_new_fn);
    fn->name = "print";
    fn->return_type = type_base_new(TYPE_VOID);
    fn->rest_param = true;
    fn->formal_param_count = 1;
    fn->formal_params[0] = var_decl;
    return fn;
}

/**
 * void println(...[any] args)
 * @return
 */
ast_new_fn *builtin_println() {
    ast_new_fn *fn = builtin_print();
    fn->name = "println";
    return fn;
}
