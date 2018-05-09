#include <fuserescue/utils.h>

#include <errno.h>
#include <string.h>

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
