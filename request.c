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
  int line = 0;
  
  if(csrv_str_vec_init(&req->request) != 0) {
    CSRV_LOG_ERROR(req->csrv, "error while allocating str vec, errno=%s", strerror(errno));
    return;
  }
  
  for(;;) {
    // 1. Read a chunk out of the buffer
    CSRV_LOG_INFO(req->csrv, "csrv_parse_headers(): read loop");
    ssize_t sz_read = read(req->socket_handle, buffer, CSRV_CHUNK_SIZE);
    if(sz_read == -1) {
      CSRV_LOG_ERROR(req->csrv, "error during read from socket, errno=%s", strerror(errno));
      if(errno == EAGAIN) {
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
      free(buffer);
      return;
    }

    CSRV_LOG_INFO(req->csrv, "csrv_parse_headers() read %zu bytes", sz_read);
  }
  
  free(buffer);
  req->status = CSRV_OK;
}

