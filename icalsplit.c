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
#include <getopt.h>
#include <libgen.h>

#include "vobject.h"

#define NAME "icalsplit"

/* generic error logging */
#define elog(exitcode, errnum, fmt, ...) \
	{\
		fprintf(stderr, "%s: " fmt "\n", NAME, ##__VA_ARGS__);\
		if (errnum)\
			fprintf(stderr, "\t: %s\n", strerror(errnum));\
		if (exitcode)\
			exit(exitcode);\
		fflush(stderr);\
	}

/* program options */
static const char help_msg[] =
	NAME ": split ical/vcard into files with 1 single element\n"
	"usage:	" NAME " [OPTIONS ...] [FILE ...]\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Verbose output\n"
	" -o, --options=OPTS	Add extra KEY[=VALUE] pairs\n"
	"	columns=NUM	Set #columns to use, default 80 per spec\n"
	"	* break		Break lines on 80 columns\n"

	"\n"
	"Arguments\n"
	" FILE		Files to use, '-' for stdin\n"
	"		No files means 'stdin only'\n"
	;

static char *const subopttable[] = {
	"break",
#define O_BREAK	0
	0,
};

#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "options", required_argument, NULL, 'o', },

	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "Vv?o:";

/* program variables */
static int verbose;
static int flags = 1 << O_BREAK;

static int testflag(int num)
{
	return !!(flags & (1 << num));
}

/* generic file open method */
static FILE *myfopen(const char *filename, const char *mode)
{
	if (!strcmp("-", filename))
		return stdin;
	if (*filename == '~') {
		char *tmp;
		FILE *fp;

		asprintf(&tmp, "%s/%s", getenv("HOME"), filename+2);
		fp = fopen(tmp, mode);
		free(tmp);
		return fp;
	} else
		return fopen(filename, mode);
}

/* write vobject to a unique filename */
static void myvobject_write(const struct vobject *vo)
{
	int fd;
	FILE *fp;
	char filename[] = "XXXXXX";

	fd = mkstemp(filename);
	if (fd < 0)
		elog(1, errno, "mkstmp %s", filename);
	fp = fdopen(fd, "w");
	if (!fp)
		elog(1, errno, "fdopen %s", filename);
	vobject_write2(vo, fp, testflag(O_BREAK) ? 80 : 0);
	fclose(fp);
	close(fd);
}

static void copy_timezones(const struct vobject *dut, struct vobject *root,
		const struct vobject *origroot)
{
	const struct vprop *vprop;
	const char *tzstr;

	const struct vobject *tz;

	for (vprop = vobject_props(dut); vprop; vprop = vprop_next(vprop)) {
		tzstr = vprop_meta(vprop, "tzid");
		if (!tzstr)
			continue;

		/* look for VTIMEZONE @tzstr */
		for (tz = vobject_first_child(root); tz; tz = vobject_next_child(tz)) {
			if (strcasecmp("VTIMEZONE", vobject_type(tz)))
				continue;
			if (!strcmp(vobject_prop(tz, "tzid") ?: "", tzstr))
				/* VTIMEZONE already present */
				break;
		}
		if (tz)
			continue;
		/* find the timezone in original vobject */
		for (tz = vobject_first_child(origroot); tz; tz = vobject_next_child(tz)) {
			if (strcasecmp("VTIMEZONE", vobject_type(tz)))
				continue;
			if (!strcmp(vobject_prop(tz, "tzid") ?: "", tzstr)) {
				/* append timezone */
				vobject_attach(vobject_dup(tz), root);
				break;
			}
		}
		if (!tz)
			elog(0, 0, "Timezone '%s' not found", tzstr);
	}

}
/* real split program */
void icalsplit(FILE *fp, const char *name)
{
	struct vobject *root, *sub;
	struct vobject *newroot, *newsub;
	int linenr = 0;

	while (1) {
		root = vobject_next(fp, &linenr);
		if (!root)
			break;
		if (strcasecmp(vobject_type(root), "VCALENDAR"))
			/* save single non-calendar element */
			vobject_write(root, stdout);
		else for (sub = vobject_first_child(root); sub; sub =
				vobject_next_child(sub)) {
			/* save (potentially) each single element */
			if (!strcasecmp(vobject_type(sub), "VTIMEZONE"))
				/* skip timezones */
				continue;
			newroot = vobject_dup_root(root);
			newsub = vobject_dup(sub);
			copy_timezones(newsub, newroot, root);
			vobject_attach(newsub, newroot);
			/* todo : timezones */
			myvobject_write(newroot);
			vobject_free(newroot);
		}
		vobject_free(root);
	}
}

int main(int argc, char *argv[])
{
	int opt;
	FILE *fp;
	int not;
	char *subopts;

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
	case 'o':
		subopts = optarg;
		while (*subopts) {
			not = !strncmp(subopts, "no", 2);
			if (not)
				subopts += 2;
			opt = getsubopt(&subopts, subopttable, &optarg);
			if (opt < 0)
				break;
			switch (opt) {
			default:
				/* make sure O_xxx & FL_xxx correspond */
				if (not)
					flags &= ~(1 << opt);
				else
					flags |= 1 << opt;
				break;
			}
		}
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

	/* filter from file(s) */
	if (argv[optind])
	for (; argv[optind]; ++optind) {
		fp = myfopen(argv[optind], "r");
		if (!fp)
			elog(1, errno, "fopen %s", argv[optind]);
		if (verbose)
			printf("## %s\n", argv[optind]);
		icalsplit(fp, basename(argv[optind]));
		fclose(fp);
	} else
		icalsplit(stdin, "stdin");
	/* emit results to stdout */
	return 0;
}

