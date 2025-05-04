
TARGET = proj2

CC = gcc
CFLAGS = -std=gnu99 -Wall -Wextra -Werror -pedantic -pthread -lrt

all: $(TARGET)

$(TARGET): proj2.c
	$(CC) $(CFLAGS) -o $(TARGET) proj2.c