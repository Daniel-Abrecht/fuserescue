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

#ifndef MAP_H
#define MAP_H

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#define ENTRIES_MAX 1024 * 1024 * 10

enum mapentry_state {
  ME_NON_TRIED,
  ME_NON_TRIMMED,
  ME_NON_SCRAPED,
  ME_BAD_SECTOR,
  ME_FINISHED
};

enum mapfile_state {
  MF_NON_TRIED,
  MF_NON_TRIMMED,
  MF_NON_SCRAPED,
  MF_BAD_SECTOR,
  MF_SPECIFIED_BLOCKS,
  MF_APPROXIMATE,
  MF_FINISHED
};

struct mapentry {
  uint64_t offset, size;
  enum mapentry_state state;
};

struct mapfile {
  size_t total;
  enum mapfile_state state;
  size_t count;
  struct mapentry entries[ENTRIES_MAX];
};

bool map_normalize(struct mapfile* map);
struct mapfile* map_read(const char* file);
bool map_move(struct mapfile* map, size_t i, ssize_t n);
bool map_write(struct mapfile* map, int fd);
void map_update(struct mapfile* map, uint64_t start, uint64_t end, enum mapentry_state state);

#endif
