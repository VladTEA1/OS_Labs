CC = gcc
CFLAGS = -Wall -Wextra -pthread -D_DEFAULT_SOURCE
TARGET = sea_battle_server
SOURCES = server.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -I../shared -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) *.o $(MMAP_FILE)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run