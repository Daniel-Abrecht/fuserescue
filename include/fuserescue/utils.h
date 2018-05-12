/*
fuserescue, an on demand data recovery tool which recovers data on a first access basis.
Copyright (C) 2018 Daniel Abrecht

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef UTILS_H
#define UTILS_H

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct pager {
  int pid, input;
};

bool parseu64(const char** str, uint64_t* ret);
int u64toa(uint64_t x, char r[18]);
void skip_spaces(const char** x);
struct pager pager_create(const char** commands, bool shell);
void pager_close_wait(struct pager* pager);


#endif
