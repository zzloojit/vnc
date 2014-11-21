#include "uv.h"
#include "vnc.h"
#include "log.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

typedef struct {
  unsigned char bpp;
  unsigned char depth;
  unsigned char bigendian_flag;
  unsigned char truecolor_flag;
  unsigned short red_max;
  unsigned short green_max;
  unsigned short blue_max;
  unsigned char red_shift;
  unsigned char green_shift;
  unsigned char blue_shift;
  unsigned char padding[3];
}pixel_format;

typedef struct {
  unsigned short width;
  unsigned short height;
  pixel_format   pixformat;
  char name[256];
}vnc_context;

static  vnc_context context;
static  uv_thread_t tid;
static  uv_loop_t* loop;
static  uv_stream_t* stream;
static  void (*handle)(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf);

static void write_cb (uv_write_t* req, int status)
{
  write_req_t* wr = (write_req_t*)req;
  Debug("write callback\n");
  free(wr->buf.base);
  free(wr);
}

static void handle_read(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
  Debug("handle read %d\n", nread);
  handle(tcp, nread, buf);
  free (buf.base);
}

static void handle_ServerInit(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
  assert (nread >= 24);
  context.width = ntohs (*(unsigned int *)buf.base);
  context.height = ntohs(*(unsigned int *)&buf.base[2]);
  memcpy((char*)&context.pixformat, &buf.base[4], 16);
  context.pixformat.red_max = ntohs(context.pixformat.red_max);
  context.pixformat.green_max = ntohs(context.pixformat.green_max);
  context.pixformat.blue_max = ntohs(context.pixformat.blue_max);
  Debug ("width = %u, height = %u bpp = %u\n"
	 "bigendian_flag = %u truecolor_flag = %u\n"
	 "red_max = %u green_max = %u blue_max = %u\n", context.width,
	 context.height, context.pixformat.bpp, context.pixformat.bigendian_flag,
	 context.pixformat.truecolor_flag, context.pixformat.red_max, context.pixformat.green_max,
	 context.pixformat.blue_max);
  unsigned int namelength = ntohl(*(unsigned int*)&buf.base[20]);
  assert (namelength <= 255);
  strncpy(context.name, (char*)&buf.base[24], 256);
  Debug("name is %s\n", context.name);
}

static void handle_ClientInit(uv_stream_t* tcp)
{
  int r = 0;

  write_req_t* wr = (write_req_t *)malloc(sizeof(write_req_t));
  char* init = malloc(1);
  *init = '\0';
  wr->buf = uv_buf_init(init, 1);
  r = uv_write(&wr->req, tcp, &wr->buf, 1, write_cb);
  assert (r == 0);
  handle = handle_ServerInit;
}

static void handle_auth(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
  Debug("%s\n", __FUNCTION__);
  unsigned int auth_method;
  assert(nread == 4);
  //big endian 
  //auth_method = (buf.base[0] << 24) | (buf.base[1] << 16) | 
  //    (buf.base[2] << 8) | (buf.base[3] );
  auth_method = ntohl(*(unsigned int *)buf.base);
  Debug("auth method %u\n", auth_method);
  // None auth
  assert (auth_method == 1);
  if (auth_method == 1)  {
    handle_ClientInit (tcp);
  }
}

static void handle_init(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
  int r = 0;
  write_req_t* wr;
  assert (tcp != NULL);
  assert (nread == 12);
  Debug("%s\n", __FUNCTION__);
  assert (buf.base[0] == 'R' && buf.base[1] == 'F' && buf.base[2] == 'B');
  assert (buf.base[11] = '\n');
  assert (buf.base[7] == '.');
  
  Debug("Server version is %s", &buf.base[4]);

  wr = malloc(sizeof(write_req_t));
  char* base = malloc(12);
  memcpy(base, "RFB 003.003\n", 12);
  wr->buf = uv_buf_init(base, 12);
  r = uv_write(&wr->req, tcp, &wr->buf, 1, write_cb);

  handle = handle_auth;
}


static void close_cb (uv_handle_t* handle) 
{
  Debug("close callback\n");
}

static uv_buf_t alloc_cb (uv_handle_t* handle, size_t suggested_size)
{
  char* buf = malloc(suggested_size);
  assert(buf);
  Debug ("alloc cb size is %d\n", suggested_size);
  return uv_buf_init(buf, suggested_size);
}

static void connect_cb (uv_connect_t* req, int status)
{
  int r = 0;
  Debug("connect callback\n");
  assert (status == 0);
  stream = req->handle;
  
  handle = handle_init;
  r = uv_read_start (stream, alloc_cb, handle_read);
  assert (r == 0);
}

static void vnc_dowork(void* arg) {
  vncaddr* addr = (vncaddr*)arg;
  uv_tcp_t conn;
  int r = 0;
  static uv_connect_t connect_req;
  struct sockaddr_in saddr = uv_ip4_addr(addr->hostname, addr->port);
  loop = uv_loop_new();

  r = uv_tcp_init(loop, &conn);
  assert (r == 0);
  
  r = uv_tcp_connect(&connect_req, &conn, saddr, connect_cb);
  assert (r == 0);
  
  uv_run(loop, UV_RUN_DEFAULT);
  
  uv_loop_delete(uv_default_loop());
  return;
}

void vnc_start(void* arg)
{
  int r = uv_thread_create(&tid, vnc_dowork, arg);
  assert (r == 0);
}

//fixed me
void vnc_stop()
{
  uv_stop (loop);
  if (stream != NULL) {
    uv_unref((uv_handle_t*)stream);
    stream = NULL;
  }
  uv_thread_join (&tid);
}
