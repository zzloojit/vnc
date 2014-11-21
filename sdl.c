#include <SDL.h>
#include "vnc.h"
#include <assert.h>
int main(int argc, char** argv)
{
  SDL_Init(SDL_INIT_VIDEO);
  uv_thread_t tid;
  int running = 1;  
  int r = 0;
  SDL_Event event;
  vncaddr addr;
  addr.hostname = "10.3.3.58";
  addr.port = 7013;
  
  r = uv_thread_create(&tid, vnc_dowork, (void*)&addr);
  assert(r == 0);
  
  int flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
  SDL_Surface* screen = SDL_SetVideoMode(1024, 768, 32,
					 flags);

  SDL_WM_SetCaption("Hello one", 0);



  while (running) {
    if (SDL_PollEvent(&event))
      {
	if (event.type == SDL_QUIT)
	  {
	    running = 0;
	  }
      }
  }
  r = uv_thread_join(&tid);
  SDL_Quit();
  return 0;
}
