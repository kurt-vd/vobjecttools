/*
 * Copyright 2014 Kurt Van Dijck <kurt@vandijck-laurijssen.be>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <unistd.h>
#include <error.h>
#include <getopt.h>
#include <fcntl.h>

#include "vcard.h"

#define NAME "testvobject"

/* program options */
static const char help_msg[] =
	NAME ": read+write vobject files\n"
	"usage:	" NAME " [INPUT [OUTPUT]]\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Verbose output\n"
	;

#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },

	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "Vv?";

/* program variables */
static int verbose;

int main(int argc, char *argv[])
{
	int ret, opt, linenr = 0;
	struct vobject *vc;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) >= 0)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\nCompiled on %s %s\n",
				NAME, VERSION, __DATE__, __TIME__);
		exit(0);
	case 'v':
		++verbose;
		break;

	case '?':
		fputs(help_msg, stderr);
		exit(0);
	default:
		fprintf(stderr, "unknown option '%c'", opt);
		fputs(help_msg, stderr);
		exit(1);
		break;
	}

	if (optind < argc) {
		ret = open(argv[optind], O_RDONLY);
		if (ret < 0)
			error(1, errno, "open %s", argv[optind]);
		dup2(ret, STDIN_FILENO);
		close(ret);
		++optind;
	}
	if (optind < argc) {
		ret = open(argv[optind], O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (ret < 0)
			error(1, errno, "open %s", argv[optind]);
		dup2(ret, STDOUT_FILENO);
		++optind;
	}

	while (1) {
		vc = vobject_next(stdin, &linenr);
		if (!vc)
			break;
		vobject_write(vc, stdout);
		vobject_free(vc);
	}
	/* emit results to stdout */
	return 0;
}

