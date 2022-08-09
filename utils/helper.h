#ifndef NATURE_SRC_LIB_HELPER_H_
#define NATURE_SRC_LIB_HELPER_H_

#include "src/value.h"
#include <unistd.h>

//void string_to_unique_list(string *list, string value);

char *itoa(int n);

bool str_equal(string a, string b);

char *file_join(char *dst, char *src);

char *str_connect(char *a, char *b);

void str_replace(char *str, char from, char to);

char *file_read(char *path);

uint64_t memory_align(uint64_t n, uint8_t align);

bool file_exists(char *path);

bool ends_with(char* str, char* suffix);

char *get_work_dir();

char *rtrim(char *str, size_t trim_len);

void *copy(char *dst, char *src, uint mode);

#endif //NATURE_SRC_LIB_HELPER_H_
