#include "test_runner.h"

#include "src/build/config.h"
#include "src/symbol/symbol.h"
#include "utils/helper.h"
#include "utils/slice.h"

static bool test_skip_name(char *name) {
    if (!TEST_SKIP_LIST || TEST_SKIP_LIST->count == 0) {
        return false;
    }
    for (int i = 0; i < TEST_SKIP_LIST->count; ++i) {
        char *skip = TEST_SKIP_LIST->take[i];
        if (str_equal(skip, name)) {
            return true;
        }
    }
    return false;
}

static ast_expr_t test_string_literal(char *value) {
    ast_expr_t expr = {0};
    expr.line = 0;
    expr.column = 0;
    expr.assert_type = AST_EXPR_LITERAL;
    ast_literal_t *literal = NEW(ast_literal_t);
    literal->kind = TYPE_STRING;
    literal->value = value;
    literal->len = value ? (int64_t) strlen(value) : 0;
    expr.value = literal;
    return expr;
}

static ast_expr_t test_ident_literal(char *ident) {
    ast_expr_t expr = {0};
    expr.line = 0;
    expr.column = 0;
    expr.assert_type = AST_EXPR_IDENT;
    expr.value = ast_new_ident(ident);
    return expr;
}

static ast_expr_t test_call_expr(ast_expr_t left, list_t *args) {
    ast_call_t *call = NEW(ast_call_t);
    call->left = left;
    call->args = args ? args : ct_list_new(sizeof(ast_expr_t));
    call->generics_args = NULL;
    call->spread = false;

    ast_expr_t call_expr = {0};
    call_expr.assert_type = AST_CALL;
    call_expr.value = call;
    call_expr.line = 0;
    call_expr.column = 0;
    return call_expr;
}

static ast_stmt_t *test_expr_stmt(ast_expr_t expr) {
    ast_stmt_t *stmt = NEW(ast_stmt_t);
    stmt->line = expr.line;
    stmt->column = expr.column;
    stmt->assert_type = AST_STMT_EXPR_FAKE;
    ast_expr_fake_stmt_t *fake = NEW(ast_expr_fake_stmt_t);
    fake->expr = expr;
    stmt->value = fake;
    return stmt;
}

static ast_stmt_t *test_call_stmt(char *fn_ident) {
    ast_expr_t call_expr = test_call_expr(test_ident_literal(fn_ident), NULL);
    return test_expr_stmt(call_expr);
}

static ast_stmt_t *test_print_stmt(char *fn_ident, char *message) {
    list_t *args = ct_list_new(sizeof(ast_expr_t));
    ast_expr_t msg_expr = test_string_literal(message);
    ct_list_push(args, &msg_expr);
    ast_expr_t call_expr = test_call_expr(test_ident_literal(fn_ident), args);
    return test_expr_stmt(call_expr);
}

static ast_stmt_t *test_print_expr_stmt(char *fn_ident, ast_expr_t expr) {
    list_t *args = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(args, &expr);
    ast_expr_t call_expr = test_call_expr(test_ident_literal(fn_ident), args);
    return test_expr_stmt(call_expr);
}

static ast_stmt_t *test_print_args_stmt(char *fn_ident, ast_expr_t first, ast_expr_t second) {
    list_t *args = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(args, &first);
    ct_list_push(args, &second);
    ast_expr_t call_expr = test_call_expr(test_ident_literal(fn_ident), args);
    return test_expr_stmt(call_expr);
}

static ast_stmt_t *test_var_def_stmt(char *ident, ast_expr_t init_expr) {
    ast_stmt_t *stmt = NEW(ast_stmt_t);
    stmt->line = 0;
    stmt->column = 0;

    ast_vardef_stmt_t *vardef = NEW(ast_vardef_stmt_t);
    vardef->var_decl.type = type_kind_new(TYPE_UNKNOWN);
    vardef->var_decl.ident = ident;
    vardef->right = NEW(ast_expr_t);
    *vardef->right = init_expr;

    stmt->assert_type = AST_STMT_VARDEF;
    stmt->value = vardef;
    return stmt;
}

static ast_expr_t test_binary_expr(ast_expr_op_t op, ast_expr_t left, ast_expr_t right) {
    ast_binary_expr_t *binary = NEW(ast_binary_expr_t);
    binary->op = op;
    binary->left = left;
    binary->right = right;

    ast_expr_t expr = {0};
    expr.assert_type = AST_EXPR_BINARY;
    expr.value = binary;
    expr.line = 0;
    expr.column = 0;
    return expr;
}

static ast_expr_t test_eq_expr(ast_expr_t left, ast_expr_t right) {
    return test_binary_expr(AST_OP_EE, left, right);
}

