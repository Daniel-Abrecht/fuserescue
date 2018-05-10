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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int cmd_printmap(struct fuserescue* fr, int argc, char* argv[argc]){
  (void)argv;
  pthread_mutex_lock(&fr->lock);
  map_write(fr->map,STDOUT_FILENO);
  pthread_mutex_unlock(&fr->lock);
  return 0;
}

static int cmd_reopen(struct fuserescue* fr, int argc, char* argv[argc]){
  if(argc > 2){
    printf("usage: %s [infile]",argv[0]);
    return 1;
  }
  const char* path = argc < 2 ? fr->infile_path : argv[1];
  int infile = open( path, O_RDONLY | O_DIRECT | O_BINARY );
  if(infile == -1){
    perror("Failed to open file");
    return 2;
  }
  if(dup2(infile,fr->infile)<0){
    perror("dup2 failed");
    close(infile);
    return 2;
  }
  close(infile);
  fr->infile_path = path;
  return 0;
}


static int cmd_save(struct fuserescue* fr, int argc, char* argv[argc]){
  (void)argc;
  (void)argv;
  fr_save_map(fr);
  return 0;
}

static int cmd_exit(struct fuserescue* fr, int argc, char* argv[argc]){
  (void)argc;
  (void)argv;
  pthread_kill(fr->self,SIGTERM);
  return 0;
}

static int cmd_blocksize(struct fuserescue* fr, int argc, char* argv[argc]){
  if(argc > 2){
    printf("usage: %s [size]",argv[0]);
    return 1;
  }
  pthread_mutex_lock(&fr->lock);
  if(argc == 2){
    uint64_t size;
    const char* s = argv[1];
    if(!parseu64(&s,&size)){
      perror("Failed to parse size");
    }else if(size > (uint64_t)DIRECTIO_BUFFER_SIZE){
      fprintf(stderr,"Blocksize too big, can't be bigger than %d\n",DIRECTIO_BUFFER_SIZE);
    }else{
      fr->blocksize = size;
    }
  }
  printf("blocksize = %"PRIu64"\n",fr->blocksize);
  pthread_mutex_unlock(&fr->lock);
  return 0;
}

static int cmd_recovery(struct fuserescue* fr, int argc, char* argv[argc]){
  if(argc < 2 || !(!strcmp(argv[1],"allow")||!strcmp(argv[1],"denay")||!strcmp(argv[1],"show"))){
    printf("usage: %s allow|denay|show [nontried|nontrimed|nonscraped|badsector]\n",argv[0]);
    printf("  %s allow: Allows reading from device to backup. \n",argv[0]);
    printf("  %s allow nonscraped: Allows trying to read nonscraped sectors. \n",argv[0]);
    puts("\n*** Changes won't affect for recovery attemps already in progress ***\n");
    goto show;
  }

  if(!strcmp(argv[1],"show"))
    goto show;

  pthread_mutex_lock(&fr->lock);
  bool allow = !strcmp(argv[1],"allow");
  if(argc==2){
    fr->allowed = allow;
  }else{
    unsigned mask = 0;
    for(int i=2; i<argc; i++){
      if(!strcmp("nontried",argv[i])){
        mask |= 1<<ME_NON_TRIED;
      }else if(!strcmp("nontrimmed",argv[i])){
        mask |= 1<<ME_NON_TRIMMED;
      }else if(!strcmp("nonscraped",argv[i])){
        mask |= 1<<ME_NON_SCRAPED;
      }else if(!strcmp("badsector",argv[i])){
        mask |= 1<<ME_BAD_SECTOR;
      }
    }
    if(allow){
      fr->recover_states |= mask;
    }else{
      fr->recover_states &= ~mask;
    }
  }
  pthread_mutex_unlock(&fr->lock);

  show: {
    pthread_mutex_lock(&fr->lock);
    puts(fr->allowed ? "recovery mode: allow" : "recovery mode: denay");
    printf("sections to recover: ");
    if((1<<ME_NON_TRIED) & fr->recover_states)
      printf("nontried ");
    if((1<<ME_NON_TRIMMED) & fr->recover_states)
      printf("nontrimmed ");
    if((1<<ME_NON_SCRAPED) & fr->recover_states)
      printf("nonscraped ");
    if((1<<ME_BAD_SECTOR) & fr->recover_states)
      printf("badsector ");
    puts("");
    pthread_mutex_unlock(&fr->lock);
  }

  return 0;
}

static int cmd_loglevel(struct fuserescue* fr, int argc, char* argv[argc]){
  bool error = false;

  if(argc == 2){
    if(!strcmp("default",argv[1])){
      fr->loglevel = LOGLEVEL_DEFAULT;
    }else if(!strcmp("info",argv[1])){
      fr->loglevel = LOGLEVEL_INFO;
    }else{
      error = true;
    }
  }

  if(error || argc > 2){
    printf("usage: %s default info\n",argv[0]);
  }

  if(fr->loglevel == LOGLEVEL_DEFAULT){
    printf("loglevel = default\n");
  }else if(fr->loglevel == LOGLEVEL_INFO){
    printf("loglevel = info\n");
  }

  return 0;
}

struct commands {
  const char* name;
  int (*function)(struct fuserescue* fr,int argc, char* argv[argc]);
  const char* description;
};

static int cmd_help(struct fuserescue* fr, int argc, char* argv[argc]);

static struct commands command_list[] = {
  {"help",cmd_help,"Displays a list of commands"},
  {"save",cmd_save,"Saves the mapfile"},
  {"exit",cmd_exit,"Exits the program"},
  {"recovery",cmd_recovery,"Allow reading from device to backup. Arguments: allow|denay|show [nontried|nontrimed|nonscraped|badsector]"},
  {"printmap",cmd_printmap,"Write the mapfile to stdout"},
  {"reopen",cmd_reopen,"Reopen mapfile. You can optionally specify the file if it changed location"},
  {"blocksize",cmd_blocksize,"Get or set biggest unit of data tried to recover at once."},
  {"loglevel",cmd_loglevel,"Get or set loglevel\n"}
};
static size_t command_count = sizeof(command_list)/sizeof(*command_list);

static int cmd_help(struct fuserescue* fr, int argc, char* argv[argc]){
  (void)fr;
  (void)argc;
  (void)argv;
  puts("Available commands are:");
  for(size_t i=0; i<command_count; i++)
    printf("  %s \t- %s\n",command_list[i].name,command_list[i].description);
  return 0;
}

// TODO: use readline or something similar, implement history and such stuff
void* cmd_controller(void* param){
  struct fuserescue* fr = param;
  static char buffer[1024];
  printf("fuserescue shell. Type help and list of commands\n");
  printf("> ");
  fflush(stdout);
  char* s;
  while((s=fgets(buffer,sizeof(buffer),stdin))){
    skip_spaces((const char**)&s);
    size_t n = strlen(s);
    if(n && s[n-1] == '\n')
      s[--n] = 0;
    if(!n) goto next;

    int argc=0;
    char* argv[100];
    char* pch = strtok(s," \t");
    while(pch){
      if(argc>=100)
        break;
      argv[argc++] = pch;
      pch = strtok(0, " \t");
    }
    if(!argc) goto next;

    size_t i;
    for(i=0; i<command_count; i++){
      if(!strcmp(argv[0],command_list[i].name))
        break;
    }
    if(i>=command_count){
      printf("Command not found\n");
    }else{
      command_list[i].function(fr,argc,argv);
    }

  next:
    printf("> ");
    fflush(stdout);
  }
  return 0;
}
