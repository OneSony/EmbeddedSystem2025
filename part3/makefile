CC = gcc
LDFLAGS = -lasound -lpthread
SRCS = main.c play.c control.c ui.c global.c log.c
TARGET = music_app

all:
	$(CC) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)