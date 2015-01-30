#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

#include <error.h>

#include "vobject.h"

/* helper functions */
static void *zalloc(unsigned int size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		error(1, errno, "malloc %u", size);
	memset(ptr, 0, size);
	return ptr;
}

/* vobject parser struct */
struct vobject {
	char *type; /* VCALENDAR, VCARD, VEVENT, ... */
	struct vprop {
		struct vprop *next;
		/*
		 * a property is just the copy of the _complete_ line (key)
		 * value & meta are just pointers to the memory
		 * key is null terminated
		 * meta is a null terminated sequence
		 */
		char *value;
		char *meta;
		/*
		 * Last member of the struct!
		 * 1 byte is enough here for key, the actual required length
		 * is allocated in runtime.
		 * 8 bytes is more convenient for debugging
		 * */
		char key[8];
	} *props, *last;
	/* members to be used by application */
	void *priv;
};

/* access the application private member */
void vobject_set_priv(struct vobject *vc, void *dat)
{
	vc->priv = dat;
}

void *vobject_get_priv(struct vobject *vc)
{
	return vc->priv;
}

const char *vobject_type(struct vobject *vc)
{
	return vc->type;
}

/* vprop walk function */
struct vprop *vobject_props(struct vobject *vc)
{
	return vc->props;
}

struct vprop *vprop_next(struct vprop *vp)
{
	return vp->next;
}

/* access vprop attributes */
const char *vprop_name(struct vprop *vp)
{
	return vp->key;
}

const char *vprop_value(struct vprop *vp)
{
	return vp->value;
}

static const char *locase(char *str)
{
	char *lo;

	if (!str)
		goto done;
	for (lo = str; *lo; ++lo)
		*lo = tolower(*lo);
done:
	return str;
}
/* walk through vprop meta data */
const char *vprop_next_meta(struct vprop *vp, const char *str)
{
	/*
	 * Avoid CAPITALIZED metadata
	 * This will burn cpu cycles only when & each time that
	 * the metadata is requested.
	 * I considered that most flows will only come here when
	 * a decision to use this vobject has already been made.
	 */
	if (!str)
		return locase(vp->meta);
	/* take str after this str in memory, only one 0 terminator allowed */
	str += strlen(str)+1;
	if (str >= vp->value)
		return NULL;
	return locase((char *)str);
}

/* fast access functions */
const char *vobject_prop(struct vobject *vc, const char *propname)
{
	struct vprop *vp;

	for (vp = vc->props; vp; vp = vp->next) {
		if (!strcasecmp(vp->key, propname))
			return vp->value;
	}
	return NULL;
}

static struct vprop *vobject_append_line(struct vobject *vc, const char *line)
{
	struct vprop *vp;
	char *str;

	vp = zalloc(sizeof(*vp) + strlen(line));
	strcpy(vp->key, line);

	/* seperate value from key */
	vp->value = strchr(vp->key, ':');
	if (vp->value)
		*vp->value++ = 0;
	/* seperate meta from key */
	vp->meta = strchr(vp->key, ';');
	if (vp->meta) {
		*vp->meta++ = 0;
		/* insert null terminator */
		for (str = strchr(vp->meta, ';'); str; str = strchr(str, ';'))
			*str++ = 0;
	}
	/* append in linked list */
	if (vc->last)
		vc->last->next = vp;
	else
		vc->props = vp;
	vc->last = vp;
	return vp;
}

/* free a vobject */
void vobject_free(struct vobject *vc)
{
	struct vprop *vp, *saved;

	for (vp = vc->props; vp; ) {
		saved = vp;
		/* step */
		vp = vp->next;
		/* remove */
		free(saved);
	}
	if (vc->type)
		free(vc->type);
	free(vc);
}

/* read next vobject from file */
struct vobject *vobject_next(FILE *fp, int *linenr)
{
	char *line = NULL, *saved = NULL;
	size_t linesize = 0, savedsize = 0, savedlen = 0;
	int ret, mylinenr = 0;
	struct vobject *vc = NULL;

