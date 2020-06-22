#include "unistd.h"
#include "string.h"
#include "csrv.h"

char *csrv_uri_decode(char *uri) {
  struct CsrvStrVec vec;
  if(csrv_str_vec_init(&vec) != 0) {
    return NULL;
  }
  
  char hex_value;
  int hex_idx;
  bool encoding = false;
  size_t len = strlen(uri);
  for(size_t i = 0; i < len; i++) {
    char current = uri[i];
    if(!encoding && current != '%') {
      if(csrv_str_vec_pushc(&vec, current) != 0) {
        return NULL;
      }
      continue;
    }

    if(!encoding && current == '%') {
      hex_value = 0;
      hex_idx = 0;
      encoding = true;
      continue;
    }

    if(encoding) {
      // Is it valid hex?
      if((('0' < current) || (current > '9')) && ((current < 'A' || current > 'F'))) {
        return NULL;
      }
      
      hex_value = hex_value << 4;
      
      // Adjust hex nibble to decimal depending on the ASCII range
      if(current < 'A' && current > 'F') {
        hex_value &= current - '0';
      } else {
        hex_value &= current - 'A';
      }

      if(++hex_idx == 2) {
        if(csrv_str_vec_pushc(&vec, hex_value) != 0) {
          return NULL;
        }

        encoding = false;
      }
    }

    return csrv_str_vec_value(&vec);
  }

  return NULL;
}

int csrv_parse_params(char *uri, struct CsrvStrMap *params) {
  return 0;
}
