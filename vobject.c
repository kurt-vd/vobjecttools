#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

#include <syslog.h>

#include "vobject.h"

/* generic error logging */
#define elog(level, errnum, fmt, ...) \
	{\
		fprintf(stderr, "%s: " fmt "\n", "vobject", ##__VA_ARGS__);\
		if (errnum)\
			fprintf(stderr, "\t: %s\n", strerror(errnum));\
		if (level >= LOG_ERR)\
			exit(1);\
		fflush(stderr);\
	}

/* helper functions */
static void *zalloc(unsigned int size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		elog(LOG_ERR, errno, "malloc %u", size);
	memset(ptr, 0, size);
	return ptr;
}

/* vobject parser struct */
struct vobject {
	char *type; /* VCALENDAR, VCARD, VEVENT, ... */
	struct vprop {
		/* IMPORTANT: next & prev must match parents 'props & proplast' */
		struct vprop *next, *prev;
		struct vprop *up;
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
	} *props, *proplast;
	/* hierarchy */
	struct vobject *next, *prev;
	struct vobject *list, *listlast, *parent;
	/* members to be used by application */
	void *priv;
};

/* access the application private member */
void vobject_set_priv(struct vobject *vc, void *dat)
{
	vc->priv = dat;
}

void *vobject_get_priv(const struct vobject *vc)
{
	return vc->priv;
}

const char *vobject_type(const struct vobject *vc)
{
	return vc->type;
}

/* vprop walk function */
struct vprop *vobject_props(const struct vobject *vc)
{
	return vc->props;
}

struct vprop *vprop_next(const struct vprop *vp)
{
	return vp->next;
}

/* access vprop attributes */
const char *vprop_name(const struct vprop *vp)
{
	return vp->key;
}

const char *vprop_value(const struct vprop *vp)
{
	return vp->value;
}

/* utility to export lower case string */
static char *locasestr;
__attribute__((destructor))
static void free_locasestr(void)
{
	if (locasestr)
		free(locasestr);
}

const char *lowercase(const char *str)
{
	char *lo;

	if (!str)
		return NULL;

	/* create copy */
	if (locasestr)
		free(locasestr);
	locasestr = strdup(str);
	/* convert to lowercase */
	for (lo = locasestr; *lo; ++lo)
		*lo = tolower(*lo);
	return locasestr;
}

/* walk through vprop meta data */
const char *vprop_next_meta(const struct vprop *vp, const char *str)
{
	/*
	 * Avoid CAPITALIZED metadata
	 * This will burn cpu cycles only when & each time that
	 * the metadata is requested.
	 * I considered that most flows will only come here when
	 * a decision to use this vobject has already been made.
	 */
	if (!str)
		return vp->meta;
	/* take str after this str in memory, only one 0 terminator allowed */
	str += strlen(str)+1;
	if (str >= vp->value)
		return NULL;
	return str;
}

/* fast access functions */
const char *vobject_prop(const struct vobject *vc, const char *propname)
{
	struct vprop *vp;

	for (vp = vc->props; vp; vp = vp->next) {
		if (!strcasecmp(vp->key, propname))
			return vp->value;
	}
	return NULL;
}

const char *vprop_meta(const struct vprop *prop, const char *metaname)
{
	const char *meta;
	int needlelen;

	needlelen = strlen(metaname);

	for (meta = vprop_next_meta(prop, NULL); meta; meta = vprop_next_meta(prop, meta)) {
		if (!strncasecmp(metaname, meta, needlelen)) {
			if (meta[needlelen] == '=')
				return meta+needlelen+1;
			else if (!meta[needlelen])
				/* no value assigned */
				return "";
			/* no fixed match, keep looking */
		}
	}
	return NULL;
}

static char *strchresc(const char *str, int c)
{
	int esc = 0;

	for (; *str; ++str) {
		if (esc) {
			if (*str == esc)
				esc = 0;
		} else if (*str == c)
			return (char *)str;
		else if (strchr("\"'", *str))
			esc = *str;
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
	vp->value = strchresc(vp->key, ':');
	if (vp->value)
		*vp->value++ = 0;
	/* seperate meta from key */
	vp->meta = strchresc(vp->key, ';');
	if (vp->meta) {
		*vp->meta++ = 0;
		/* insert null terminator */
		for (str = strchresc(vp->meta, ';'); str; str = strchr(str, ';'))
			*str++ = 0;
	}
	/* append in linked list */
	vp->prev = vc->proplast;
	if (vc->proplast)
		vc->proplast->next = vp;
	else
		vc->props = vp;
	vc->proplast = vp;
	return vp;
}

/* vobject hierarchy */
void vobject_detach(struct vobject *vo)
{
	if (!vo->parent)
		return;
	if (vo->parent->list == vo)
		vo->parent->list = vo->next;
	if (vo->parent->listlast == vo)
		vo->parent->listlast = vo->prev;
	if (vo->prev)
		vo->prev->next = vo->next;
	if (vo->next)
		vo->next->prev = vo->prev;
	vo->next = vo->prev = vo->parent = NULL;
}

void vobject_attach(struct vobject *obj, struct vobject *parent)
{
	vobject_detach(obj);

	obj->prev = parent->listlast;
	if (parent->listlast)
		parent->listlast->next = obj;
	else
		parent->list = obj;
	parent->listlast = obj;
	obj->parent = parent;
}

/* vprop hierarchy */
void vprop_detach(struct vprop *vp)
{
	/* parent level detach
	 * For this to work properly,
	 * the struct member layout is important!
	 */
	if (vp == vp->up->next)
		vp->up->next = vp->next;
	if (vp == vp->up->prev)
		vp->up->prev = vp->prev;
	/* linked list detach */
	if (vp->prev)
		vp->prev->next = vp->next;
	if (vp->next)
		vp->next->prev = vp->prev;
}
void vprop_attach(struct vprop *vp, struct vobject *vo)
{
	vprop_detach(vp);
	vp->prev = vo->proplast;
	vp->next = vo->props;
	if (vp->next)
		vp->next->prev = vp;
	if (vp->prev)
		vp->prev->next = vp;
	else
		vo->props = vp;
	vo->proplast = vp;
	/* tricky part, see struct definition */
	vp->up = (void *)&vo->props;
}

struct vobject *vobject_first_child(const struct vobject *vo)
{
	return vo ? vo->list : NULL;
}

struct vobject *vobject_next_child(const struct vobject *vo)
{
	return vo ? vo->next : NULL;
}

/* free a vobject */
void vprop_free(struct vprop *vp)
{
	vprop_detach(vp);
	free(vp);
}

/* free a vobject */
void vobject_free(struct vobject *vc)
{
	while (vc->props)
		vprop_free(vc->props);
	while (vc->list)
		vobject_free(vc->list);
	vobject_detach(vc);
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
				elog(LOG_INFO, 0, "unexpected EOF on line %u", *linenr);
			break;
		}
		++(*linenr);
		while (ret && strchr("\r\n\v\f", line[ret-1]))
			--ret;
		line[ret] = 0;
		if (strchr("\t ", *line)) {
			/* add line to previous */
			if (!saved || !*saved) {
				elog(LOG_INFO, 0, "bad line %u", *linenr);
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
			struct vobject *parent = vc;

			/* create new/child VCard */
			vc = zalloc(sizeof(*vc));
			vc->type = strdup(line+6);
			if (parent)
				vobject_attach(vc, parent);
			/* don't add this line */
			continue;
		} else if (vc && !strncasecmp(line, "END:", 4) &&
				!strcasecmp(line+4, vc->type)) {
			if (!vc->parent)
				/* end this vobject */
				break;
			vc = vc->parent;
			continue;
		}
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
int vobject_write2(const struct vobject *vc, FILE *fp, int columns)
{
	int nlines = 0;
	struct vprop *vp;
	char *line = NULL;
	const char *meta;
	size_t linesize = 0, fill, pos, todo;
	const struct vobject *child;

	fprintf(fp, "BEGIN:%s\n", vc->type);
	++nlines;

	/* iterate over all properties */
	for (vp = vc->props; vp; vp = vp->next) {
		fill = appendprintf(&line, &linesize, 0, "%s", vp->key);
		for (meta = vprop_next_meta(vp, NULL); meta; meta =
				vprop_next_meta(vp, meta))
			fill += appendprintf(&line, &linesize, fill, ";%s", meta);
		fill += appendprintf(&line, &linesize, fill, ":%s", vp->value);

		if (!columns) {
			fputs(line, fp);
			fputc('\n', fp);
			++nlines;
		} else
		for (pos = 0; pos < fill; pos += todo) {
			todo = pos ? 79 : 80;
			if (pos + todo > fill)
				todo = fill - pos;
			if (pos)
				fputc(' ', fp);
			if (fwrite(line+pos, todo, 1, fp) < 0)
				elog(LOG_ERR, errno, "fwrite");
			fputc('\n', fp);
			++nlines;
		}
	}

	/* write child objects */
	for (child = vobject_first_child(vc); child; child = vobject_next_child(child))
		nlines += vobject_write2(child, fp, columns);

	/* terminate vobject */
	fprintf(fp, "END:%s\n", vc->type);
	++nlines;
	return nlines;
}

int vobject_write(const struct vobject *vc, FILE *fp)
{
	return vobject_write2(vc, fp, 80);
}

static struct vprop *vprop_dup(const struct vprop *src)
{
	struct vprop *dst;
	int linelen;

	linelen = (src->value - src->key) + strlen(src->value ?: "");
	/* duplicate memory */
	dst = zalloc(sizeof(*dst) + linelen+2);
	memcpy(dst->key, src->key, linelen+2);
	/* set value & meta properly */
	if (src->value)
		dst->value = dst->key + (src->value - src->key);
	if (src->meta)
		dst->meta = dst->key + (src->meta - src->key);
	return dst;
}

struct vobject *vobject_dup_root(const struct vobject *src)
{
	struct vobject *dst;
	const struct vprop *prop;

	dst = zalloc(sizeof(*dst));
	dst->type = strdup(src->type);

	for (prop = vobject_props(src); prop; prop = vprop_next(prop))
		vprop_attach(vprop_dup(prop), dst);
	return dst;
}

struct vobject *vobject_dup(const struct vobject *src)
{
	struct vobject *dst, *sub;

	dst = vobject_dup_root(src);
	for (src = vobject_first_child(src); src; src = vobject_next_child(src)) {
		sub = vobject_dup(src);
		vobject_attach(sub, dst);
	}
	return dst;
}
