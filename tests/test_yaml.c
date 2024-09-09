#include "utils/helper.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


void test_travis_yaml(void) {
    char *yaml_path = getenv("YAML_PATH");
    assert(yaml_path);
    FILE *file = fopen(yaml_path, "r");
    assert(file != NULL);

    yaml_parser_t parser;
    yaml_event_t event;

    assert(yaml_parser_initialize(&parser));
    yaml_parser_set_input_file(&parser, file);

    int language_found = 0;
    int matrix_found = 0;
    int os_count = 0;
    int compiler_count = 0;

    do {
        if (!yaml_parser_parse(&parser, &event)) {
            assertf(false, "parser error: %d", parser.error);
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *value = (char *) event.data.scalar.value;

            if (strcmp(value, "language") == 0) {
                yaml_parser_parse(&parser, &event);
                assert(strcmp((char *) event.data.scalar.value, "c") == 0);
                language_found = 1;
            } else if (strcmp(value, "matrix") == 0) {
                matrix_found = 1;
            } else if (strcmp(value, "os") == 0) {
                yaml_parser_parse(&parser, &event);
                assert(strcmp((char *) event.data.scalar.value, "linux") == 0 ||
                       strcmp((char *) event.data.scalar.value, "osx") == 0);
                os_count++;
            } else if (strcmp(value, "compiler") == 0) {
                yaml_parser_parse(&parser, &event);
                assert(strcmp((char *) event.data.scalar.value, "gcc") == 0 ||
                       strcmp((char *) event.data.scalar.value, "clang") == 0);
                compiler_count++;
            }
        }

        if (event.type != YAML_STREAM_END_EVENT)
            yaml_event_delete(&event);
    } while (event.type != YAML_STREAM_END_EVENT);

    yaml_event_delete(&event);
    yaml_parser_delete(&parser);
    fclose(file);

    assert(language_found);
    assert(matrix_found);
    assert(os_count == 4);
    assert(compiler_count == 4);

    printf("Travis YAML passed！\n");
}

int main(void) {
    test_travis_yaml();
    return 0;
}