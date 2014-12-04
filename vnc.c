#include "uv.h"
#include "vnc.h"
#include "log.h"
#include "bitmap.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <SDL.h>


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
  SDL_Surface* screen;
  unsigned short width;
  unsigned short height;
  pixel_format   pixformat;
  char name[256];
  unsigned int init;
  int          pipe;
}vnc_context;

enum Encoding {
  RAW = 0,
  COPY_RECTANGLE=1,
  RPE = 2,
  CORRE = 4,
  HEXTILE = 5,
  ZLIB = 6,
  TIGHT = 7,
  ZLIBHEX = 8,
  ZRLE = 16,
  PSEUDO_CURSOR = -23,
};

struct {
  unsigned short nrect;
  unsigned char buff[2560 * 2560 * 4];
  unsigned char* curr;
  unsigned short xpos;
  unsigned short ypos;
  unsigned short width;
  unsigned short height;
  unsigned int   nneed;
  unsigned int   bpp;
  unsigned int   encoding;
}update;

typedef struct {
  unsigned short xpos;
  unsigned short ypos;
  unsigned short width;
  unsigned short height;
  int encoding_type;
}*update_prect, update_rect;
  

typedef struct {
  uv_stream_t* handle;
  unsigned int nread;
  unsigned int nexcept;
#define BUFFSIZE 2560 * 2560 *4
  unsigned char buff[BUFFSIZE];
}except_rd_t;

static  vnc_context context;
static  uv_thread_t tid;
static  uv_loop_t* loop;
static  except_rd_t stream;
uv_timer_t update_req;

// define in sdl.c
extern void handle_mousebutton(SDL_Event *ev);
extern void handle_mousemotion (SDL_Event *ev);

static  void (*handle)(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf);
static void handle_incoming (uv_stream_t *tcp, ssize_t nread, uv_buf_t buf);
static void handle_conn(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf);
static void write_cb (uv_write_t* req, int status);
static void handle_rect_entry(uv_stream_t *tcp, ssize_t nread, uv_buf_t buf);
static void framebuff_updatereq(unsigned short x, unsigned short y, 
				unsigned short w, unsigned short h, 
				unsigned char incre);

void push_pointer_event (unsigned char bmask,
			     unsigned short xpos,
			     unsigned short ypos)
{

  typedef struct  {
    unsigned char message_type;
    unsigned char button_mask;
    unsigned short xpos;
    unsigned short ypos;
  }*ppointer_event ;

  int r = 0;
  write_req_t* wr = (write_req_t *)malloc(sizeof(write_req_t));
  ppointer_event  p_event= malloc(sizeof(*p_event));
  p_event->message_type = 5;
  p_event->button_mask = bmask;
  p_event->xpos = htons(xpos);
  p_event->ypos = htons(ypos);
  wr->buf = uv_buf_init((void*)p_event, sizeof(*p_event));

  r = uv_write(&wr->req, stream.handle, &wr->buf, 1, write_cb);
}

void push_key_event (unsigned char down_flag,
		     unsigned int  key)
{
  typedef struct {
    unsigned char message_type;
    unsigned char down_flag;
    unsigned short padding;
    unsigned int   key;
  }key_event;

  int r = 0;
  write_req_t* wr = (write_req_t *)malloc(sizeof(write_req_t));
  key_event*  p_event= malloc(sizeof(key_event));
  p_event->message_type = 4;
  p_event->down_flag = down_flag;
  p_event->padding = 0;
  p_event->key = htonl(key);
  wr->buf = uv_buf_init ((void*)p_event, sizeof (*p_event));

  r = uv_write (&wr->req, stream.handle, &wr->buf, 1, write_cb);

}

static void write_cb (uv_write_t* req, int status)
{
  write_req_t* wr = (write_req_t*)req;
  free(wr->buf.base);
  free(wr);
}

