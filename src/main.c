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

#include <fuserescue/fuserescue.h>
#include <fuserescue/utils.h>
#include <fuserescue/cmd.h>
#include <fuserescue/map.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif


static char readbuffer[DIRECTIO_BUFFER_SIZE] __attribute__ ((__aligned__ (4096)));



void fr_save_map(struct fuserescue* fr){
  pthread_mutex_lock(&fr->lock);
  int mapfd = open(fr->mapfile,O_CREAT|O_WRONLY|O_TRUNC|O_BINARY);
  if(mapfd==-1){
    perror("failed to open mapfile");
    exit(5);
  }
  if(!map_normalize(fr->map)){
    printf("Bug: map became corrupted!!!\n");
    map_write(fr->map,1); // write it to stdout
    close(mapfd);
    exit(5);
  }
  if(!map_write(fr->map,mapfd)){
    perror("failed to write mapfile");
    exit(5);
  }
  close(mapfd);
  pthread_mutex_unlock(&fr->lock);
}


static int fr_getattr(
  const char* path,
  struct stat* stbuf
){
  if(strcmp(path, "/"))
    return -ENOENT;

  struct fuserescue* fr = fuse_get_context()->private_data;

  stbuf->st_mode = S_IFREG | 0440;
  stbuf->st_nlink = 1;
  stbuf->st_size = fr->size;

  return 0;
}


static int fr_open(
  const char* path,
  struct fuse_file_info* fi
){
  (void) fi;

  if(strcmp(path, "/"))
    return -ENOENT;

  return 0;
}

