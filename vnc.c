#include "uv.h"
#include "vnc.h"
#include "log.h"
#include <assert.h>

static void connect_cb(uv_connect_t* req, int status)
{
  Debug("connect callback\n");
}

void vnc_dowork(void* arg) {
  vncaddr* addr = (vncaddr*)arg;
  uv_loop_t* loop;
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
