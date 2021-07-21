#ifndef NATURE_SRC_LIB_ERROR_H_
#define NATURE_SRC_LIB_ERROR_H_

void error_exit(int code, char *message);

void error_message(int line, char *message);

void error_ident_not_found(int line, char *ident);
void error_type_not_found(int line, char *ident);
void error_redeclare_ident(int line, char *ident);

#endif //NATURE_SRC_LIB_ERROR_H_