static int fr_read(
  const char* path,
  char* buf,
  size_t size,
  off_t offset,
  struct fuse_file_info* fi
){
  (void) buf;
  (void) fi;

  if(strcmp(path, "/"))
    return -ENOENT;

  struct fuserescue* fr = fuse_get_context()->private_data;

  if ((uint64_t)offset >= fr->size)
    return 0;

  if(fr->size-offset < size)
    size = fr->size-offset;

  bool error = false;
  memset(buf,0,size);
  
  struct ranges {
    uint64_t start, end;
  };

  enum { max_slices=1024*1024 };
  size_t to_recover_count = 1;
  static struct ranges to_recover[max_slices];
  to_recover[0].start = offset;
  to_recover[0].end = offset+size;

  pthread_mutex_lock(&fr->lock);
  size_t to_recover_index = 0;
  struct mapentry* entries = fr->map->entries;
  for(size_t i=0,n=fr->map->count; i<n; i++){
    if(entries[i].state != ME_FINISHED){
      if( (1lu<<entries[i].state) & fr->recover_states )
        continue;
    }
    uint64_t overlap_start = (uint64_t)offset > entries[i].offset ? (uint64_t)offset : entries[i].offset;
    uint64_t overlap_end = (uint64_t)offset + size > entries[i].offset+entries[i].size ? entries[i].offset+entries[i].size : (uint64_t)offset + size;
    if(overlap_start >= overlap_end)
      continue;
    if(entries[i].state == ME_FINISHED){
      if((uint64_t)lseek(fr->outfile,overlap_start,SEEK_SET) != overlap_start){
        perror("failed to lseek outfile");
        exit(2);
      }
      size_t rcount = 0;
      ssize_t ret;
      do {
        ret = read(fr->outfile, buf+(overlap_start-offset), overlap_end - overlap_start);
        if(ret<0){
          if(ret == EINTR)
            continue;
          perror("failed to read from outfile");
          exit(2);
        }
        rcount += ret;
      } while(!ret || rcount < overlap_end - overlap_start);
      if(fr->loglevel >= LOGLEVEL_INFO)
        printf("read %"PRIx64" - %"PRIx64"\n", overlap_start,overlap_end);
    }else{
      error = true;
    }
    if( overlap_start == to_recover[to_recover_index].start && overlap_end == to_recover[to_recover_index].end ){
      to_recover_count--;
      break;
    }
    if(overlap_start == to_recover[to_recover_index].start){
      to_recover[to_recover_index].start = overlap_end;
      continue;
    }
    if(overlap_end == to_recover[to_recover_index].end){
      to_recover[to_recover_index].start = overlap_start;
      continue;
    }
    uint64_t sl_end = to_recover[to_recover_index].end;
    to_recover[to_recover_index].end = overlap_start;
    if(to_recover_count>=max_slices){
      fprintf(stderr,"Error: too many fragmants to recover for this read. Trying to recover as many as possible. Try again later for the remaining ones.");
      error = true;
      break;
    }
    to_recover_count++;
    to_recover_index++;
    to_recover[to_recover_index].start = overlap_end;
    to_recover[to_recover_index].end = sl_end;
  }
  bool allowed = fr->allowed;
  uint64_t blocksize = fr->blocksize;
  pthread_mutex_unlock(&fr->lock);

  if(to_recover_count && !allowed){
    error = true;
  }

  if(to_recover_count && allowed){
    enum { FORWARD, BACKWARD } direction = FORWARD;
    for(ssize_t i=0,j=to_recover_count-1; i<=j;){
      if(direction == FORWARD){
        uint64_t s = to_recover[i].start;
        uint64_t e = to_recover[i].end;
        if(fr->loglevel >= LOGLEVEL_INFO)
          printf("trying to recover %"PRIx64"+%"PRIx64" - %"PRIx64"\n", fr->offset,s,e);
        if((uint64_t)lseek(fr->outfile,s,SEEK_SET) != s){
          perror("failed to lseek outfile");
          exit(2);
        }
        if((uint64_t)lseek(fr->infile,fr->offset+s,SEEK_SET) != fr->offset + s){
          perror("failed to lseek infile");
          exit(3);
        }
        do {
          size_t m = e-s;
          if(!m) break;
          if(m > blocksize)
            m = blocksize;
          ssize_t ret;
          do {
            ret = read(fr->infile,readbuffer,m);
          } while(ret < 0 && errno == EINTR);
          if(ret<0){
            error = true;
            if(errno != EIO){
              perror("read failed in an unexpected way");
              goto end;
            }
            perror("forward read from infile failed");
            pthread_mutex_lock(&fr->lock);
            map_update(fr->map,s,s+m,ME_NON_SCRAPED);
            map_update(fr->map,s+m,e,ME_NON_TRIED);
            fr->unsaved = true;
            pthread_mutex_unlock(&fr->lock);
            to_recover[i].start = s + m;
            direction = BACKWARD;
            goto next;
          }else{
            memcpy(buf+(s-offset),readbuffer,ret);
            if(write(fr->outfile,buf+(s-offset),m)<0){
              perror("writing to outfile failed");
              exit(2);
            }
            pthread_mutex_lock(&fr->lock);
            map_update(fr->map,s,s+ret,ME_FINISHED);
            fr->unsaved = true;
            pthread_mutex_unlock(&fr->lock);
            s += ret;
          }
        } while(s<e);
        i++;
      }else{
        uint64_t s = to_recover[j].start;
        uint64_t e = to_recover[j].end;
        printf("trying to recover %"PRIx64"+%"PRIx64" - %"PRIx64"\n", fr->offset,s,e);
        do {
          size_t m = e-s;
          if(!m) break;
          if(m > blocksize)
            m = blocksize;
          if((uint64_t)lseek(fr->infile,fr->offset+(e-m),SEEK_SET) != fr->offset+(e-m)){
            perror("failed to lseek infile");
            exit(3);
          }
          ssize_t ret;
          do {
            ret = read(fr->infile,readbuffer,m);
          } while(ret < 0 && errno == EINTR);
          if(ret<0){
            error = true;
            if(errno != EIO){
              perror("read failed in an unexpected way");
              goto end;
            }
            perror("backward read from infile failed");
            pthread_mutex_lock(&fr->lock);
            map_update(fr->map,e-m,e,ME_NON_SCRAPED);
            map_update(fr->map,s,e-m,ME_NON_TRIED);
            fr->unsaved = true;
            pthread_mutex_unlock(&fr->lock);
            to_recover[i].end = e-m;
            direction = FORWARD;
            goto next;
          }else{
            memcpy(buf+(e-m-offset),readbuffer,ret);
            if((uint64_t)lseek(fr->outfile,e-m,SEEK_SET) != e-m){
              perror("failed to lseek outfile");
              exit(2);
            }
            if(write(fr->outfile,buf+(e-m-offset),m)<0){
              perror("writing to outfile failed");
              exit(2);
            }
            pthread_mutex_lock(&fr->lock);
            map_update(fr->map,s,s+ret,ME_FINISHED);
            fr->unsaved = true;
            pthread_mutex_unlock(&fr->lock);
            e -= ret;
          }
        } while(s<e);
        j--;
      }
      next:;
    }
  }

end:
  if(fr->unsaved)
    fr_save_map(fr);
  fr->unsaved = false;

  return error ? -EIO : (int)size;
}


