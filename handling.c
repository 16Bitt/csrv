#include "stdio.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "csrv.h"

char *csrv_response_status_string(enum CsrvResponseStatus status) {
  switch(status) {
    case CSRV_HTTP_OK:
      return "200 OK";
    case CSRV_HTTP_NOT_FOUND:
      return "404 Not Found";
    case CSRV_HTTP_UNAUTHORIZED:
      return "401 Unauthorized";
    case CSRV_HTTP_BAD_REQUEST:
      return "400 Bad Request";
    case CSRV_HTTP_SERVER_ERROR:
    default:
      return "500 Internal Server Error";
  }
}

struct CsrvResponse *csrv_init_response(struct CsrvRequest *req) {
  struct CsrvResponse *resp = (struct CsrvResponse *) malloc(sizeof(struct CsrvResponse));
  if(resp == NULL) {
    CSRV_LOG_ERROR(req->csrv, "failed to malloc response, errno=%s", strerror(errno));
    return NULL;
  }
  
  // Default the status value
  resp->status = CSRV_HTTP_OK;
  resp->socket_handle = req->socket_handle;
  resp->csrv = req->csrv;

  if(csrv_str_map_init(&resp->headers) != 0) {
    CSRV_LOG_ERROR(req->csrv, "failed to init str map, errno=%s", strerror(errno));
    free(resp);
    return NULL;
  }

  if(csrv_str_vec_init(&resp->body) != 0) {
    csrv_str_map_cleanup(&resp->headers);
    free(resp);
    return NULL;
  }

  return resp;
}

int csrv_write_response(struct CsrvResponse *resp) {
  FILE *file = fdopen(resp->socket_handle, "w");
  if(file == NULL) {
    CSRV_LOG_ERROR(resp->csrv, "failed to open socket as file, errno=%s", strerror(errno));
    return -1;
  }
  
  char* body = csrv_str_vec_value(&resp->body);
  
  fprintf(file, "HTTP/1.1 %s\r\n", csrv_response_status_string(resp->status));
  fprintf(file, "Connection: Keep-Alive\r\n");
  fprintf(file, "Keep-Alive: timeout=5, max=999\r\n");
  // length technically includes the \0 character, so subtract 1
  fprintf(file, "Content-Length: %zu\r\n", resp->body.length - 1);
  fprintf(file, "Content-Type: text/plain\r\n");
  fprintf(file, "\r\n");
  fprintf(file, "%s", body);

  fflush(file);
  return 0;
}

void csrv_cleanup_response(struct CsrvResponse *resp) {
  csrv_str_map_cleanup(&resp->headers);
  free(resp->body.string);
  free(resp);
}
