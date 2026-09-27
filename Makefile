CC = gcc
CFLAGS = -Wall -Wextra -g
CPPFLAGS = -Irouter -IyoutubeApiSearch
LDLIBS = -lcurl -lcjson -lws2_32 -lbcrypt
TARGET = main.exe
SOURCES = $(wildcard *.c opensearchIngestion/*.c opensearchDelete/*.c router/*.c youtubeApiSearch/*.c)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)