static struct fuse_operations fr_oper = {
  .getattr  = fr_getattr,
  .open     = fr_open,
  .read     = fr_read
};


int main(int argc, char* argv[]){
  if(argc<5||argc>7){
    fprintf(stderr,"Usage: %s infile outfile mapfile mountpoint [offset] [size]\n",argv[0]);
    return 1;
  }
  int infile = open( argv[1], O_RDONLY | O_DIRECT | O_BINARY );
  if(infile == -1){
    perror("Failed to open input file");
    return 1;
  }
  uint64_t offset = 0;
  long long insize = lseek( infile, 0, SEEK_END );
  if(insize < 0){
    perror("Input file is not seekable");
    return 1;
  }
  if(argc >= 6){
    const char* s = argv[5];
    if(!parseu64(&s,&offset) || strlen(s) || offset >= (uint64_t)insize){
      perror("invalid offset");
      return 1;
    }
    insize -= offset;
  }
  if(argc >= 7){
    const char* s = argv[6];
    uint64_t size;
    if(!parseu64(&s,&size) || strlen(s) || size > (uint64_t)insize){
      perror("invalid size");
      return 1;
    }
    insize = size;
  }
  int outfile = open( argv[2], O_RDWR | O_SYNC | O_BINARY | O_CREAT, 0660 );
  if(outfile == -1){
    perror("Failed to open output file");
    return 1;
  }
  long long outsize = lseek(outfile, 0, SEEK_END);
  if(outsize<0){
    perror("Output file is not seekable");
    return 1;
  }
  if(outsize < insize)
    ftruncate(outfile,insize);
  struct mapfile* map = map_read(argv[3]);
  if(!map){
    fprintf(stderr,"Failed to read map file\n");
    return 1;
  }
  struct stat stbuf;
  if( stat(argv[4], &stbuf) == -1 ){
    perror("failed to stat mountpoint");
    return 1;
  }
  if( !S_ISREG(stbuf.st_mode) ){
    fprintf(stderr, "mountpoint is not a regular file\n");
    return 1;
  }
  int sector_size = 0;
  if(ioctl(outfile, BLKSSZGET, &sector_size)<0 || !sector_size)
    sector_size = 512;
  if((unsigned)sector_size > sizeof(readbuffer))
    sector_size = sizeof(readbuffer);
  struct fuserescue params = {
    .infile = infile,
    .outfile = outfile,
    .offset = offset,
    .infile_path = strdup(argv[1]),
    .blocksize = sector_size,
    .size = insize,
    .map = map,
    .mapfile = argv[3],
    .self = pthread_self(),
    .unsaved = false,
    .allowed = false,
    .recover_states = (1<<ME_NON_TRIMMED) | (1<<ME_NON_TRIED),
    .loglevel = LOGLEVEL_DEFAULT
  };
  pthread_mutex_init(&params.lock,0);
  pthread_t ctlt;
  int ret = pthread_create(&ctlt,0,cmd_controller,&params);
  if(ret < 0){
    perror("pthread_create failed");
    return 1;
  }
  pthread_detach(ctlt);
  char* options[] = {
    argv[0], "-s", "-f", "-o", "ro", "-o", "auto_unmount",
    "-o", "hard_remove", "-o", "max_readahead=0",
    "-o" "sync_read", "-o", "direct_io",
    argv[4]
  };
  struct fuse_args args = FUSE_ARGS_INIT(sizeof(options)/sizeof(*options), options);
  int es = fuse_main(args.argc, args.argv, &fr_oper, &params);
  fr_save_map(&params);
  pthread_kill(ctlt,SIGTERM);
  return es;
}
