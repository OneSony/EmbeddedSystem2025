CC = gcc
LDFLAGS = -lasound -lpthread -O3 -ffast-math
SRCS = main.c play.c control.c ui.c global.c log.c wsola.c
TARGET = music_app

all:
	$(CC) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)