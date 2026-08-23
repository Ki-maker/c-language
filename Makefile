CC = gcc
CFLAGS = -Wall -Wextra -g
LDLIBS = -lws2_32
TARGET = main.exe
SOURCES = $(wildcard *.c)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)