static void handle_sdlev (uv_stream_t* pipe, ssize_t nread, uv_buf_t buf)
{
  SDL_Event *ev;

  assert (nread = sizeof (ev));
  ev = *(SDL_Event**)buf.base;
  switch (ev->type) {
  case SDL_MOUSEMOTION:
      handle_mousemotion(ev);
      break;
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
    handle_mousebutton(ev);
    break;
  case SDL_KEYDOWN:
    handle_keydown(ev);
    break;
  case SDL_KEYUP:
    handle_keyup(ev);
    break;
  default:
    break;
  }
  int r = 0;
  write (context.pipe, &r, sizeof (r));
}

static uv_buf_t sdl_alloc(uv_handle_t* handle, size_t suggested_size) 
{
  // used size 2 * SDL_Event avoid bug
  static SDL_Event* ev[2];
  return uv_buf_init ((char*)&ev, sizeof(ev));
}

// handle input from mouse and keyboard 
static void handle_input (void)
{
  static uv_tcp_t pipe;
  int r = 0;
  int flags = fcntl(context.pipe, F_GETFL, 0);
  assert(flags >= 0);
  r = fcntl(context.pipe, F_SETFL, flags | O_NONBLOCK);
  assert(r >= 0);

  r = uv_tcp_init (loop, &pipe);

  r = uv_tcp_open (&pipe, context.pipe);
  uv_read_start((uv_stream_t*)&pipe, sdl_alloc, handle_sdlev);
}

static void framebuff_updatereq(unsigned short x, unsigned short y, 
				unsigned short w, unsigned short h, 
				unsigned char incre)
{
  int r = 0;
  typedef struct {
    unsigned char type;
    unsigned char incre;
    unsigned short xpos;
    unsigned short ypos;
    unsigned short width;
    unsigned short height;
  }*framebuff_req;
  write_req_t* wr = (write_req_t *)malloc(sizeof(write_req_t));
  framebuff_req req_buf = malloc(sizeof (*req_buf));
  req_buf->type = 3;
  req_buf->incre = incre;
  req_buf->xpos = htons(x);
  req_buf->ypos = htons(y);
  req_buf->width = htons(w);
  req_buf->height = htons(h);
  wr->buf = uv_buf_init((char*)req_buf, sizeof(*req_buf));

  r = uv_write(&wr->req, stream.handle, &wr->buf, 1, write_cb);
  
  assert (r == 0);
}

static void handle_raw(ssize_t nread, uv_buf_t buf, int offset) 
{
  
}

static void handle_updaterect()
{
  typedef struct {
    unsigned short xpos;
    unsigned short ypos;
    unsigned short width;
    unsigned short height;
    int encoding_type;
  }*update_prect, update_rect;

  if (update.curr - update.buff >= sizeof(update_rect)) {
    update_prect prect = (update_prect)(update.buff);
    update_rect  rect;
    rect.xpos = ntohs(prect->xpos);
    rect.ypos = ntohs(prect->ypos);
    rect.width = ntohs(prect->width);
    rect.height = ntohs(prect->height);
    rect.encoding_type = ntohl(prect->encoding_type);

    if (update.curr - update.buff >= sizeof(rect) + rect.width * rect.height * 4) {
      //memcpy (context.screen->pixels, update.buff + sizeof (rect), rect.width * rect.height * 4);
      SDL_Surface *image = SDL_CreateRGBSurfaceFrom(
						    update.buff + sizeof(rect),
						    rect.width, rect.height,
						    32, rect.width * 4,
						    0, 0, 0, 255
						    );
      SDL_Rect rec;
      SDL_Rect srec;
      srec.x = 0, srec.y = 0, srec.w = rect.width, srec.h = rect.height;
      rec.x = rect.xpos, rec.y = rect.ypos, rec.w = rect.width , rec.h = rect.height;
      
      SDL_BlitSurface(image, &srec, context.screen, &rec);
      SDL_UpdateRect(context.screen, rect.xpos, rect.ypos, rect.width, rect.height);
      update.nrect--;
      int len = sizeof(rect) + rect.width * rect.height * 4;

      if (rect.encoding_type == RAW) {
	Debug("update rect format is RAW\n"
	      "xpos is %d ypos is %d\n"
	      "width is %d height is %d\n",
	      rect.xpos, rect.ypos,
	      rect.width, rect.height);
      }

	handle = handle_conn;
    }
  }
}

