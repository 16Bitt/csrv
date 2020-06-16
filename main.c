#include "stdio.h"
#include "errno.h"
#include "string.h"
#include "csrv.h"

int main(int argc, char **argv) {
  struct Csrv srv;
  srv.model = CSRV_FORK;
  srv.status = CSRV_OK;
  srv.port = 2222;
  srv.num_requests = 0;
  srv.log = fopen("/dev/stdout", "w");

  if(srv.log == NULL) {
    printf("FAILED TO ACQUIRE STDOUT: %s\n", strerror(errno));
    exit(1);
  }

  csrv_listen(&srv);

  return 0;
}
