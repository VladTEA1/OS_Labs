.PHONY: all server client clean run-server run-client

all: server client

server:
	@echo "Building server..."
	@cd server && make

client:
	@echo "Building client..."
	@cd client && make

clean:
	@echo "Cleaning..."
	@cd server && make clean
	@cd client && make clean
	@rm -f /tmp/sea_battle.mmap

run-server:
	@cd server && ./sea_battle_server

run-client:
	@cd client && ./sea_battle_client

help:
	@echo "Available commands:"
	@echo "  make all        - Build server and client"
	@echo "  make server     - Build only server"
	@echo "  make client     - Build only client"
	@echo "  make clean      - Clean everything"
	@echo "  make run-server - Run server"
	@echo "  make run-client - Run client"