static void handle_rect (uv_stream_t *tcp, ssize_t nread, uv_buf_t buf)
{
  assert (tcp == stream.handle);
  unsigned char bpp = context.pixformat.bpp;
  unsigned int  linesize = update.width * bpp / 8;
  assert (nread == update.width * update.height * bpp / 8);
  SDL_Surface *image = SDL_CreateRGBSurfaceFrom(
						stream.buff,
						update.width, update.height,
						bpp, linesize,
						0, 0, 0, 255
						);
	  

  SDL_Rect rec;
  SDL_Rect srec;
  srec.x = 0, srec.y = 0, srec.w = update.width, srec.h = update.height;
  rec.x = update.xpos, rec.y = update.ypos;
  rec.w = update.width , rec.h = update.height;
      
  SDL_BlitSurface(image, &srec, context.screen, &rec);
  SDL_UpdateRect(context.screen, update.xpos, update.ypos, 
		 update.width, update.height);
  update.nrect--;
  
  if (update.nrect == 0) {
    handle = handle_conn;
    stream.nread = 0;
    stream.nexcept= 1;
  }else {
    handle = handle_rect_entry;
    stream.nread = 0;
    stream.nexcept = sizeof(update_rect);
  }
}

static void handle_incoming (uv_stream_t *tcp, ssize_t nread, uv_buf_t buf)
{
  memcpy(update.curr, buf.base, nread);
  update.curr += nread;
  handle_updaterect();
}

static void handle_rect_entry(uv_stream_t *tcp, ssize_t nread, uv_buf_t buf)
{
  assert (nread == sizeof (update_rect));
  unsigned char bpp = context.pixformat.bpp;
  update_prect prect = (update_prect)(buf.base);
  update.xpos = ntohs(prect->xpos);
  update.ypos = ntohs(prect->ypos);
  update.width = ntohs(prect->width);
  update.height = ntohs(prect->height);
  update.encoding = ntohl(prect->encoding_type);
  //Debug ("rect %d %d %d %d\n", update.xpos, update.ypos, update.width, update.height);
  assert (tcp == stream.handle);
  stream.nread = 0;
  stream.nexcept = update.width * update.height * context.pixformat.bpp / 8;
  handle = handle_rect;
}

static void handle_framebuffupdate(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
#pragma pack(push)
#pragma pack(1)
  
  typedef struct {
    unsigned char padding;
    unsigned short nrectangles;
  }*update_head;
#pragma pack(pop)  

  assert (nread >= sizeof(*(update_head)0));
  //unsigned short rectangles = (unsigned short *)&buf.base[2];
  update_head head = (update_head)&buf.base[0];
  
  update.nrect = ntohs(head->nrectangles);
  //Debug ("begin update nrectangles is %d \n", update.nrect);

  if (update.nrect) {
    handle = handle_rect_entry;
    stream.nread = 0;
    stream.nexcept = sizeof(update_rect);
  }else {
    handle = handle_conn;
  }
}

static void handle_conn(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
  assert (nread >= 1);
  unsigned char msgid = (unsigned char )buf.base[0];
  //Debug("%s nread %d msgid is %d\n", __FUNCTION__, nread, msgid);

  assert (tcp == stream.handle);
  // framebufferupdate 
  if (msgid == 0) {
    stream.nread = 0;
    stream.nexcept = 3;
    handle = handle_framebuffupdate;
  }else{
    assert (0);
  }
}

