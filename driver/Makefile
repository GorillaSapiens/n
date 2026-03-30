CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2

all: n65driver

n65driver: n65driver.c
	$(CC) $(CFLAGS) n65driver.c -o n65driver

clean:
	rm -f n65driver
