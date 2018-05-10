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

#ifndef FUSERESCUE_H
#define FUSERESCUE_H

#define _GNU_SOURCE
#define FUSE_USE_VERSION 26

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define DIRECTIO_BUFFER_SIZE 1024 * 10

enum loglevel {
  LOGLEVEL_DEFAULT,
  LOGLEVEL_INFO
};

struct fuserescue {
  pthread_mutex_t lock;
  int infile, outfile;
  const char* infile_path;
  bool infile_directio;
  uint64_t size, offset, blocksize;
  struct mapfile* map;
  const char* mapfile;
  long unsigned recover_states;
  pthread_t self;
  bool unsaved;
  bool allowed;
  enum loglevel loglevel;
};

extern const char license[];
extern const size_t license_size;

void fr_save_map(struct fuserescue* fr);

#endif