static ast_expr_t test_and_expr(ast_expr_t left, ast_expr_t right) {
    return test_binary_expr(AST_OP_AND_AND, left, right);
}

static ast_stmt_t *test_assign_expr_stmt(char *ident, ast_expr_t right) {
    ast_assign_stmt_t *assign_stmt = NEW(ast_assign_stmt_t);
    assign_stmt->left = test_ident_literal(ident);
    assign_stmt->var_decl = NULL;
    assign_stmt->right = right;

    ast_stmt_t *stmt = NEW(ast_stmt_t);
    stmt->line = 0;
    stmt->column = 0;
    stmt->assert_type = AST_STMT_ASSIGN;
    stmt->value = assign_stmt;
    return stmt;
}

static ast_stmt_t *test_assign_add_one_stmt(char *ident) {
    ast_expr_t right = test_binary_expr(AST_OP_ADD, test_ident_literal(ident), *ast_int_expr(0, 0, 1));
    return test_assign_expr_stmt(ident, right);
}

static ast_stmt_t *test_assign_sub_one_stmt(char *ident) {
    ast_expr_t right = test_binary_expr(AST_OP_SUB, test_ident_literal(ident), *ast_int_expr(0, 0, 1));
    return test_assign_expr_stmt(ident, right);
}

static ast_expr_t test_method_call_expr(char *ident, char *method) {
    ast_expr_select_t *select = NEW(ast_expr_select_t);
    select->left = test_ident_literal(ident);
    select->key = method;

    ast_expr_t select_expr = {0};
    select_expr.assert_type = AST_EXPR_SELECT;
    select_expr.value = select;
    select_expr.line = 0;
    select_expr.column = 0;

    return test_call_expr(select_expr, NULL);
}

static slice_t *test_collect_main_tests(module_t *main_package, int *total, int *skipped) {
    slice_t *tests = slice_new();
    if (total) {
        *total = 0;
    }
    if (skipped) {
        *skipped = 0;
    }
    for (int i = 0; i < main_package->stmt_list->count; ++i) {
        ast_stmt_t *stmt = main_package->stmt_list->take[i];
        if (stmt->assert_type != AST_FNDEF) {
            continue;
        }
        ast_fndef_t *fndef = stmt->value;
        if (!fndef->is_test) {
            continue;
        }
        if (total) {
            (*total)++;
        }
        char *display_name = fndef->test_name ? fndef->test_name : fndef->fn_name;
        if (test_skip_name(display_name)) {
            if (skipped) {
                (*skipped)++;
            }
            continue;
        }
        slice_push(tests, fndef);
    }
    return tests;
}

static ast_stmt_t *test_if_stmt(ast_expr_t condition, slice_t *consequent, slice_t *alternate) {
    ast_if_stmt_t *if_stmt = NEW(ast_if_stmt_t);
    if_stmt->condition = condition;
    if_stmt->consequent = consequent;
    if_stmt->alternate = alternate;

    ast_stmt_t *stmt = NEW(ast_stmt_t);
    stmt->line = 0;
    stmt->column = 0;
    stmt->assert_type = AST_STMT_IF;
    stmt->value = if_stmt;
    return stmt;
}

static ast_stmt_t *test_print_multi_stmt(char *fn_ident, list_t *args) {
    ast_expr_t call_expr = test_call_expr(test_ident_literal(fn_ident), args);
    return test_expr_stmt(call_expr);
}

