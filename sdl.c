#include <SDL.h>
#include "vnc.h"
#include <assert.h>
#include <errno.h>

// copy from qemu
static gui_grab = 1;
static unsigned char buttonmask;
static unsigned short lastx;
static unsigned short lasty;
surface_type surface;
static char name[256];

static sdl_grab_start(void)
{
  
  if (!(SDL_GetAppState() & SDL_APPINPUTFOCUS)) {
    return;
  }
  
  SDL_WM_GrabInput (SDL_GRAB_ON);
  gui_grab = 1;
}

static sdl_grab_end(void)
{
  SDL_WM_GrabInput (SDL_GRAB_OFF);
  gui_grab = 0;
  
}

static void handle_mousemotion (SDL_Event *ev)
{
  //fixed me
  int max_x = surface.width , max_y = surface.height;
  
  if (gui_grab && (ev->motion.x == 0 || ev->motion.y == 0 ||
		   ev->motion.x == max_x || ev->motion.y == max_y)) {
    sdl_grab_end();
  }
  if (!gui_grab && 
      (ev->motion.x > 0 && ev->motion.x < max_x &&
       ev->motion.y > 0 && ev->motion.y < max_y)) {
    sdl_grab_start();
  }
  unsigned short dx = ev->motion.x - lastx;
  unsigned short dy = ev->motion.y - lasty;
  lastx = ev->motion.x;
  lasty = ev->motion.y;
  push_pointer_event (buttonmask, dx, dy);
  fprintf(stdout, "sdl mouse motion x:%d y:%d\n", ev->motion.x,
	  ev->motion.y);		    
}

static void handle_mousebutton(SDL_Event *ev)
{
  int buttonstate = SDL_GetMouseState (NULL, NULL);
  SDL_MouseButtonEvent* bev;
  
  bev = &ev->button;
  
  if (ev->type == SDL_MOUSEBUTTONDOWN) {
    buttonstate |= SDL_BUTTON(bev->button);
  }else{
    buttonstate &= ~SDL_BUTTON(bev->button);
  }

  if (buttonstate & SDL_BUTTON (SDL_BUTTON_LEFT)) {
    buttonmask |= 0x01 ;
  }else{
    buttonmask &= ~0x01 ;
  }

  if (buttonstate & SDL_BUTTON (SDL_BUTTON_MIDDLE)) {
    buttonmask |= 0x02;
  }else {
    buttonmask &= ~0x02;
  }
  if (buttonstate & SDL_BUTTON (SDL_BUTTON_RIGHT)) {
    buttonmask |= 0x04;
  }else {
    buttonmask &= ~ 0x04;
  }
  fprintf(stdout, "buttonmask is %d\n", buttonmask);
  push_pointer_event (buttonmask , lastx, lasty);
}


int main(int argc, char** argv)
{
  SDL_Init(SDL_INIT_VIDEO);
  uv_thread_t tid;
  int running = 1;  
  int channels[2];
  int r = 0;

  vnc_args args;

  if (argc != 3 ) {
    fprintf(stdout, "%s ip port\n", argv[0]);
  }
  
  if (socketpair (AF_LOCAL, SOCK_STREAM, 0, channels) == -1) {
    fprintf (stderr, "socketpair failed %s\n", strerror(errno));
  }
  args.send = channels[0];
  args.hostname = argv[1];
  args.port = atol(argv[2]);

  vnc_start((void*)&args);

  int ret = read(channels[1], &surface, sizeof (surface));
  assert (ret == sizeof(surface));
  
  int flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
  /* SDL_Surface* screen = SDL_SetVideoMode(800, 600,  */
  /* 					 32, */
  /* 					 flags); */

  SDL_Surface* screen = SDL_SetVideoMode(surface.width, surface.height,
   					 surface.bpp,
   					 flags); 

  ret = read(channels[1], name, surface.namelen);
  assert (ret == surface.namelen);

  SDL_WM_SetCaption(name, 0);

  ret = write (channels[1], &screen, sizeof(screen));
  assert (ret == sizeof(screen));

  int i = 0, j = 0;
  SDL_Event event, *ev = &event;  

  SDL_WM_GrabInput (SDL_GRAB_ON);

  while (running) {
    if (SDL_PollEvent(ev))
      {
	switch (ev->type) {
	case SDL_QUIT:
	    running = 0;
	    break;
	case SDL_MOUSEMOTION:
	  handle_mousemotion(ev);
	  break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	  handle_mousebutton(ev);
	  break;
	}
      }
  }
  
  SDL_Quit();
  return 0;
}
