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

#include <fuserescue/map.h>
#include <fuserescue/utils.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


bool map_normalize(struct mapfile* map){
  struct mapentry* entries = map->entries;
  for(size_t j=map->count; j--;)
    for(size_t i=0; i<j; i++)
      if(entries[i].offset > entries[i+1].offset){
        printf("switching %zu <> %zu\n",i,i+1);
        struct mapentry e = entries[i];
        entries[i] = entries[i+1];
        entries[i+1] = e;
      }
  for(size_t i=1; i<map->count; i++){
    uint64_t e = entries[i-1].offset + entries[i-1].size;
    if(entries[i].offset<e){
      fprintf(stderr,"Failed to normalize mapfile: overlapping entries not allowed. %"PRIx64"-%"PRIx64" %"PRIx64"-%"PRIx64"\n",entries[i-1].offset,e,entries[i].offset,entries[i].offset+entries[i].size);
      return false;
    }
    if( entries[i].offset==e && entries[i].state==entries[i-1].state ){
      entries[i-1].size += entries[i].size;
      map_move(map,i+1,-1);
    }
  }
  return true;
}

struct mapfile* map_read(const char* file){
  struct mapfile* map = calloc(1,sizeof(struct mapfile));
  if(!map){
    perror("failed to allocate map file");
    return 0;
  }
  FILE* f = fopen(file,"rb");
  if(!f){
    if(errno==ENOENT)
      return map;
    perror("Faile to open map file");
    free(map);
    return 0;
  }
  char buffer[257] = {0};
  bool skip = false;
  enum {
    ST_STATUSLINE,
    ST_ENTRY
  } state = ST_STATUSLINE;
  while(fgets(buffer,sizeof(buffer),f)){
    size_t size = strlen(buffer);
    if(skip && buffer[size-1] != '\n')
      continue;
    skip = false;
    if(buffer[0] == '#'){
      skip = buffer[size-1] == '\n';
      continue;
    }
    if(buffer[size-1] != '\n'){
      fprintf(stderr,"Reading mapfile failed, line too long\n");
      free(map);
      fclose(f);
      return 0;
    }
    buffer[--size] = 0;

    const char* s = buffer;

    switch(state){
      case ST_STATUSLINE: {
        skip_spaces(&s);
        if(!*s) continue;
        if(!parseu64(&s,&map->total)){
          perror("Failed to parse total");
          return 0;
        }
        skip_spaces(&s);
        char c = *s;
        switch(c){
          case '?': map->state=MF_NON_TRIED; break;
          case '*': map->state=MF_NON_TRIMMED; break;
          case '/': map->state=MF_NON_SCRAPED; break;
          case '-': map->state=MF_BAD_SECTOR; break;
          case 'F': map->state=MF_SPECIFIED_BLOCKS; break;
          case 'G': map->state=MF_APPROXIMATE; break;
          case '+': map->state=MF_FINISHED; break;
          default: fprintf(stderr,"Failed to parse status\n"); return 0;
        }
        state = ST_ENTRY;
      }; break;
      case ST_ENTRY: {
        skip_spaces(&s);
        if(!*s) continue;
        if(map->count >= ENTRIES_MAX){
          fprintf(stderr,"Mapfile contains more than %zu entries\n",(size_t)ENTRIES_MAX);
          fclose(f);
          free(map);
          return 0;
        }
        struct mapentry* e = map->entries + map->count++;
        if(!parseu64(&s,&e->offset)){
          perror("Failed to parse offset");
          return 0;
        }
        skip_spaces(&s);
        if(!parseu64(&s,&e->size)){
          perror("Failed to parse size");
          return 0;
        }
        skip_spaces(&s);
        char c = *s;
        switch(c){
          case '?': e->state=ME_NON_TRIED; break;
          case '*': e->state=ME_NON_TRIMMED; break;
          case '/': e->state=ME_NON_SCRAPED; break;
          case '-': e->state=ME_BAD_SECTOR; break;
          case '+': e->state=ME_FINISHED; break;
          default: fprintf(stderr,"Failed to parse map file entry\n"); return 0;
        }
      } break;
    }
  }
  fclose(f);
  if(!map_normalize(map)){
    free(map);
    return 0;
  }
  return map;
}

