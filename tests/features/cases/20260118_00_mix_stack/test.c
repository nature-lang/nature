//
// Created by weiwenhao on 2026/1/18.
//
#include <stdint.h>
#include <stdio.h>

// 定义回调函数类型，和 nature 中的 fn():i64 对应
typedef int64_t (*nature_callback_fn)(void);

void testb(nature_callback_fn callback) {
    volatile int64_t local_var1 = 12345;
    volatile int64_t local_var2 = 67890;
    volatile char buffer[128];

    callback();
}

void testa(nature_callback_fn callback) {
    volatile int64_t local_var1 = 12345;
    volatile int64_t local_var2 = 67890;
    volatile char buffer[128];

    testb(callback);
}

// C 函数，接收 nature 回调函数指针并调用
// 这个函数会在栈上创建一些局部变量（C 栈帧）
// 然后调用 nature 的回调函数
int64_t test_in_c(nature_callback_fn callback) {
    // 在 C 栈上分配一些变量，模拟正常的 C 函数栈帧
    volatile int64_t local_var1 = 12345;
    volatile int64_t local_var2 = 67890;
    volatile char buffer[128];

    // 填充 buffer 产生栈脏数据
    for (int i = 0; i < 128; i++) {
        buffer[i] = (char) (i % 256);
    }

    testa(callback);

    return 12;
}
