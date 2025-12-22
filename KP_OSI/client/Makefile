CC = gcc
CFLAGS = -Wall -Wextra -pthread -D_DEFAULT_SOURCE
TARGET = sea_battle_client
SOURCES = client.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -I../shared -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) *.o

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run