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

int csrv_read_header_chunk(struct CsrvRequest *req) {
  CSRV_LOG_INFO(req->csrv, "enter csrv_read_header_chunk()");
  char *buffer = (char *) malloc(CSRV_CHUNK_SIZE);
  size_t buffer_offs = 0;
  size_t n_tries = 0;
  bool done = false;
  
  if(csrv_str_vec_init(&req->request) != 0) {
    CSRV_LOG_ERROR(req->csrv, "error while allocating str vec, errno=%s", strerror(errno));
    return -1;
  }
  
  if(csrv_str_map_init(&req->headers.header_map) != 0) {
    CSRV_LOG_ERROR(req->csrv, "error while allocating str map, errno=%s", strerror(errno));
    return -1;
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
          return -1;
        }
        continue;
      }

      req->status = CSRV_HEADER_PARSE_FAILURE;
      free(buffer);
      return -1;
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
      return -1;
    }
    
    done = csrv_probe_header_end(req, buffer_offs);
    buffer_offs += sz_read;
    CSRV_LOG_INFO(req->csrv, "csrv_parse_headers() read %zu bytes", sz_read);
  }
  
  free(buffer);
  return 0;
}

bool csrv_probe_header_end(struct CsrvRequest *req, ssize_t buffer_offs) {
  for(ssize_t i = req->request.length; i - 4 > buffer_offs; i--) {
    char* section = &req->request.string[i - 3];
    if(strncmp(section, "\r\n\r\n", 4) == 0) {
      req->body_offset = i;
      return true;
    }
  }

  return false;
}

void csrv_parse_headers(struct CsrvRequest *req) {
  CSRV_LOG_INFO(req->csrv, "enter csrv_parse_headers()");

  if(csrv_read_header_chunk(req) != 0) {
    return;
  }
  
  struct CsrvStrVec vec;
  if(csrv_str_vec_init(&vec) != 0) {
    // Adding a label for goto -- removes a little bit of boilerplate
  parse_alloc_fail:
    CSRV_LOG_ERROR(req->csrv, "error while allocating (header level), errno=%s", strerror(errno));
    req->status = CSRV_ALLOC_FAILURE;
    return;
  }
  
  char *key;
  char *value;
  enum CsrvHeaderParseState state = CSRV_HEADER_PARSE_METHOD;
  // Individually parse out headers
  for(size_t i = 0; i < req->body_offset; i++) {
    char current = req->request.string[i];
    if(current == ':' && state == CSRV_HEADER_PARSE_KEY) {
      key = csrv_str_vec_value(&vec);
      if(key == NULL) {
        goto parse_alloc_fail;
      }
      if(csrv_str_vec_init(&vec) != 0) {
        goto parse_alloc_fail;
      }
      
      state = CSRV_HEADER_PARSE_WHITESPACE;
      continue;
    }

    if(current == '\r' || current == '\n') {
      switch(state) {
        case CSRV_HEADER_PARSE_PROTO:
          req->headers.proto = csrv_str_vec_value(&vec);
          if(req->headers.proto == NULL) {
            goto parse_alloc_fail;
          }
          if(csrv_str_vec_init(&vec) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_RETURN;
          break;
        case CSRV_HEADER_PARSE_VALUE:
          value = csrv_str_vec_value(&vec);
          if(value == NULL) {
            goto parse_alloc_fail;
          }
          csrv_str_map_add(&req->headers.header_map, key, value);
          free(key);
          if(csrv_str_vec_init(&vec) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_RETURN;
          break;
        case CSRV_HEADER_PARSE_RETURN:
          break;
        default:
          goto parse_error;
      }
      continue;
    }

    if(isspace(current)) {
      switch(state) {
        case CSRV_HEADER_PARSE_METHOD:
          req->headers.method = csrv_str_vec_value(&vec);
          if(req->headers.method == NULL) {
            goto parse_alloc_fail;
          }
          if(csrv_str_vec_init(&vec) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_AFTER_METHOD;
          break;
        case CSRV_HEADER_PARSE_URI:
          req->headers.uri = csrv_str_vec_value(&vec);
          if(req->headers.uri == NULL) {
            goto parse_alloc_fail;
          }
          if(csrv_str_vec_init(&vec) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_AFTER_URI;
          break;
        case CSRV_HEADER_PARSE_AFTER_METHOD:
        case CSRV_HEADER_PARSE_AFTER_URI:
        case CSRV_HEADER_PARSE_WHITESPACE:
          break;
        default:
          goto parse_error;
      }
      continue;
    }

    if(!isspace(current)) {
      switch(state) {
        case CSRV_HEADER_PARSE_METHOD:
        case CSRV_HEADER_PARSE_PROTO:
        case CSRV_HEADER_PARSE_URI:
        case CSRV_HEADER_PARSE_VALUE:
        case CSRV_HEADER_PARSE_KEY:
          if(csrv_str_vec_pushc(&vec, current) != 0) {
            goto parse_alloc_fail;
          }
          break;
        case CSRV_HEADER_PARSE_AFTER_METHOD:
          if(csrv_str_vec_pushc(&vec, current) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_URI;
          break;
        case CSRV_HEADER_PARSE_AFTER_URI:
          if(csrv_str_vec_pushc(&vec, current) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_PROTO;
          break;
        case CSRV_HEADER_PARSE_WHITESPACE:
          if(csrv_str_vec_pushc(&vec, current) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_VALUE;
          break;
        case CSRV_HEADER_PARSE_RETURN:
          if(csrv_str_vec_pushc(&vec, current) != 0) {
            goto parse_alloc_fail;
          }
          state = CSRV_HEADER_PARSE_KEY;
          break;
        default:
          goto parse_error;
      }
      continue;
    }
    
  parse_error:
    CSRV_LOG_ERROR(req->csrv, "unhandled char='%c' and state=%d during parse", current, state);
    req->status = CSRV_HEADER_PARSE_FAILURE;
    return;
  }
  
  if(csrv_set_request_meta(req) != 0) {
    goto parse_alloc_fail;
  }

  req->status = CSRV_OK;
}

int csrv_set_request_meta(struct CsrvRequest *req) {
  char* len = csrv_str_map_get(&req->headers.header_map, "Content-Length");
  if(len == NULL) {
    req->headers.content_size = 0;
    return 0;
  }
  
  if(sscanf(len, "%zu", &req->headers.content_size) != 1) {
    req->headers.content_size = 0;
    return -1;
  }

  return 0;
}

