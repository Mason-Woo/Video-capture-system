BIN = wcamsrv
INC = -Iinclude/  
SRC = $(wildcard *.c)
OBJS = $(patsubst %.c, %.o,$(SRC))

CC = gcc
CFLAGS = $(INC) -g

$(BIN):$(OBJS)
	$(CC) -o  $@ $^  -g  -lavformat -lavdevice -lavcodec 

clean:
	rm $(OBJS) $(BIN)