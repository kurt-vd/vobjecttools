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
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>

#include "vobject.h"

#define NAME "votool"

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
	"usage:	" NAME " -a ACTION [OPTIONS ...] [FILE ...]\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Verbose output\n"
	" -a, --action=ACTION	Perform action, one of:\n"
	"	* cat		Read & write to stdout\n"
	"	- split		Split VCalendar's so each contains only 1 VEVENT\n"
	" -o, --options=OPTS	Add extra KEY[=VALUE] pairs\n"
	"	* break		Break lines on 80 columns\n"
	" -O, --output=FILE	Output all vobjects to FILE\n"

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

	{ "action", required_argument, NULL, 'a', },
	{ "options", required_argument, NULL, 'o', },
	{ "output", required_argument, NULL, 'O', },

	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "Vv?a:o:O:";

/* program variables */
static int verbose;
static const char *action;
static int flags = 1 << O_BREAK;
static char *outputfile;

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

static void redirect_output(void)
{
	if (outputfile && strcmp("-", outputfile)) {
		int fd;

		fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
			elog(1, errno, "open %s", outputfile);
		if (dup2(fd, STDOUT_FILENO) < 0)
			elog(1, errno, "dup2 %s", outputfile);
		close(fd);
	}
}

static const char *find_prefix(const struct vobject *vo)
{
	const char *type = vobject_type(vo), *saved_type;

	if (!strcasecmp(type, "vcard"))
		return "card";
	if (!strcasecmp(type, "vevent"))
		return "evnt";
	else if (!strcasecmp(type, "vtodo"))
		return "todo";
	else if (!strcasecmp(type, "vjournal"))
		return "jrnl";
	else if (!strcasecmp(type, "vfreebusy"))
		return "busy";
	else if (strcasecmp(type, "vcalendar"))
		return NULL;
	/* vcalendar */
	saved_type = NULL;
	for (vo = vobject_first_child(vo); vo; vo = vobject_next_child(vo)) {
		type = find_prefix(vo);
		if (saved_type && type && strcmp(saved_type, type))
			/* heterogenous types found */
			return "cal";
		saved_type = type ?: saved_type;
	}
	return saved_type ?: "cal";
}

/* write vobject to a unique filename */
static void myvobject_write(const struct vobject *vo)
{
	int fd;
	FILE *fp;
	char filename[32];

	if (outputfile) {
		/* output to single file, dup2'd to stdout */
		vobject_write2(vo, stdout, testflag(O_BREAK) ? 80 : 0);
		return;
	}
	sprintf(filename, "XXXXXX.%s", find_prefix(vo) ?: "ics");
	fd = mkstemps(filename, strlen(filename)-6);
	if (fd < 0)
		elog(1, errno, "mkstmp %s", filename);
	fp = fdopen(fd, "w");
	if (!fp)
		elog(1, errno, "fdopen %s", filename);
	vobject_write2(vo, fp, testflag(O_BREAK) ? 80 : 0);
	fclose(fp);
	close(fd);
}

/*
 * SPLIT
 */
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
			myvobject_write(root);
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
	case 'a':
		action = optarg;
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
	case 'O':
		outputfile = optarg;
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

	/* prepare arguments */
	argv += optind;
	if (!*argv)
		argv = NULL;

	/* differentiate */
	if (action && !strcmp("split", action)) {
		if (!argv)
			/* avoid creating output */
			elog(1, 0, "no input files");
		redirect_output();
		/* filter from file(s) */
		for (; *argv; ++argv) {
			fp = myfopen(*argv, "r");
			if (!fp)
				elog(1, errno, "fopen %s", *argv);
			if (verbose)
				printf("## %s\n", *argv);
			icalsplit(fp, basename(*argv));
			fclose(fp);
		}
		return 0;
	} else if (action && !strcmp("cat", action)) {
		struct vobject * vc;
		int linenr = 0;

		if (!argv)
			elog(1, 0, "no input files");
		redirect_output();
		for (; *argv; ++argv) {
			fp = myfopen(*argv, "r");
			if (!fp)
				elog(1, errno, "fopen %s", *argv);
			if (verbose)
				printf("## %s\n", *argv);
			while (1) {
				vc = vobject_next(fp, &linenr);
				if (!vc)
					break;
				vobject_write(vc, stdout);
				vobject_free(vc);
			}
			fclose(fp);
		}
		return 0;
	}

	fprintf(stderr, "unknown action '%s'\n", action ?: "<>");
	fputs(help_msg, stderr);
	exit(1);
}

