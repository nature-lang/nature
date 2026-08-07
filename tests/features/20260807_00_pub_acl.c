#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

/**
 * pub 访问控制测试:
 * - pub fn/const/var/type + impl 方法跨 module 访问成功
 * - 私有符号在 module.member / star import / selective import / 类型引用 / 方法调用 各路径报错
 * - 函数体内 pub 报错
 */
static void set_build_output(char *output_name) {
    strcpy(BUILD_OUTPUT_DIR, getenv("BUILD_OUTPUT_DIR"));
#ifdef __WINDOWS
    snprintf(BUILD_OUTPUT_NAME, sizeof(BUILD_OUTPUT_NAME), "%s.exe", output_name);
#else
    strcpy(BUILD_OUTPUT_NAME, output_name);
#endif
}

static void build_entry(char *entry, char *output_name) {
    set_build_output(output_name);

    COMPILER_TRY {
        build(entry, false);
    }
    else {
        assertf(false, "build entry '%s' failed: %s", entry, (char *) test_error_msg);
    }
}

static void assert_entry_output(char *entry, char *output_name, char *expect) {
    build_entry(entry, output_name);

    char *output = exec_output();
    assertf(str_equal(output, expect), "entry '%s' output mismatch\nexpect: %s\nactual: %s", entry, expect,
            output);
}

static void assert_entry_error(char *entry, char *output_name, char *expect) {
    set_build_output(output_name);

    COMPILER_TRY {
        build(entry, false);
        assertf(false, "entry '%s' should not build successfully", entry);
    }
    else {
        assertf(str_equal(test_error_msg, expect), "entry '%s' error mismatch\nexpect: %s\nactual: %s", entry,
                expect, (char *) test_error_msg);
    }
}

int main(void) {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // pub 导出的 fn/const/var/type/impl 方法跨 module 访问成功
    assert_entry_output("main.n", "acl_ok", "3\n100\n7\n2\n");

    // 私有符号跨 module 访问报错(module.member)
    assert_entry_error("err_member.n", "acl_err_member",
                       "20260807_00_pub_acl/err_member.n:4:17: undefined: math.private_fn\n");

    // star import 本身合法, 使用其中的私有符号时报错
    assert_entry_error("err_star.n", "acl_err_star",
                       "20260807_00_pub_acl/err_star.n:4:22: undefined: private_fn\n");

    // selective import 在符号实际解析时检查可见性
    assert_entry_error("err_select.n", "acl_err_select",
                       "20260807_00_pub_acl/err_select.n:4:22: undefined: private_fn\n");

    // 私有类型引用报错
    assert_entry_error("err_type.n", "acl_err_type",
                       "20260807_00_pub_acl/err_type.n:4:16: undefined: math.private_t\n");

    // 私有方法调用报错
    assert_entry_error("err_method.n", "acl_err_method",
                       "20260807_00_pub_acl/err_method.n:5:13: p.priv_mul undefined (cannot refer to unexported "
                       "method priv_mul)\n");

    // 函数体内 pub 报错
    assert_entry_error("err_local_pub.n", "acl_err_local_pub",
                       "20260807_00_pub_acl/err_local_pub.n:2:7: pub can only be applied to module-level "
                       "declarations\n");

    return 0;
}
