#include "builtin.h"

/**
 * void print(...[any] args)
 * @return
 */
ast_new_fn *builtin_print() {
    // var_decl
//    ast_var_decl  decl =
    ast_list_decl *array_decl = NEW(ast_list_decl);
    array_decl->type = type_base_new(TYPE_ANY);
    ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
    var_decl->type = type_base_new(TYPE_ARRAY);
    var_decl->type.is_origin = true;
    var_decl->type.value = array_decl;
    var_decl->ident = "args";

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
