CC = gcc
CFLAGS = -Wall -Wextra -g
CPPFLAGS = -Irouter -Isearch
LDLIBS = -lcurl -lws2_32
TARGET = main.exe
SOURCES = $(wildcard *.c router/*.c search/*.c)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)
