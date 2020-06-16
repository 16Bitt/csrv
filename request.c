#include "sys/types.h"
#include "sys/socket.h"
#include "netinet/in.h"
#include "netinet/ip.h"
#include "string.h"
#include "stdio.h"
#include "errno.h"
#include "stdlib.h"
#include "fcntl.h"
#include "unistd.h"
#include "stdbool.h"
#include "ctype.h"
#include "csrv.h"

struct CsrvRequest *csrv_alloc_request(struct Csrv *csrv, int new_socket_handle) {
  CSRV_LOG_INFO(csrv, "enter csrv_alloc_request()");
  struct CsrvRequest *req = (struct CsrvRequest *) calloc(1, sizeof(struct CsrvRequest));
  if(req == NULL) {
    csrv->status = CSRV_ALLOC_FAILURE;
    CSRV_LOG_ERROR(csrv, "malloc failed with errno=%d", errno);
    return NULL;
  }
  
  req->csrv = csrv;
  req->socket_handle = new_socket_handle;
  req->status = CSRV_OK;
  req->id = csrv->request_id_max++;
  
  csrv->active_requests++;
  csrv->status = CSRV_OK;
  return req;
}

void csrv_cleanup_request(struct CsrvRequest *req) {
  CSRV_LOG_INFO(req->csrv, "enter csrv_cleanup_request()");

  struct Csrv *csrv = req->csrv;
  csrv->active_requests--;
  free(req);
  csrv->status = CSRV_OK;
}


