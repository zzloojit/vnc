#ifndef VNC_H
#define VNC_H
#include "uv.h"
typedef struct {
  char* hostname;
  short port;
}vncaddr;

void vnc_start(void* arg);
void vnc_stop();
#endif
