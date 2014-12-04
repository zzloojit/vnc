#ifndef VNC_H
#define VNC_H
#include "uv.h"
#include <SDL.h>

typedef struct {
  char* hostname;
  short port;
  int   send;
}vnc_args;

typedef struct {
  unsigned short width;
  unsigned short height;
  unsigned int   bpp;
  unsigned int namelen;
}surface_type;

void vnc_start(void* arg);
void push_pointer_event (unsigned char bmask,
			 unsigned short xpos,
			 unsigned short ypos);

void push_key_event (unsigned char down_flag,
		     unsigned int  key);

void vnc_stop();
#endif
