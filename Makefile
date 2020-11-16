.PHONY:		all clean spotless

CFLAGS = -Wall -Wextra -Wshadow -Wmissing-prototypes -Wmissing-declarations
LDLIBS = -lgpiod

all:		az5

az5:		az5.c
		$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:

spotless:	clean
		rm -f az5
