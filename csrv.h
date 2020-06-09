#ifndef CSRV_H
#define CSRV_H

#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"

enum CsrvModel {
  CSRV_FORK,
  CSRV_THREAD,
  CSRV_EVENT
};

// Status codes specific to Csrv
enum CsrvStatus {
  CSRV_OK,
  CSRV_BIND_FAILURE,
  CSRV_LISTEN_FAILURE,
  CSRV_ACCEPT_FAILURE,
  CSRV_HEADER_PARSE_FAILURE,
  CSRV_ALLOC_FAILURE
};

// Hashmap of string->string
struct CsrvStrMap {
  size_t n_items;
  size_t n_collisions;
  size_t size;
  char **hashmap;
};

// String vector
struct CsrvStrVec {
  size_t length;
  size_t buff_sz;
  size_t realloc_count;
  char *string;
};

// Core server -- entrypoint into library
struct Csrv {
  enum CsrvModel model;
  enum CsrvStatus status;
  int socket_handle;
  uint16_t port;
  unsigned int num_requests;
  FILE *log;
  size_t active_requests;
  size_t request_id_max;
};

#define CSRV_LISTEN_BACKLOG 20

// Connection handling
void csrv_listen(struct Csrv *csrv);
void csrv_accept_handler(struct Csrv *csrv);
void csrv_accept_fork(struct Csrv *csrv, int sock_handle);
void csrv_accept_thread(struct Csrv *csrv, int sock_handle);

// Handler entrypoints and structures
struct CsrvRequestHeader {
  unsigned int status_code;
  size_t content_size;

  char *host;
  char *uri;
  char *path;

  struct CsrvStrMap header_map;
};

struct CsrvRequest {
  int socket_handle;
  size_t id;
  enum CsrvStatus status;
  
  struct CsrvRequestHeader headers;
  struct CsrvStrVec request;
  struct Csrv *csrv;
};

#define CSRV_CHUNK_SIZE (512 * sizeof(char))
struct CsrvRequest *csrv_alloc_request(struct Csrv *csrv, int new_socket_handle);
void csrv_cleanup_request(struct CsrvRequest *req);
void csrv_parse_headers(struct CsrvRequest *req);

// String handling
#define CSRV_STR_VEC_SIZE 32
int csrv_str_vec_init(struct CsrvStrVec *vec);
int csrv_str_vec_pushc(struct CsrvStrVec *vec, char c);
char *csrv_str_vec_value(struct CsrvStrVec *vec);
int csrv_str_vec_pushn(struct CsrvStrVec *vec, char* buffer, size_t sz);

// String:String map
#define CSRV_DEFAULT_MAP_SIZE 256
int csrv_str_map_init(struct CsrvStrMap *map);
void csrv_str_map_cleanup(struct CsrvStrMap *map);
size_t csrv_djb2_hash(char *str);
void csrv_str_map_add(struct CsrvStrMap *map, char *key, char *value);

// Logging
#define CSRV_LOG_INFO(csrv, fmt, ...) \
  fprintf((csrv)->log, "[INFO ] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define CSRV_LOG_ERROR(csrv, fmt, ...) \
  fprintf((csrv)->log, "[ERROR] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#endif
