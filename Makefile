INC := /home/suncloud/source/libuv/include
LIBS :=/home/suncloud/source/libuv
vnc:sdl.c vnc.c
	gcc sdl.c vnc.c -o vnc `pkg-config --libs --cflags sdl` -I$(INC) -L$(LIBS) -luv  -g

clean:
	rm vnc