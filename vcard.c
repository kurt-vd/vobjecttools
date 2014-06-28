#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include <error.h>

#include "vcard.h"

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

/* vcard parser struct */
struct vcard {
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
void vcard_set_priv(struct vcard *vc, void *dat)
{
	vc->priv = dat;
}

void *vcard_get_priv(struct vcard *vc)
{
	return vc->priv;
}

/* vprop walk function */
struct vprop *vcard_props(struct vcard *vc)
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

/* walk through vprop meta data */
const char *vprop_next_meta(struct vprop *vp, const char *str)
{
	char *lo;

	if (!str)
		return vp->meta;
	/* take str after this str in memory, only one 0 terminator allowed */
	str += strlen(str)+1;
	if (str >= vp->value)
		return NULL;
	/*
	 * Avoid CAPITALIZED metadata
	 * This will burn cpu cycles only when & each time that
	 * the metadata is requested.
	 * I considered that most flows will only come here when
	 * a decision to use this vcard has already been made.
	 */
	for (lo = (char *)str; *lo; ++lo)
		*lo = tolower(*lo);
	return str;
}

/* fast access functions */
const char *vcard_prop(struct vcard *vc, const char *propname)
{
	struct vprop *vp;

	for (vp = vc->props; vp; vp = vp->next) {
		if (!strcasecmp(vp->key, propname))
			return vp->value;
	}
	return NULL;
}

static struct vprop *vcard_append_line(struct vcard *vc, const char *line)
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

/* free a vcard */
void vcard_free(struct vcard *vc)
{
	struct vprop *vp, *saved;

	for (vp = vc->props; vp; ) {
		saved = vp;
		/* step */
		vp = vp->next;
		/* remove */
		free(saved);
	}
	free(vc);
}

/* read next vcard from file */
struct vcard *vcard_next(FILE *fp, int *linenr)
{
	char *line = NULL, *saved = NULL;
	size_t linesize = 0, savedsize = 0, savedlen = 0;
	int ret, mylinenr = 0;
	struct vcard *vc = NULL;

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
			if (savedlen + ret -1 < savedsize -1) {
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
				vcard_append_line(vc, saved);
			/* erase saved stuff */
			savedlen = 0;
			*saved = 0;
		}
		/* fresh line, new property */
		if (!strcasecmp(line, "BEGIN:VCARD")) {
			if (vc)
				error(1, 0, "nested BEGIN on line %u", *linenr);
			/* create VCard */
			vc = zalloc(sizeof(*vc));
			/* don't add this line */
			continue;
		} else if (vc && !strcasecmp(line, "END:VCARD"))
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
