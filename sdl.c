#include "vnc.h"
#include "log.h"

#include <SDL.h>
#include <assert.h>
#include <errno.h>


// copy from qemu
static gui_grab = 1;
static unsigned char buttonmask;
static unsigned short lastx;
static unsigned short lasty;
surface_type surface;
static char name[256];
static int fd_pipe;

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

void handle_grab(SDL_Event *ev)
{
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

}
void handle_mousemotion (SDL_Event *ev)
{
  //fixed me
  unsigned short dx = ev->motion.x - lastx;
  unsigned short dy = ev->motion.y - lasty;
  lastx = ev->motion.x;
  lasty = ev->motion.y;
  push_pointer_event (buttonmask, dx, dy);
}

void handle_mousebutton(SDL_Event *ev)
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

void handle_keydown (SDL_Event* ev) 
{
  SDL_KeyboardEvent* key = &ev->key;
  Debug( "key name is %s\n", SDL_GetKeyName(key->keysym.sym));
  push_key_event(1, key->keysym.sym);
}

void handle_keyup (SDL_Event* ev)
{
  SDL_KeyboardEvent* ke = &ev->key;
  push_key_event(0, ke->keysym.sym);
}

static void poll_event(void* arg)
{
  int running = 1;  
  int i = 0, j = 0;
  int r = 0;
  SDL_Event event, *ev = &event;  

  //SDL_EnableKeyRepeat (250, 100);
  SDL_WM_GrabInput (SDL_GRAB_ON);

  while (running) {
    if (SDL_PollEvent(ev))
      {

	switch (ev->type) {
	case SDL_QUIT:
	    running = 0;
	    break;
	case SDL_MOUSEMOTION:
	  //	  handle_mousemotion(ev);
	  handle_grab(ev);
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	  //handle_mousebutton(ev);
	case SDL_KEYDOWN: 
	case SDL_KEYUP:
	  r = write (fd_pipe, &ev, sizeof (ev));
	  assert (r == sizeof(ev));
	  read (fd_pipe, &r, sizeof (r));
	  break;
	default:
	  ;
	}
      }
  }
  
  SDL_Quit();
  
}

int main(int argc, char** argv)
{
  SDL_Init(SDL_INIT_VIDEO);
  uv_thread_t tid;
  int k = 0;
  
  int channels[2];

  int r = 0;

  vnc_args args;

  if (argc != 3 ) {
    fprintf(stdout, "%s ip port\n", argv[0]);
    return ;
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

  fd_pipe = channels[1];
  poll_event(NULL);
  
  
  /* SDL_Rect rect; */
  /* while (r = read (channels[1], &rect, sizeof(rect))) { */
  /*   printf("read r = %d\n", r); */
  /*   uv_mutex_lock(&mutex); */
  /*   SDL_UpdateRect(screen, rect.x, rect.y, rect.w , rect.h); */
  /*   uv_mutex_unlock(&mutex); */
  /* } */
  return 0;
}
