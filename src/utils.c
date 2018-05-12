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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <fuserescue/utils.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool parseu64(const char** str, uint64_t* ret){
  uint64_t res = 0;
  int base = 10;
  const char* s = *str;
  size_t i=0;
  for(char c;(c=*s);s++,i++){
    if(res * base < res){
      errno = EOVERFLOW;
      return false;
    }
    if( i == 0 && c == '0' )
      base = 8;
    if( i == 1 && res == 0 && c == 'x' ){
      base = 16;
      continue;
    }
    int digit = 0;
    if( c <= '9' && c >= '0' ){
      digit = c-'0';
    }else if( c <= 'z' && c >= 'a' ){
      digit = c-'a'+10;
    }else if( c <= 'Z' && c >= 'A' ){
      digit = c-'A'+10;
    }else digit = -1;
    if( digit<0 || digit>=base )
      break;
    res = res * base + digit;
  }
  if(!i){
    errno = EINVAL;
    return false;
  }
  *ret = res;
  *str = s;
  return true;
}

int u64toa(uint64_t x, char r[18]){
  int i = 18;
  do {
    int c = x % 16;
    r[--i] = c<10 ? c+'0' : c-10+'A';
  } while(x /= 16);
  r[--i] = 'x';
  r[--i] = '0';
  if(i) memmove(r,r+i,18-i);
  return 18 - i;
}

void skip_spaces(const char** x){
  const char* c = *x;
  while(*c && isspace(*c)) c++;
  *x = c;
}

static size_t getcmd(size_t size, char buffer[size], const char* str, bool shell){
  if(size < 2)
    return 0;
  size--;
  bool inquote = false;
  bool backslash = false;
  bool good = false;
  const char* tmp = str;
  skip_spaces(&str);
  size_t i,j;
  for(i=0,j=0; i<size && str[j]; i++,j++){
    char c = str[j];
    if(backslash){
      buffer[i] = c;
      backslash = false;
      continue;
    }
    if(c == '\\'){
      backslash = true;
      i--;
      continue;
    }
    if(c == '"'){
      inquote = !inquote;
      continue;
    }
    // TODO: handle environment variables
    if(!inquote && (isspace(c) || (shell && (c=='|'||c=='&'||c==';'||c=='#'||c=='<'||c=='>')))){
      good = true;
      break;
    }
    buffer[i] = c;
  }
  buffer[i] = 0;
  if(!good && str[j])
    return 0;
  return str-tmp+j;
}

struct pager pager_create(const char** commands, bool shell){
  int io[2]={-1,-1}, checker[2]={-1,-1};
  if( pipe(io)      == -1
   || pipe(checker) == -1
   || fcntl(checker[1], F_SETFD, FD_CLOEXEC) == -1
  ) goto nonfatal_error;
  const char* default_commands[4] = {0};
  if(!commands || !*commands){
    int i = 0;
    const char* pager = getenv("PAGER");
    if(pager)
      default_commands[i++] = pager;
    default_commands[i++] = "less";
    default_commands[i++] = "more";
    commands = default_commands;
  }
  int pid = fork();
  if(pid<0)
    goto nonfatal_error;
  if(pid){
    close(io[0]);
    close(checker[1]);
    int result = 0;
    int ret;
    do {
      ret = read(checker[0],&result,sizeof(result));
    } while(ret && !( ret == -1 && errno == -EINTR ));
    if(result){
      goto nonfatal_error;
    }
    close(checker[0]);
    return (struct pager){pid,io[1]};
  }else{
    close(io[1]);
    close(checker[0]);
    if(dup2(io[0],STDIN_FILENO) == -1)
      goto fork_error;
    close(io[0]);
    static char cmd[1024];
    for(;*commands;commands++){
      if(shell){
        getcmd(sizeof(cmd),cmd,*commands,true);
        char* path = strdup(getenv("PATH"));
        if(!path)
          goto fork_error;
        char* dir = strtok(path,":");
        char* prog = 0;
        while(dir){
          prog = malloc(strlen(dir)+strlen(cmd)+2);
          if(!prog)
            goto fork_error;
          sprintf(prog,"%s/%s",dir,cmd);
          if(!access(prog, X_OK))
            break;
          prog = 0;
          free(prog);
          dir = strtok(0, ":");
        }
        free(path);
        if(!prog)
          continue;
        free(prog);
        execlp("sh","sh","-c",*commands,0);
      }else{
        static const char* args[256];
        const char* it = *commands;
        size_t ret;
        size_t i = 0;
        for(; i<255; it+=ret, i++){
          skip_spaces(&it);
          if(!(ret=getcmd(sizeof(cmd),cmd,it,false)))
            break;
          char* x = malloc(ret+1);
          if(!x)
            goto fork_error;
          memcpy(x,it,ret);
          x[ret] = 0;
          args[i] = x;
        }
        args[i++] = 0;
        execvp(args[0],(char*const*)args);
      }
    }
  fork_error:;
    int err = errno;
    write(checker[1],&err,sizeof(err));
    close(checker[1]);
    close(io[0]);
    close(STDIN_FILENO);
  }
  exit(1);
nonfatal_error:
  close(io[1]);
  close(io[0]);
  close(checker[0]);
  close(checker[1]);
  return (struct pager){0,STDOUT_FILENO};
}

void pager_close_wait(struct pager* pager){
  if(pager->input == -1)
    return;
  if(pager->pid != 0)
    close(pager->input);
  pager->input = -1;
  if(pager->pid <= 0)
    return;
  while(waitpid(pager->pid,0,0) == -1 && errno == EINTR);
}
