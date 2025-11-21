CC = gcc
CFLAGS = -Wall -g -Wextra -DNCURSES_WIDECHAR -std=c99
LDFLAGS = -lncursesw

TARGET = puyo.exe
SRC = puyo.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -o $@ $(CFLAGS) $^ $(LDFLAGS)
	
clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)