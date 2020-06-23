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
#include "poll.h"
#include "csrv.h"

// 1. open socket
// 2. bind socket to address
// 3. listen() to set backlog and open connection
// 4. accept() to handle incoming connections
void csrv_listen(struct Csrv *csrv) {
  CSRV_LOG_INFO(csrv, "enter csrv_listen()");
  
  csrv->socket_handle = socket(AF_INET, SOCK_STREAM, 0);
  if (csrv->socket_handle == -1) {
    CSRV_LOG_ERROR(csrv, "socket() failed with errno=%s", strerror(errno));
    csrv->status = CSRV_BIND_FAILURE;
    return;
  }

  fcntl(csrv->socket_handle, F_SETFL, O_NONBLOCK);
  CSRV_LOG_INFO(csrv, "socket() returned handle %d", csrv->socket_handle);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(csrv->port);

  // TODO: allow binding on not-the host
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int bind_result = bind(csrv->socket_handle, (const struct sockaddr *) &addr, sizeof(addr));
  if (bind_result == -1) {
    CSRV_LOG_ERROR(csrv, "bind() failed with errno=%s", strerror(errno));
    csrv->status = CSRV_BIND_FAILURE;
    return;
  }

  CSRV_LOG_INFO(csrv, "bind() successful");

  int listen_result = listen(csrv->socket_handle, CSRV_LISTEN_BACKLOG);
  if(listen_result == -1) {
    CSRV_LOG_ERROR(csrv, "listen() failed with errno=%s", strerror(errno));
    csrv->status = CSRV_LISTEN_FAILURE;
    return;
  }
  
  CSRV_LOG_INFO(csrv, "listen() successful, starting poll()");

  struct pollfd pfd;
  pfd.fd = csrv->socket_handle;
  pfd.events = POLLIN;

  csrv->status = CSRV_OK;
  for(;;) {
    int poll_res = poll(&pfd, 1, 1000);
    if(poll_res == 0) {
      continue;
    } else if(poll_res == -1) {
      CSRV_LOG_ERROR(csrv, "poll() failed with errno=%s", strerror(errno));
      continue;
    }

    csrv_accept_handler(csrv);
  }
}

void csrv_accept_handler(struct Csrv *csrv) {
  CSRV_LOG_INFO(csrv, "enter csrv_accept_handler()");
  struct sockaddr_in addr;
  socklen_t addr_sz;
  int new_sock_handle = accept(csrv->socket_handle, (struct sockaddr *) &addr, &addr_sz);
  if(new_sock_handle < 0) {
    csrv->status = CSRV_ACCEPT_FAILURE;
    CSRV_LOG_ERROR(csrv, "accept() failed with errno=%s", strerror(errno));
    return;
  }
  CSRV_LOG_INFO(csrv, "accept() successful with socket handle %d", new_sock_handle);

  switch(csrv->model) {
  case CSRV_FORK:
    csrv_accept_fork(csrv, new_sock_handle);
    return;
  case CSRV_THREAD:
    csrv_accept_thread(csrv, new_sock_handle);
    return;
  case CSRV_EVENT:
  default:
    fprintf(csrv->log, "Unknown model type %d during accept()\n", csrv->model);
    close(new_sock_handle);
    break;
  }
}

void csrv_accept_fork(struct Csrv *csrv, int sock_handle) {
  CSRV_LOG_INFO(csrv, "enter csrv_accept_fork()");
  
  // We don't want to wait() for child processes to finish
  // If we don't handle this signal, we either have to wait for the
  // process to finish (blocking) or ignore it (creates process zombies)
  signal(SIGCHLD, SIG_IGN);
  int pid = fork();
  if(pid == -1) {
    CSRV_LOG_ERROR(csrv, "fork() failed with errno=%s", strerror(errno));
    close(sock_handle);
    return;
  }

  if(pid != 0) {
    CSRV_LOG_INFO(csrv, "fork() child pid=%d", pid);
    close(sock_handle);
    return;
  }

  struct CsrvRequest *req = csrv_alloc_request(csrv, sock_handle);
  if(req == NULL) {
    CSRV_LOG_ERROR(csrv, "csrv_alloc_request() failed! Aborting.");
    close(sock_handle);
    _exit(1);
  }

  csrv_parse_headers(req);
  if(req->status == CSRV_OK) {
    CSRV_LOG_INFO(csrv, "%s %s", req->headers.method, req->headers.uri);
    CSRV_LOG_INFO(csrv, "Size: %zu", req->headers.content_size);
    struct CsrvResponse *resp = csrv_init_response(req);
    if(resp == NULL) {
      CSRV_LOG_ERROR(csrv, "failed to create response");
      _exit(1);
    }
    
    //TODO: Remove this test code
    char *text = "Hello, world!";
    if(csrv_str_vec_pushn(&resp->body, text, strlen(text)) != 0) {
      CSRV_LOG_ERROR(resp->csrv, "failed to write body, errno=%s", strerror(errno));
    }

    if(csrv_write_response(resp) != 0) {
      CSRV_LOG_ERROR(csrv, "failed to write response, errno=%s", strerror(errno));
    }
    csrv_cleanup_response(resp);
  } else {
    CSRV_LOG_ERROR(csrv, "request failed with status=%d", req->status);
  }

  close(sock_handle);
  csrv_cleanup_request(req);
  CSRV_LOG_INFO(csrv, "ending forked process");
  _exit(0);
}

void csrv_accept_thread(struct Csrv *csrv, int sock_handle) {
  close(sock_handle);
}

