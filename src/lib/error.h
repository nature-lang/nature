#ifndef NATURE_SRC_LIB_ERROR_H_
#define NATURE_SRC_LIB_ERROR_H_

#define ERROR_STR_COUNT 1000

void error_exit(int line, char *format, ...);

void error_message(int line, char *message);

void error_ident_not_found(int line, char *ident);
void error_type_not_found(int line, char *ident);
void error_redeclare_ident(int line, char *ident);

// '1.1' (type untyped float) cannot be represented by the type int
void error_type_not_match(int line);

void error_printf(int line, char* format, ...);

#endif //NATURE_SRC_LIB_ERROR_H_