static slice_t *test_runner_body(slice_t *tests, int total, int skipped) {
    slice_t *body = slice_new();
    int run_total = total - skipped;

    slice_push(body, test_var_def_stmt("failed", *ast_int_expr(0, 0, 0)));
    slice_push(body, test_var_def_stmt("passed", *ast_int_expr(0, 0, (uint64_t) run_total)));
    slice_push(body, test_var_def_stmt("skipped", *ast_int_expr(0, 0, (uint64_t) skipped)));

    for (int i = 0; i < tests->count; ++i) {
        ast_fndef_t *test_fn = tests->take[i];
        char *display_name = test_fn->test_name ? test_fn->test_name : test_fn->fn_name;
        char *prefix = dsprintf("Test [%d/%d] %s ... ", i + 1, run_total, display_name);

        slice_push(body, test_print_stmt("print", prefix));
        slice_t *try_body = slice_new();
        slice_push(try_body, test_call_stmt(test_fn->fn_name));
        slice_push(try_body, test_print_stmt("println", "OK"));

        slice_t *catch_body = slice_new();
        slice_push(catch_body, test_print_stmt("println", "FAILED"));
        slice_push(catch_body, test_print_expr_stmt("println", test_method_call_expr("e", "msg")));
        slice_push(catch_body, test_assign_add_one_stmt("failed"));
        slice_push(catch_body, test_assign_sub_one_stmt("passed"));

        ast_try_catch_stmt_t *try_stmt = NEW(ast_try_catch_stmt_t);
        try_stmt->try_body = try_body;
        try_stmt->catch_err = (ast_var_decl_t) {0};
        try_stmt->catch_err.ident = "e";
        try_stmt->catch_err.type = type_kind_new(TYPE_UNKNOWN);
        try_stmt->catch_body = catch_body;

        ast_stmt_t *stmt = NEW(ast_stmt_t);
        stmt->line = 0;
        stmt->column = 0;
        stmt->assert_type = AST_STMT_TRY_CATCH;
        stmt->value = try_stmt;
        slice_push(body, stmt);
    }

    ast_expr_t failed_zero = test_eq_expr(test_ident_literal("failed"), *ast_int_expr(0, 0, 0));
    ast_expr_t skipped_zero = test_eq_expr(test_ident_literal("skipped"), *ast_int_expr(0, 0, 0));
    ast_expr_t all_ok_cond = test_and_expr(failed_zero, skipped_zero);

    list_t *all_args = ct_list_new(sizeof(ast_expr_t));
    ast_expr_t all_prefix = test_string_literal("All");
    ast_expr_t all_total = *ast_int_expr(0, 0, (uint64_t) total);
    ast_expr_t all_suffix = test_string_literal("tests passed.");
    ct_list_push(all_args, &all_prefix);
    ct_list_push(all_args, &all_total);
    ct_list_push(all_args, &all_suffix);
    slice_t *all_body = slice_new();
    slice_push(all_body, test_print_multi_stmt("println", all_args));

    list_t *count_args = ct_list_new(sizeof(ast_expr_t));
    ast_expr_t passed_ident = test_ident_literal("passed");
    ast_expr_t passed_label = test_string_literal("passed;");
    ast_expr_t skipped_ident = test_ident_literal("skipped");
    ast_expr_t skipped_label = test_string_literal("skipped;");
    ast_expr_t failed_ident = test_ident_literal("failed");
    ast_expr_t failed_label = test_string_literal("failed.");
    ct_list_push(count_args, &passed_ident);
    ct_list_push(count_args, &passed_label);
    ct_list_push(count_args, &skipped_ident);
    ct_list_push(count_args, &skipped_label);
    ct_list_push(count_args, &failed_ident);
    ct_list_push(count_args, &failed_label);
    slice_t *count_body = slice_new();
    slice_push(count_body, test_print_multi_stmt("println", count_args));

    slice_push(body, test_if_stmt(all_ok_cond, all_body, count_body));
    return body;
}

static ast_fndef_t *test_find_main(module_t *main_package) {
    for (int i = 0; i < main_package->stmt_list->count; ++i) {
        ast_stmt_t *stmt = main_package->stmt_list->take[i];
        if (stmt->assert_type != AST_FNDEF) {
            continue;
        }
        ast_fndef_t *fndef = stmt->value;
        if (str_equal(fndef->fn_name, FN_MAIN_NAME)) {
            return fndef;
        }
    }
    return NULL;
}

void test_inject_main(module_t *main_package) {
    int total = 0;
    int skipped = 0;
    slice_t *tests = test_collect_main_tests(main_package, &total, &skipped);
    slice_t *body = test_runner_body(tests, total, skipped);

    ast_fndef_t *main_fndef = test_find_main(main_package);
    if (main_fndef) {
        main_fndef->body = body;
        return;
    }

    ast_fndef_t *fndef = ast_fndef_new(main_package, 0, 0);
    fndef->symbol_name = ident_with_prefix(main_package->ident, FN_MAIN_NAME);
    fndef->fn_name = FN_MAIN_NAME;
    fndef->fn_name_with_pkg = fndef->symbol_name;
    fndef->return_type = type_kind_new(TYPE_VOID);
    fndef->return_type.line = 0;
    fndef->return_type.column = 0;
    fndef->params = ct_list_new(sizeof(ast_var_decl_t));
    fndef->body = body;
    fndef->is_errable = true;

    symbol_t *s = symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, false);
    assertf(s, "fn '%s' redeclared", fndef->symbol_name);

    ast_stmt_t *stmt = NEW(ast_stmt_t);
    stmt->line = 0;
    stmt->column = 0;
    stmt->assert_type = AST_FNDEF;
    stmt->value = fndef;
    slice_push(main_package->stmt_list, stmt);
}
