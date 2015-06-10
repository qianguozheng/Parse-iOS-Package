CC=gcc
LDFLAGS=-lzip -lz -lplist
TARGET=extract
BIN=ipainfo
all:$(TARGET).c
	$(CC) -o $(BIN) $(TARGET).c $(LDFLAGS)

clean:
	rm $(BIN)

install:
	mv $(BIN) ./bin
uninstall:
	rm ./bin/$(BIN)
