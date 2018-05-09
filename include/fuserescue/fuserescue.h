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
  uint64_t size, offset, blocksize;
  struct mapfile* map;
  const char* mapfile;
  long unsigned recover_states;
  pthread_t self;
  bool unsaved;
  bool allowed;
  enum loglevel loglevel;
};

void fr_save_map(struct fuserescue* fr);

#endif