void csrv_parse_headers(struct CsrvRequest *req) {
  CSRV_LOG_INFO(req->csrv, "enter csrv_parse_headers()");
  char *buffer = (char *) malloc(CSRV_CHUNK_SIZE);
  size_t buffer_offs = 0;
  size_t n_tries = 0;
  bool done = false;
  
  if(csrv_str_vec_init(&req->request) != 0) {
    CSRV_LOG_ERROR(req->csrv, "error while allocating str vec, errno=%s", strerror(errno));
    return;
  }
  
  if(csrv_str_map_init(&req->headers.header_map) != 0) {
    CSRV_LOG_ERROR(req->csrv, "error while allocating str map, errno=%s", strerror(errno));
    return;
  }
  
  // Read enough data to read the headers
  while(!done) {
    // 1. Read a chunk out of the buffer
    CSRV_LOG_INFO(req->csrv, "csrv_parse_headers(): read loop");
    ssize_t sz_read = read(req->socket_handle, buffer, CSRV_CHUNK_SIZE);
    if(sz_read == -1) {
      CSRV_LOG_ERROR(req->csrv, "error during read from socket, errno=%s", strerror(errno));
      if(errno == EAGAIN) {
        n_tries++;
        if(n_tries > CSRV_MAX_EAGAIN_TRIES) {
          CSRV_LOG_ERROR(req->csrv, "could not read resource after %zu tries, aborting", n_tries);
          req->status = CSRV_RETRY_EXCEEDED;
          return;
        }
        continue;
      }

      req->status = CSRV_HEADER_PARSE_FAILURE;
      free(buffer);
      return;
    }
    
    // read() returns 0 on EOF
    if(sz_read == 0) {
      break;
    }
    
    // 2. Write the chunk back to the string vector
    if(csrv_str_vec_pushn(&req->request, buffer, sz_read) != 0) {
      CSRV_LOG_ERROR(req->csrv, "error during csrv_str_vec_pushn(), errno=%d", errno);
      req->status = CSRV_ALLOC_FAILURE;
      free(buffer);
      return;
    }

    for(ssize_t i = req->request.length; i - 4 > buffer_offs; i--) {
      char* section = &req->request.string[i - 3];
      if(strncmp(section, "\r\n\r\n", 4) == 0) {
        req->body_offset = i;
        done = true;
        break;
      }
    }
    buffer_offs += sz_read;

    CSRV_LOG_INFO(req->csrv, "csrv_parse_headers() read %zu bytes", sz_read);
  }
  free(buffer);
  
  struct CsrvStrVec vec;
  if(csrv_str_vec_init(&vec) != 0) {
    // Adding a label for goto -- removes a little bit of boilerplate
  parse_alloc_fail:
    CSRV_LOG_ERROR(req->csrv, "error while allocating (header level), errno=%s", strerror(errno));
    req->status = CSRV_ALLOC_FAILURE;
    return;
  }
  
  char *key;
  enum CsrvHeaderParseState state = CSRV_HEADER_PARSE_METHOD;
  // Individually parse out headers
  for(size_t i = 0, line = 1; i < req->body_offset; i++) {
    char current = req->request.string[i];

    if(line == 1) {
      if((current == '\n' || current == '\r') && state == CSRV_HEADER_PARSE_PROTO) {
        line++;
        state = CSRV_HEADER_PARSE_RETURN;
        continue;
      }
      
      if(isspace(current) && state == CSRV_HEADER_PARSE_METHOD) {
        req->headers.method = csrv_str_vec_value(&vec);
        if(req->headers.method == NULL) {
          goto parse_alloc_fail;
        }

        if(csrv_str_vec_init(&vec) != 0) {
          goto parse_alloc_fail;
        }
        
        state = CSRV_HEADER_PARSE_AFTER_METHOD;
        continue;
      }
      
      if(isspace(current) && state == CSRV_HEADER_PARSE_URI) {
        req->headers.uri = csrv_str_vec_value(&vec);
        if(req->headers.method == NULL) {
          goto parse_alloc_fail;
        }

        if(csrv_str_vec_init(&vec) != 0) {
          goto parse_alloc_fail;
        }
        
        state = CSRV_HEADER_PARSE_AFTER_URI;
        continue;
      }

      if(!isspace(current) && (state == CSRV_HEADER_PARSE_URI || state == CSRV_HEADER_PARSE_METHOD || state == CSRV_HEADER_PARSE_PROTO)) {
        if(csrv_str_vec_pushc(&vec, current) != 0) {
          goto parse_alloc_fail;
        }

        continue;
      }

      if(!isspace(current)) {
        if(csrv_str_vec_pushc(&vec, current) != 0) {
          goto parse_alloc_fail;
        }

        if(state == CSRV_HEADER_PARSE_AFTER_METHOD) {
          state = CSRV_HEADER_PARSE_URI;
          continue;
        }
        
        if(state == CSRV_HEADER_PARSE_AFTER_URI) {
          state = CSRV_HEADER_PARSE_PROTO;
          continue;
        }

        goto parse_error;
      }

      if(isspace(current) && (state == CSRV_HEADER_PARSE_AFTER_METHOD || state == CSRV_HEADER_PARSE_AFTER_URI)) {
        continue;
      }

      goto parse_error;
    }

    if(current == ':' && state == CSRV_HEADER_PARSE_KEY) {
      state = CSRV_HEADER_PARSE_WHITESPACE;
      key = csrv_str_vec_value(&vec);

      if(key == NULL) {
        goto parse_alloc_fail;
      }

      continue;
    }
    
    if((current == '\r' || current == '\n') && state == CSRV_HEADER_PARSE_VALUE) {
      state = CSRV_HEADER_PARSE_RETURN;
      char *val = csrv_str_vec_value(&vec);
      if(val == NULL) {
        goto parse_alloc_fail;
      }

      csrv_str_map_add(&req->headers.header_map, key, val);

      if(csrv_str_vec_init(&vec) != 0) {
        goto parse_alloc_fail;
      }
      
      continue;
    }
    
    if((current == '\r' || current == '\n') && state == CSRV_HEADER_PARSE_RETURN) {
      continue;
    }
    
    if(!(current == '\r' || current == '\n') && state == CSRV_HEADER_PARSE_RETURN) {
      state = CSRV_HEADER_PARSE_KEY;
      if(csrv_str_vec_pushc(&vec, current) != 0) {
        goto parse_alloc_fail;
      }
      continue;
    }

    if(isspace(current) && state == CSRV_HEADER_PARSE_WHITESPACE) {
      continue;
    }

    if(!isspace(current) && state == CSRV_HEADER_PARSE_WHITESPACE) {
      state = CSRV_HEADER_PARSE_VALUE;
      if(csrv_str_vec_pushc(&vec, current) != 0) {
        goto parse_alloc_fail;
      }
      continue;
    }
    
    if(!isspace(current) && (state == CSRV_HEADER_PARSE_KEY || state == CSRV_HEADER_PARSE_VALUE)) {
      if(csrv_str_vec_pushc(&vec, current) != 0) {
        goto parse_alloc_fail;
      }

      continue;
    }

  parse_error:
    CSRV_LOG_ERROR(req->csrv, "unhandled char='%c' and state=%d during parse", current, state);
    req->status = CSRV_HEADER_PARSE_FAILURE;
    return;
  }


  req->status = CSRV_OK;
}

