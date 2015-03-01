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
	"usage:	" NAME " ACTION [OPTIONS ...] [FILE ...]\n"
	"\n"
	"Actions\n"
	" *cat		Read & write to stdout\n"
	"  split	Split VCalendar's so each contains only 1 VEVENT\n"
	"  subject	Return a subject for each vobject\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Verbose output\n"
	" -o, --options=OPTS	Add extra KEY[=VALUE] pairs\n"
	"	* break		Break lines on 80 columns\n"
	"	  utf8		Avoid breaking inside UTF8 sequences, break before\n"
	"	  fix		Fix vobjects before processing\n"
	"			- Enforce single N for VCard\n"
	" -O, --output=FILE	Output all vobjects to FILE\n"

	"\n"
	"Arguments\n"
	" FILE		Files to use, '-' for stdin\n"
	"		No files means 'stdin only'\n"
	;

enum subopt {
	OPT_BREAK = 0,
	OPT_UTF8,
	OPT_FIX,
};

static char *const subopttable[] = {
	"break", /* matches VOF_BREAK */
	"utf8", /* matches VOF_UTF8 */
	"fix",

	0,
};

#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "options", required_argument, NULL, 'o', },
	{ "output", required_argument, NULL, 'O', },

	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "Vv?o:O:";

/* program variables */
static int verbose;
static const char *action = "";
static int flags;
static char *outputfile;

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

/* fix some vobject problems */
static void vobject_fix(struct vobject *vo)
{
	if (!strcasecmp(vobject_type(vo), "VCALENDAR")) {
		for (vo = vobject_first_child(vo); vo;
				vo = vobject_next_child(vo))
			vobject_fix(vo);
		return;
	} else if (!strcasecmp(vobject_type(vo), "VCARD")) {
		const char *propn, *next;
		const char *Nvalue = NULL, *str;

		for (propn = vobject_first_prop(vo); propn; propn = next) {
			/* get next prop already */
			next = vprop_next(propn);
			if (!strcasecmp(propn, "N")) {
				str = vprop_value(propn);
				if (!Nvalue)
					Nvalue = str;
				else if (strcmp(str, Nvalue)) {
					elog(0, 0, "remove N:%s for N:%s", str, Nvalue);
					vprop_remove(propn);
				} else {
					vprop_remove(propn);
				}
			}
		}
	}
}

static const char *find_suffix(const struct vobject *vo)
{
	return !strcasecmp("vcard", vobject_type(vo)) ? "vcf" : "ics";
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
		vobject_write2(vo, stdout, flags);
		return;
	}
	sprintf(filename, "%s-XXXXXX.%s", find_prefix(vo) ?: "cal", find_suffix(vo));
	fd = mkstemps(filename, strlen(strrchr(filename, '.')));
	if (fd < 0)
		elog(1, errno, "mkstmp %s", filename);
	fp = fdopen(fd, "w");
	if (!fp)
		elog(1, errno, "fdopen %s", filename);
	vobject_write2(vo, fp, flags);
	fclose(fp);
	close(fd);
}

/*
 * SPLIT
 */
static void copy_timezones(const struct vobject *dut, struct vobject *root,
		const struct vobject *origroot)
{
	const char *prop, *tzstr;

	const struct vobject *tz;

	for (prop = vobject_first_prop(dut); prop; prop = vprop_next(prop)) {
		tzstr = vprop_meta(prop, "tzid");
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
		if (flags & (1 << OPT_FIX))
			vobject_fix(root);
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

/* retrieve short subject */
const char *vosubject(const struct vobject *vo)
{
	const char *type = vobject_type(vo);

	if (!strcasecmp("vcalendar", type)) {
		const char *result;

		for (vo = vobject_first_child(vo); vo; vo = vobject_next_child(vo)) {
			result = vosubject(vo);
			if (result)
				return result;
		}
		return "vcalendar without subject";
	} else if (!strcasecmp(type, "vcard"))
		return vobject_prop(vo, "FN") ?: "vcard without subject";
	else if (!strcasecmp(type, "vevent"))
		return vobject_prop(vo, "summary");
	else if (!strcasecmp(type, "vtodo"))
		return vobject_prop(vo, "summary");
	else if (!strcasecmp(type, "vjournal"))
		return vobject_prop(vo, "summary");
	else
		return NULL;
}

int main(int argc, char *argv[])
{
	int opt;
	FILE *fp;
	int not;
	char *subopts;

	if (!argv[1]) {
		fputs(help_msg, stderr);
		exit(1);
	}
	if (argv[1][0] != '-') {
		/* consume action */
		action = argv[1];
		optind = 2;
	}
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
			const char *saved = subopts;

			not = !strncmp(subopts, "no", 2);
			if (not)
				subopts += 2;
			opt = getsubopt(&subopts, subopttable, &optarg);
			if (opt < 0) {
				elog(1, 0, "suboption '%s' unrecognized", saved);
				break;
			}
			switch (opt) {
			case OPT_BREAK:
				/* invert 'not' */
				not = !not;
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
	if (!strcmp("split", action)) {
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
	} else if (!strcmp("cat", action)) {
		struct vobject * vc;
		int linenr = 0;

		if (!argv)
			elog(1, 0, "no input files");
		redirect_output();
		for (; *argv; ++argv) {
			fp = myfopen(*argv, "r");
			if (!fp)
				elog(1, errno, "fopen %s", *argv);
			linenr = 0;
			if (verbose)
				printf("## %s\n", *argv);
			while (1) {
				vc = vobject_next(fp, &linenr);
				if (!vc)
					break;
				if (flags & (1 << OPT_FIX))
					vobject_fix(vc);
				vobject_write2(vc, stdout, flags);
				vobject_free(vc);
			}
			fclose(fp);
		}
	} else if (!strcmp("subject", action)) {
		struct vobject *vc;
		int linenr;

		if (!argv)
			elog(1, 0, "no input files");

		redirect_output();
		for (; *argv; ++argv) {
			fp = myfopen(*argv, "r");
			if (!fp)
				elog(1, errno, "fopen %s", *argv);
			linenr = 0;
			vc = vobject_next(fp, &linenr);
			/* only read 1 vobject */
			fclose(fp);
			if (!vc)
				continue;
			printf("%s\t%s\n", *argv, vosubject(vc));
			vobject_free(vc);
		}
	} else {
		fprintf(stderr, "unknown action '%s'\n", action ?: "<>");
		fputs(help_msg, stderr);
		exit(1);
	}
	return 0;
}

