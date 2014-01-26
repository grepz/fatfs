PRODUCT = test_fatfs

CC = gcc
LD = ld

DEFS = -DDEBUG

CFLAGS = -O0
CFLAGS += -Wall -Wextra -Warray-bounds

SRCS = main.c fatfs.c

all: $(PRODUCT)

$(PRODUCT): $(SRCS)
	$(CC) $(DEFS) $(CFLAGS) $^ -o $@

clean:
	rm -f *.o $(PRODUCT)