bool map_move(struct mapfile* map, size_t i, ssize_t n){
  if( i > map->count ){
    errno = EINVAL;
    return false;
  }
  if( n<0 && map->count < (size_t)-n ){
    errno = EINVAL;
    return false;
  }
  if( n>0 && ENTRIES_MAX - map->count < (size_t)n ){
    errno = ENOMEM;
    return false;
  }
  size_t m = map->count - i;
  if(m) memmove(map->entries+i+n,map->entries+i,m*sizeof(struct mapentry));
  map->count += n;
  return true;
}

bool map_write(struct mapfile* map, int fd){
  char c[18] = {0};
  const char txt1[] = {
    "# Mapfile. Created by fuserescue\n"
    "#\n"
    "# current_pos  current_status\n"
    "0  +\n"
    "#      pos        size  status\n"
  };

  if(write(fd,txt1,sizeof(txt1)-1) < 0)
    return false;

  for( size_t i=0,n=map->count; i<n; i++ ){
    int n;
    n = u64toa(map->entries[i].offset,c);
    if(write(fd,c,n) < 0)
      return false;
    if(write(fd,"  ",2) < 0)
      return false;
    n = u64toa(map->entries[i].size,c);
    if(write(fd,c,n) < 0)
      return false;
    char s;
    switch(map->entries[i].state){
      case ME_FINISHED: s = '+'; break;
      case ME_BAD_SECTOR: s = '-'; break;
      case ME_NON_SCRAPED: s = '/'; break;
      case ME_NON_TRIMMED: s = '*'; break;
      case ME_NON_TRIED: default: s = '?'; break;
    }
    if(write(fd,(char[]){' ',' ',s,'\n'},4) < 0)
      return false;
  }

  return true;
}

void map_update(struct mapfile* map, uint64_t start, uint64_t end, enum mapentry_state state){
  struct mapentry* entries = map->entries;
  bool inserted = false;
  size_t i,n;
  for(i=0,n=map->count; i<n; i++){
    uint64_t entry_start = entries[i].offset;
    uint64_t entry_end = entries[i].offset+entries[i].size;
    if( entry_end < start )
      continue;
    if( entry_start > end )
      break;
    if(state == entries[i].state){
      if(start < entry_start){
        size_t s = entry_start - start;
        entries[i].offset = start;
        entries[i].size += s;
      }
      if(end > entry_end){
        entries[i].size += end - entry_end;
      }
      inserted=true;
      i++;
      break;
    }else{
      if(start <= entry_start && end >= entry_end){
        entries[i].offset = start;
        entries[i].size = end-start;
        entries[i].state = state;
        inserted=true;
        i++;
        break;
      }
      if(start <= entry_start){
        entries[i].offset = end;
        entries[i].size = entry_end - end;
        if(!map_move(map,i,1)){
          perror("map_move failed");
          exit(4);
        }
        entries[i].offset = start;
        entries[i].size = end - start;
        entries[i].state = state;
        inserted=true;
        i += 2;
        break;
      }
      if(end >= entry_end){
        entries[i].size = start - entry_start;
        i++;
        if(!map_move(map,i,1)){
          perror("map_move failed");
          exit(4);
        }
        entries[i].offset = start;
        entries[i].size = end - start;
        entries[i].state = state;
        inserted=true;
        i++;
        break;
      }
      entries[i].size = start - entry_start;
      i++;
      if(!map_move(map,i,2)){
        perror("map_move failed");
        exit(4);
      }
      entries[i].offset = start;
      entries[i].size = end - start;
      entries[i].state = state;
      i++;
      entries[i].offset = end;
      entries[i].size = entry_end - end;
      entries[i].state = entries[i-2].state;
      inserted=true;
      i++;
      break;
    }
  }
  if(!inserted){
    if(!map_move(map,i,1)){
      perror("map_move failed");
      exit(4);
    }
    entries[i].offset = start;
    entries[i].size = end - start;
    entries[i].state = state;
    i++;
  }
  size_t j = --i + 1;
  uint64_t e_end = entries[i].offset + entries[i].size;
  for(n=map->count; j<n; j++){
    uint64_t entry_start = entries[j].offset;
    uint64_t entry_end = entries[j].offset+entries[j].size;
    if( entry_start > e_end )
      break;
    if( entry_end > e_end ){
      if(entries[j].state == state){
        entries[i].size += entry_end - e_end;
        e_end = entry_end;
      }else{
        entries[j].offset = e_end;
        entries[j].size = entry_end - e_end;
        break;
      }
    }
  }
  if(j > i+1){
    map_move(map,j,i+1-j);
  }
}

