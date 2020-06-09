#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "csrv.h"

int csrv_str_vec_init(struct CsrvStrVec *vec) {
  vec->buff_sz = CSRV_STR_VEC_SIZE;
  vec->length = 0;
  vec->realloc_count = 0;
  vec->string = (char *) malloc(vec->buff_sz * sizeof(char));
  if(vec->string == NULL) {
    return -1;
  };

  return 0;
}

int csrv_str_vec_pushc(struct CsrvStrVec *vec, char c) {
  if(vec->length == vec->buff_sz) {
    vec->realloc_count++;
    vec->string = (char *) realloc(vec->string, vec->buff_sz << 1);
    if(vec->string == NULL) {
      return -1;
    }
    
    vec->buff_sz = vec->buff_sz << 1;
  }

  vec->string[vec->length++] = c;
  return 0;
}

int csrv_str_vec_pushn(struct CsrvStrVec *vec, char* buffer, size_t sz) {
  // realloc to the nearest CHUNK_SIZE
  if(sz > vec->buff_sz - vec->length) {
    size_t new_sz = sz + vec->buff_sz + ((sz + vec->buff_sz) % CSRV_STR_VEC_SIZE);
    vec->string = (char *) realloc(vec->string, new_sz);
    
    if(vec->string == NULL) {
      return -1;
    }
  }

  memcpy(&vec->string[vec->length], buffer, sz);
  vec->length += sz;
  return 0; 
}

char *csrv_str_vec_value(struct CsrvStrVec *vec) {
  // TODO: handle realloc failure
  csrv_str_vec_pushc(vec, '\0');
  return vec->string;
}

