#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool parseu64(const char** str, uint64_t* ret);
int u64toa(uint64_t x, char r[18]);
void skip_spaces(const char** x);

#endif
