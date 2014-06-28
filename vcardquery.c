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

#include "vcard.h"

#define NAME "vcardquery"

/* program options */
static const char help_msg[] =
	NAME ": filter VCard properties\n"
	"usage:	" NAME " [OPTIONS ...] NEEDLE [FILE ...]\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Verbose output\n"

	" -p, --prop=PROP	Which property to retrieve (default: EMAIL)\n"
	" -s, --swap		Output property, then name, then metadata\n"
	" -M, --mutt		Output for Mutt (prop=EMAIL, swap + header line)\n"
	"\n"
	"Arguments\n"
	" NEEDLE	The text to look for in NAME or <PROP>\n"
	" FILE		Files to use, '-' for stdin\n"
	"		No files means 'stdin only'\n"
	;

#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "prop", required_argument, NULL, 'p', },
	{ "swap", no_argument, NULL, 's', },
	{ "mutt", no_argument, NULL, 'M', },
	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "Vv?p:sM";

/* program variables */
static int verbose;
/* print value first, then name, then metadata (like for Mutt) */
static int swapoutput;

void vcard_add_result(struct vcard *vc, const char *lookfor, int nthprop)
{
	const char *name, *str, *eq;
	struct vprop *vp;
	int nprop = 0, nmeta;

	name = vcard_prop(vc, "FN") ?: "<no name>";

	for (vp = vcard_props(vc); vp; vp = vprop_next(vp)) {
		if (strcasecmp(lookfor, vprop_name(vp)))
			continue;
		if ((nthprop >= 0) && (nthprop != nprop++))
			continue;
		if (swapoutput)
			printf("%s\t%s", vprop_value(vp), name);
		else
			printf("%s\t%s", name, vprop_value(vp));

		for (str = vprop_next_meta(vp, NULL), nmeta = 0; str;
				str = vprop_next_meta(vp, str)) {
			if (!strncasecmp(str, "X-", 2))
				/* ignore Xtended metadata */
				continue;
			if (!strcasecmp(lookfor, "EMAIL") && !strcasecmp(str, "TYPE=INTERNET"))
				/* ignore 'internet' type for email ... */
				continue;
			eq = strchr(str, '=');
			printf("%c%s", nmeta ? ',' : '\t', eq ? eq+1 : str);
			++nmeta;
		}
		printf("\n");
	}
}

/* real filter program */
int vcard_filter(FILE *fp, const char *needle, const char *lookfor)
{
	struct vcard *vc;
	struct vprop *vp;
	int linenr = 0, ncards = 0, nprop;
	const char *propname;

	while (1) {
		vc = vcard_next(fp, &linenr);
		if (!vc)
			break;
		nprop = 0;
		for (vp = vcard_props(vc); vp; vp = vprop_next(vp)) {
			/* match in name */
			propname = vprop_name(vp);
			if (!strcasecmp(propname, "FN")) {
				if (strcasestr(vprop_value(vp), needle)) {
					vcard_add_result(vc, lookfor, -1);
					break;
				}
			} else if (!strcasecmp(propname, lookfor)) {
				if (strcasestr(vprop_value(vp), needle))
					vcard_add_result(vc, lookfor, nprop);
					/* don't abort the loop here, maybe add
					 * other property entries (like multiple
					 * email addresses */
				++nprop;
			}
		}
		vcard_free(vc);
	}
	return ncards;
}

int main(int argc, char *argv[])
{
	int opt;
	const char *needle;
	const char *lookfor = "email";
	FILE *fp;
	int mutt = 0;

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

	case 'p':
		lookfor = optarg;
		break;
	case 's':
		swapoutput = 1;
		break;
	case 'M':
		mutt = 1;
		swapoutput = 1;
		lookfor = "EMAIL";
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

	if (optind >= argc) {
		fprintf(stderr, "no search string");
		fputs(help_msg, stderr);
		exit(1);
	}
	needle = argv[optind++];

	if (mutt)
		/* emit 1 line to ignore for mutt */
		printf("%s %s\n", NAME, VERSION);

	/* filter from file(s) */
	if (!argv[optind])
		vcard_filter(stdin, needle, lookfor);
	else for (; argv[optind]; ++optind) {
		fp = fopen(argv[optind], "r");
		if (!fp)
			error(1, errno, "fopen %s", argv[optind]);
		vcard_filter(fp, needle, lookfor);
		fclose(fp);
	}
	/* emit results to stdout */
	return 0;
}