	if (!linenr)
		linenr = &mylinenr;

	while (1) {
		ret = getline(&line, &linesize, fp);
		if (ret < 0) {
			if (vc)
				error(0, 0, "unexpected EOF on line %u", *linenr);
			break;
		}
		++(*linenr);
		while (ret && strchr("\r\n\v\f", line[ret-1]))
			--ret;
		line[ret] = 0;
		if (strchr("\t ", *line)) {
			/* add line to previous */
			if (!saved || !*saved) {
				error(0, 0, "bad line %u", *linenr);
				continue;
			}
			if (savedlen + ret -1 + 1 > savedsize) {
				savedsize = (savedlen + ret - 1 + 1 + 63) & ~63;
				saved = realloc(saved, savedsize);
			}
			strcpy(saved+savedlen, line+1);
			savedlen += ret-1;
			continue;
		}
		if (saved && *saved) {
			/* append property */
			if (vc)
				vobject_append_line(vc, saved);
			/* erase saved stuff */
			savedlen = 0;
			*saved = 0;
		}
		/* fresh line, new property */
		if (!strncasecmp(line, "BEGIN:", 6)) {
			if (vc)
				error(1, 0, "nested BEGIN on line %u", *linenr);
			/* create VCard */
			vc = zalloc(sizeof(*vc));
			vc->type = strdup(line+6);
			/* don't add this line */
			continue;
		} else if (vc && !strncasecmp(line, "END:", 4) &&
				!strcasecmp(line+4, vc->type))
			break;
		/* save line, we only know that a line finished on next line */
		if (savedsize < ret+1) {
			savedsize = (ret + 1 + 63) & ~63;
			saved = realloc(saved, savedsize);
		}
		strcpy(saved, line);
		savedlen = ret;
	}
	if (saved)
		free(saved);
	if (line)
		free(line);
	return vc;
}

static int appendprintf(char **pline, size_t *psize, size_t pos, const char *fmt, ...)
{
#define BLOCKSZ	64
	int len;
	char *printed;
	va_list va;

	va_start(va, fmt);
	len = vasprintf(&printed, fmt, va);
	va_end(va);

	if (len < 0)
		return 0;

	if ((pos + len + 1) > *psize) {
		*psize = ((pos + len + 1) + BLOCKSZ -1) % ~(BLOCKSZ-1);
		*pline = realloc(*pline, *psize);
	}
	strcpy((*pline) + pos, printed);
	free(printed);
	return len;
}

/* output vobjects, returns the number of ascii lines */
int vobject_write(const struct vobject *vc, FILE *fp)
{
	int nlines = 0;
	struct vprop *vp;
	char *line = NULL;
	const char *meta;
	size_t linesize = 0, fill, pos, todo;

	fprintf(fp, "BEGIN:%s\n", vc->type);
	++nlines;

	/* iterate over all properties */
	for (vp = vc->props; vp; vp = vp->next) {
		fill = appendprintf(&line, &linesize, 0, "%s", vp->key);
		for (meta = vprop_next_meta(vp, NULL); meta; meta =
				vprop_next_meta(vp, meta))
			fill += appendprintf(&line, &linesize, fill, ";%s", meta);
		fill += appendprintf(&line, &linesize, fill, ":%s", vp->value);

		for (pos = 0; pos < fill; pos += todo) {
			todo = pos ? 79 : 80;
			if (pos + todo > fill)
				todo = fill - pos;
			if (pos)
				fputc(' ', fp);
			if (fwrite(line+pos, todo, 1, fp) < 0)
				error(1, errno, "fwrite");
			fputc('\n', fp);
			++nlines;
		}
	}

	/* terminate vobject */
	fprintf(fp, "END:%s\n", vc->type);
	++nlines;
	return nlines;
}
