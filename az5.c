/*
 * az5.c - Monitor a GPIO and run shell commands if it changes
 *
 * Copyright (C) 2021 Linzhi Ltd.
 *
 * This work is licensed under the terms of the MIT License.
 * A copy of the license can be found in the file COPYING.txt
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include <gpiod.h>


#define	GPIO_CHIP	"gpiochip0"

#define	CONSUMER	"az5"

#define	MAX_WIDTH	80


struct gpio_map {
	unsigned n;
	const char *name;
};


static const struct gpio_map gpio_map_gen1[] = {
	{ 14,	"USER" },
	{ 89,	"SLOT_0_TWARN" },
	{ 90,	"SLOT_1_TWARN" },
	{ 91,	"SLOT_0_TSHUT" },
	{ 92,	"SLOT_1_TSHUT" },
	{ 93,	"SLOT_0_SALRT" },
	{ 94,	"SLOT_1_SALRT" },
	{ 0, NULL }
};

static const struct gpio_map gpio_map_gen2[] = {
	{ 31,	"USER" },
	{ 39,	"SLOT_0_TWARN" },
	{ 37,	"SLOT_1_TWARN" },
	{ 33,	"SLOT_0_TSHUT" },
	{ 38,	"SLOT_1_TSHUT" },
	{ 35,	"SLOT_0_SALRT" },
	{ 34,	"SLOT_1_SALRT" },
	{ 0, NULL }
};

static const struct gpio_map *gpio_map = gpio_map_gen1;
static unsigned *lines;
static unsigned n_lines;

static const char *while_on_command;
static const char *off_command;


static void setup_gpio(struct gpiod_line_bulk *bulk)
{
	struct gpiod_chip *chip;
	int res;

	chip = gpiod_chip_open_by_name(GPIO_CHIP);
	if (!chip) {
		fprintf(stderr, "gpiod_chip_open_by_name\n");
		exit(1);
	}
	res = gpiod_chip_get_lines(chip, lines, n_lines, bulk);
	if (res < 0) {
		fprintf(stderr, "gpiod_chip_get_lines: %d\n", res);
		exit(1);
	}
	if (gpiod_line_request_bulk_both_edges_events(bulk, CONSUMER) < 0) {
		fprintf(stderr, "gpiod_line_request_bulk_both_edges_events\n");
		exit(1);
	}
}


static unsigned gpio_read(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line *line;
	unsigned vec, i;
	int res;

	/*
	 * gpiod_line_get_value_bulk seems to be broken, so we get the values
	 * one by one.
	 */

	vec = 0;
	for (i = 0; i != n_lines; i++) {
		line = gpiod_line_bulk_get_line(bulk, i);
		res = gpiod_line_get_value(line);
		if (res < 0) {
			fprintf(stderr, "gpiod_line_get_value\n");
			exit(1);
		}
		vec |= res << i;
	}
	return vec;
}


static void update(struct gpiod_line_bulk *bulk)
{
	unsigned all_up = (1 << n_lines) - 1;
	bool was_on = 0;

	while (1) {
		unsigned current = gpio_read(bulk);
		char active[10 + 1];	 /* good for up to 32 bits */

		if (current == all_up)
			break;
		sprintf(active, "%u", current ^ all_up);
		if (setenv("AZ5_ACTIVE", active, 1) < 0)
			perror("setenv");
		system(while_on_command);
		was_on = 1;
	}
	if (was_on)
		system(off_command);
}


static void loop(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line_event ev;
	struct gpiod_line_bulk ev_bulk;
	bool changed;
	unsigned i;
	int res;

	while (1) {
		res = gpiod_line_event_wait_bulk(bulk, NULL, &ev_bulk);
		if (res < 0) {
			perror("gpiod_line_event_wait_bulk");
			exit(1);
		}
		/* @@@ should never happen */
		if (!res)
			continue;
		changed = 0;
		for (i = 0; i != n_lines; i++) {
			struct gpiod_line *line, **lp;
			bool found = 0;

			gpiod_line_bulk_foreach_line(&ev_bulk, line, lp) {
				if (line == bulk->lines[i]) {
					found = 1;
					break;
				}
			}
			if (!found)
				continue;
			if (gpiod_line_event_read(bulk->lines[i], &ev) < 0) {
				perror("gpiod_line_event_read");
				exit(1);
			}
			changed = 1;
		}
		if (!changed)
			continue;
		update(bulk);
	}
}


static unsigned get_gpio(const char *name)
{
	char *end;
	unsigned i, n;

	for (i = 0; gpio_map[i].name; i++)
		if (!strcmp(gpio_map[i].name, name))
			return gpio_map[i].n;
	n = strtoul(name, &end, 0);
	if (*end) {
		fprintf(stderr, "invalid GPIO name/number \"%s\"\n", name);
		exit(1);
	}
	return n;
}


static void set_generation(void)
{
	const char *generation = getenv("BOARD_GENERATION");

	if (!generation) {
		fprintf(stderr, "BOARD_GENERATION is not set\n");
		exit(1);
	}
	if (!strcmp(generation, "2"))
		gpio_map = gpio_map_gen2;
}


static void usage(const char *name)
{
	unsigned i;
	unsigned pos = 0, len;

	fprintf(stderr,
"usage: %s [-g 1|2] gpio ... while-on-command off-command\n\n"
"-g 1|2\n"
"    board generation (default: 1)\n"
    , name);

	fprintf(stderr, "GPIO names:\n");
	for (i = 0; gpio_map[i].name; i++) {
		len = strlen(gpio_map[i].name);
		if (pos && pos + len + 1 > MAX_WIDTH) {
			fprintf(stderr, "\n");
			pos = 0;
		}
		fprintf(stderr, "%s%s", pos ? " " : "", gpio_map[i].name);
		pos += len + 1;
	}
	fprintf(stderr, "\n");
	exit(1);
}


int main(int argc, char *argv[])
{
	struct gpiod_line_bulk bulk;
	int c;
	unsigned i;

	set_generation();
	while ((c = getopt(argc, argv, "g:")) != EOF)
		switch (c) {
		case 'g':
			if (!strcmp(optarg, "1"))
				gpio_map = gpio_map_gen1;
			else if (!strcmp(optarg, "2"))
				gpio_map = gpio_map_gen2;
			else
				usage(*argv);
			break;
		default:
			usage(*argv);
		}
	if (argc - optind < 3)
		usage(*argv);

	n_lines = argc - optind - 2;
	lines = malloc(sizeof(unsigned) * n_lines);
	if (!lines) {
		perror("malloc");
		exit(1);
	}

	for (i = 0; i != n_lines; i++)
		lines[i] = get_gpio(argv[optind + i]);

	while_on_command = argv[argc - 2];
	off_command = argv[argc - 1];

	setup_gpio(&bulk);

	loop(&bulk);
	return 0;
}
