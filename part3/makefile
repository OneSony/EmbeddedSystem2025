CC = gcc
LDFLAGS = -lasound -lpthread -lm
SRCS = main.c play.c control.c ui.c global.c equalizer.c
TARGET = music_app

all:
	$(CC) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)