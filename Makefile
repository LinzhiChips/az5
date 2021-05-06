#
# Copyright (C) 2021 Linzhi Ltd.
#
# This work is licensed under the terms of the MIT License.
# A copy of the license can be found in the file COPYING.txt
#

CC = arm-linux-gcc

.PHONY:		all clean spotless

CFLAGS = -Wall -Wextra -Wshadow -Wmissing-prototypes -Wmissing-declarations
LDLIBS = -lgpiod

all:		az5

az5:		az5.c
		$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:

spotless:	clean
		rm -f az5
