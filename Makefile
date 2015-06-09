CC=gcc
LDFLAGS=-lzip -lz -lplist
TARGET=extract
all:$(TARGET).c
	$(CC) -o $(TARGET) $(TARGET).c $(LDFLAGS)