static void handle_read(uv_stream_t* tcp, ssize_t nread, uv_buf_t buf)
{
  //  Debug("handle read %d\n", nread);
  assert (tcp == stream.handle);
  assert (nread >= 0);
  stream.nread += nread;
  if (stream.nread < stream.nexcept)
    return;
  assert (stream.nread == stream.nexcept);
  
  uv_buf_t buff = uv_buf_init (stream.buff, BUFFSIZE);
  handle(tcp, stream.nread, buff);
  //free (buf.base);
}

static void update_cb (uv_timer_t* handle, int status)
{

  framebuff_updatereq (0, 0, context.width , context.height, 1);
}

static void handle_name(uv_stream_t *tcp, ssize_t nread, uv_buf_t buf)
{
  assert (nread <= 256);
  assert (tcp == stream.handle);
  strncpy (context.name, buf.base, stream.nread);
  
  stream.nread = 0;
  stream.nexcept = 1;
  handle = handle_conn;
  framebuff_updatereq (0, 0, context.width , context.height, 0);

  surface_type surface;
  surface.width = context.width;
  surface.height = context.height;
  surface.bpp = context.pixformat.bpp;
  surface.namelen = nread;
  
  int r = write (context.pipe, &surface, sizeof(surface));
  assert (r == sizeof (surface));

  r = write (context.pipe, context.name, nread + 1);
  assert (r == nread + 1);

  r = read (context.pipe, &context.screen, sizeof (context.screen));
  assert (r == sizeof (context.screen));

  uv_timer_start (&update_req, update_cb, 0, 30);
  handle_input();
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
  assert (tcp == stream.handle);
  stream.nread = 0;
  stream.nexcept = namelength;
  handle = handle_name;
  context.init = 1;

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
  assert (tcp == stream.handle);
  stream.nexcept = 24;
  stream.nread = 0;
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
  
  assert (tcp == stream.handle);
  stream.nexcept = 4;
  stream.nread = 0;
  handle = handle_auth;
}


static void close_cb (uv_handle_t* handle) 
{
  Debug("close callback\n");
}

/* static uv_buf_t alloc_cb (uv_handle_t* handle, size_t suggested_size) */
/* { */
/*   assert (handle == stream.handle); */
/*   char* buf = malloc (stream.nexcept); */
/*   //char* buf = malloc(suggested_size); */
/*   assert(buf); */
/*   //Debug ("alloc cb size is %d\n", suggested_size); */
/*   Debug ("alloc cb size is %d\n", stream.nexcept); */
/*   //return uv_buf_init(buf, suggested_size); */
/*   return uv_buf_init(buf, stream.nexcept); */
/* } */

static uv_buf_t alloc_cb (uv_handle_t* handle, size_t suggested_size)
{
  assert ((uv_stream_t*)handle == stream.handle);
  if (stream.nread == stream.nexcept) {
    assert (0);
  }
  unsigned char* start = stream.buff + stream.nread;
  assert (stream.nexcept < BUFFSIZE);
  return uv_buf_init (start, stream.nexcept - stream.nread);
}

static void connect_cb (uv_connect_t* req, int status)
{
  int r = 0;
  Debug("connect callback\n");
  assert (status == 0);
  stream.handle = req->handle;
  stream.nexcept = 12;
  stream.nread = 0;
  handle = handle_init;
  r = uv_read_start (stream.handle, alloc_cb, handle_read);
  assert (r == 0);
}

static void vnc_dowork(void* arg) {
  vnc_args* args = (vnc_args*)arg;
  uv_tcp_t conn;
  int r = 0;
  static uv_connect_t connect_req;


  struct sockaddr_in saddr = uv_ip4_addr(args->hostname, args->port);
  context.pipe = args->send;
  loop = uv_loop_new();

  r = uv_tcp_init(loop, &conn);
  assert (r == 0);

  uv_timer_init (loop, &update_req);
  
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
  if (stream.handle != NULL) {
    uv_unref((uv_handle_t*)stream.handle);
    stream.handle = NULL;
  }
  uv_thread_join (&tid);
}
