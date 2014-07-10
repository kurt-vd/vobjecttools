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
	" -i, --indent		Output with indents\n"
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
	{ "indent", no_argument, NULL, 'i', },
	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "Vv?p:sMi";

/* program variables */
static int verbose;
/* print value first, then name, then metadata (like for Mutt) */
static int swapoutput;
/* print indented list */
static int indented;

/* configuration values */
static char **files;
static int nfiles, rfiles; /* used & reserved files */

/* generic file open method */
static FILE *myfopen(const char *filename, const char *mode)
{
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

/* parse config file */
static int parse_config(const char *filename)
{
	char *line = NULL;
	size_t linesize = 0, linenr = 0;
	int ret;
	char *tok;
	FILE *fp;

	fp = myfopen(filename, "r");
	if (!fp) {
		if (verbose)
			error(0, errno, "fopen %s", filename);
		return 0;
	}

	while (1) {
		ret = getline(&line, &linesize, fp);
		if (ret < 0)
			break;
		++linenr;
		if (line[0] == '#')
			/* ignore comments */
			continue;
		tok = strtok(line, " \t\r\n\v\f");
		if (!tok) {
			/* empty line */
		} else if (!strcmp(tok, "file")) {
			/* add config file */
			if ((nfiles+1) >= rfiles) {
				rfiles += 16;
				files = realloc(files, rfiles * sizeof(*files));
			}
			files[nfiles++] = strdup(strtok(NULL, " \t\r\n\v\f"));
		} else if (verbose)
			error(0, 0, "unknown config option '%s' in %s:%lu", tok,
					filename, linenr);
	}
	fclose(fp);
	return 0;
}

/*
 * Parse elements into a vector of strings.
 * Element seperators will be overwritten with 0
 * To restore, use cleanupstrvector()
 */
static int savestrvector(char *str, int sep, char **vec, int rvec)
{
	int nvec = 0;

	do {
		vec[nvec++] = str;
		str = strchr(str, sep);
		if (str && (nvec >= rvec))
			break;
		if (str)
			*str++ = 0;
	} while (str);
	/* pad with NULL */
	memset(vec+nvec, 0, (rvec - nvec) * sizeof(*vec));
	return nvec;
}

static void cleanupstrvector(char **vec, int sep)
{
	/* restore seperators */
	for (; *vec; ++vec)
		(*vec)[strlen(*vec)] = sep;
}

/* compact representation of meta data */
static const char *vprop_meta_str(struct vprop *vp)
{
	static char buf[1024];
	const char *str, *eq;
	char *ostr = buf;

	for (str = vprop_next_meta(vp, NULL); str;
			str = vprop_next_meta(vp, str)) {
		if (!strncasecmp(str, "X-", 2))
			/* ignore Xtended metadata */
			continue;
		if (!strcasecmp(vprop_name(vp), "EMAIL") &&
				!strcasecmp(str, "TYPE=INTERNET"))
			/* ignore 'internet' type for email ... */
			continue;
		eq = strchr(str, '=');
		if (ostr > buf)
			*ostr++ = ',';
		strcpy(ostr, eq ? eq+1 : str);
		ostr += strlen(str);
	}
	return (ostr > buf) ? buf : NULL;
}

/* print indented result */
void vcard_indent_result(struct vcard *vc, const char *lookfor, int bitmask)
{
	const char *meta;
	struct vprop *vp;
	int nprop = 0, nvec, j;
	char *vec[16];

	printf("%s\n", vcard_prop(vc, "FN") ?: "<no name>");

	for (vp = vcard_props(vc); vp; vp = vprop_next(vp)) {
		if (strcasecmp(lookfor, vprop_name(vp)))
			continue;
		if (!(bitmask & (1 << nprop++)))
			continue;
		/* found a property, first print tags */
		meta = vprop_meta_str(vp);
		if (meta)
			printf("\t[%s]\n", meta);

		nvec = savestrvector((char *)vprop_value(vp), ';', vec, 16);
		if (!strcasecmp("ADR", lookfor)) {
			if (vec[0] && vec[0][0])
				printf("\t%s\n", vec[0]);
			if (vec[1] && vec[1][0])
				printf("\t%s\n", vec[1]);
			if (vec[2] && vec[2][0])
				printf("\t%s\n", vec[2]);
			if ((vec[3] && vec[3][0]) || (vec[5] && vec[5][0]))
				printf("\t%s %s\n", vec[5], vec[3]);
			if (vec[4] && vec[4][0])
				printf("\t%s\n", vec[4]);
			if (vec[6] && vec[6][0])
				printf("\t%s\n", vec[6]);
		} else for (j = 0; j < nvec; ++j)
		       printf("\t%s\n", vec[j]);
		cleanupstrvector(vec, ';');
	}
}

void vcard_add_result(struct vcard *vc, const char *lookfor, int bitmask)
{
	const char *name, *meta;
	struct vprop *vp;
	int nprop = 0;

	if (indented) {
		vcard_indent_result(vc, lookfor, bitmask);
		return;
	}

	name = vcard_prop(vc, "FN") ?: "<no name>";

	for (vp = vcard_props(vc); vp; vp = vprop_next(vp)) {
		if (strcasecmp(lookfor, vprop_name(vp)))
			continue;
		if (!(bitmask & (1 << nprop++)))
			continue;
		if (swapoutput)
			printf("%s\t%s", vprop_value(vp), name);
		else
			printf("%s\t%s", name, vprop_value(vp));
		meta = vprop_meta_str(vp);
		if (meta)
			printf("\t%s", meta);
		printf("\n");
	}
}

/* return a searchable telephone nr. */
static const char *searchable_telnr(const char *str)
{
	static char buf[128];
	char *tel = buf;

	/* allow leading + */
	if (*str == '+')
		*tel++ = *str++;

	for (; *str; ++str) {
		if (strchr("0123456789", *str))
			*tel++ = *str;
	}
	*tel = 0;
	return buf;
}

/* real filter program */
int vcard_filter(FILE *fp, const char *needle, const char *lookfor)
{
	struct vcard *vc;
	struct vprop *vp;
	int linenr = 0, ncards = 0, nprop, bitmask, propcnt;
	const char *propname, *propval;

	while (1) {
		vc = vcard_next(fp, &linenr);
		if (!vc)
			break;
		nprop = 0;
		propcnt = 0;
		bitmask = 0;
		for (vp = vcard_props(vc); vp; vp = vprop_next(vp)) {
			/* match in name */
			propname = vprop_name(vp);
			if (!strcasecmp(propname, "FN")) {
				if (strcasestr(vprop_value(vp), needle))
					bitmask = ~0;
			} else if (!strcasecmp(propname, "N")) {
				if (strcasestr(vprop_value(vp), needle))
					bitmask = ~0;
			} else if (!strcasecmp(propname, lookfor)) {
				/* count props */
				++propcnt;
				propval = vprop_value(vp);
				if (!strcasecmp(propname, "TEL"))
					propval = searchable_telnr(propval); 
				if (strcasestr(propval, needle))
					bitmask |= 1 << nprop;
				++nprop;
			}
		}
		if (bitmask && propcnt)
			vcard_add_result(vc, lookfor, bitmask);
		vcard_free(vc);
	}
	return ncards;
}

int main(int argc, char *argv[])
{
	int opt, j;
	const char *needle;
	const char *lookfor = "email";
	FILE *fp;
	int mutt = 0;

	parse_config("/etc/vcardquery.conf");
	parse_config("~/.vcardquery");
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
	case 'i':
		indented = 1;
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
	if (argv[optind])
	for (; argv[optind]; ++optind) {
		fp = myfopen(argv[optind], "r");
		if (!fp)
			error(1, errno, "fopen %s", argv[optind]);
		if (verbose)
			printf("## %s\n", argv[optind]);
		vcard_filter(fp, needle, lookfor);
		fclose(fp);
	} else if (nfiles)
	for (j = 0; j < nfiles; ++j) {
		fp = myfopen(files[j], "r");
		if (!fp)
			error(1, errno, "fopen %s", files[j]);
		if (verbose)
			printf("## %s\n", files[j]);
		vcard_filter(fp, needle, lookfor);
		fclose(fp);
	} else
		vcard_filter(stdin, needle, lookfor);
	/* emit results to stdout */
	return 0;
}

