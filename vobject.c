#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>

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
		/* IMPORTANT: sub & lastsub order must match parents 'props & proplast' */
		struct vprop *next, *prev;
		struct vprop *up;
		/* metadata as properties */
		struct vprop *sub, *lastsub;

		char *value;
		/* key may be used to iterate */
		char key[8];
	} *props, *proplast;
	/* hierarchy */
	struct vobject *next, *prev;
	struct vobject *list, *listlast, *parent;
	/* members to be used by application */
	void *priv;
};

#define usertovprop(str) ((struct vprop *)((str)-offsetof(struct vprop, key)))
#define vproptouser(vprop)	((vprop) ? (vprop)->key : NULL)

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
const char *vobject_first_prop(const struct vobject *vc)
{
	return vproptouser(vc->props);
}

const char *vprop_first_meta(const char *str)
{
	return vproptouser(usertovprop(str)->sub);
}

const char *vprop_next(const char *key)
{
	return vproptouser(usertovprop(key)->next);
}

/* access vprop attributes */
const char *vprop_value(const char *key)
{
	return usertovprop(key)->value;
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

const char *vprop_meta(const char *prop, const char *metaname)
{
	const char *key;

	for (key = vprop_first_meta(prop); key; key = vprop_next(key)) {
		if (!strcasecmp(key, metaname))
			return vprop_value(key) ?: "";
	}
	return NULL;
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
	if (vp->up && vp == vp->up->sub)
		vp->up->sub = vp->next;
	if (vp->up && vp == vp->up->lastsub)
		vp->up->lastsub = vp->prev;
	/* linked list detach */
	if (vp->prev)
		vp->prev->next = vp->next;
	if (vp->next)
		vp->next->prev = vp->prev;
	vp->prev = vp->next = vp->up = NULL;
}

static void vprop_attach_vprop(struct vprop *vp, struct vprop *parent)
{
	vprop_detach(vp);
	vp->prev = parent->lastsub;
	if (vp->prev)
		vp->prev->next = vp;
	else
		parent->sub = vp;
	parent->lastsub = vp;
	vp->up = parent;
}

static void vprop_attach(struct vprop *vp, struct vobject *vo)
{
	/* give a fake parent vprop pointer from vobject,
	 * so that vprop->sub actually points to vobject->props
	 */
	vprop_attach_vprop(vp, (struct vprop *)(((char *)&((vo)->props))-offsetof(struct vprop, sub)));
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
static void vprop_free(struct vprop *vp)
{
	vprop_detach(vp);
	while (vp->sub)
		vprop_free(vp->sub);
	if (vp->value)
		free(vp->value);
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

/* FILE INPUT */
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

static struct vprop *mkvprop(const char *key, const char *value)
{
	struct vprop *vp;

	vp = zalloc(sizeof(*vp) + strlen(key));
	strcpy(vp->key, key);

	if (value)
		vp->value = strdup(value);
	if (value && !vp->value)
		elog(LOG_ERR, errno, "strdup");
	return vp;
}

static struct vprop *strtovprop(char *line)
{
	struct vprop *vp;
	char *value, *meta, *next, *end;

	/* seperate value from key */
	value = strchresc(line, ':');
	if (value)
		*value++ = 0;

	/* seperate meta from key */
	meta = strchresc(line, ';');
	if (meta)
		*meta++ = 0;

	/* create vprop */
	vp = mkvprop(line, value);

	for (; meta; meta = next) {
		next = strchresc(meta, ';');
		if (next)
			*next++ = 0;
		value = strchresc(meta, '=');
		if (value) {
			*value++ = 0;
			end = value + strlen(value)-1;
			if (strchr("\"'", *value) && (*value == *end)) {
				++value;
				*end = 0;
			}
		}
		vprop_attach_vprop(mkvprop(meta, value), vp);
	}
	return vp;
}

/* read next vobject from file */
struct vobject *vobject_next(FILE *fp, int *linenr)
{
	char *line = NULL, *saved = NULL;
	size_t linesize = 0, savedsize = 0, savedlen = 0;
	int ret, mylinenr = 0;
	struct vobject *vc = NULL;
	struct vprop *vp;

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
			if (vc) {
				vp = strtovprop(saved);
				if (vp)
					vprop_attach(vp, vc);
			}
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
int vobject_write2(const struct vobject *vc, FILE *fp, int flags)
{
	int nlines = 0;
	struct vprop *vp, *meta;
	char *line = NULL;
	size_t linesize = 0, fill, pos, todo;
	const struct vobject *child;

	fprintf(fp, "BEGIN:%s\n", vc->type);
	++nlines;

	/* iterate over all properties */
	for (vp = vc->props; vp; vp = vp->next) {
		fill = appendprintf(&line, &linesize, 0, "%s", vp->key);
		for (meta = vp->sub; meta; meta = meta->next)
			fill += appendprintf(&line, &linesize, fill,
					strpbrk(meta->value, ":;") ? ";%s=\"%s\"" : ";%s=%s",
					meta->key, meta->value);
		fill += appendprintf(&line, &linesize, fill, ":%s", vp->value);

		if (flags & VOF_NOBREAK) {
			fputs(line, fp);
			fputc('\n', fp);
			++nlines;
		} else
		for (pos = 0; pos < fill; pos += todo) {
			todo = pos ? 79 : 80;
			if (pos + todo >= fill)
				todo = fill - pos;
			else if (flags & VOF_UTF8) {
				for (; todo > 72; --todo) {
					if ((line[pos+todo] & 0xc0) != 0x80)
						/* next byte is a start sequence */
						break;
				}
			}
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
		nlines += vobject_write2(child, fp, flags);

	/* terminate vobject */
	fprintf(fp, "END:%s\n", vc->type);
	++nlines;
	return nlines;
}

int vobject_write(const struct vobject *vc, FILE *fp)
{
	return vobject_write2(vc, fp, 0);
}

static struct vprop *vprop_dup(const struct vprop *src)
{
	struct vprop *dst;
	struct vprop *vp;

	/* duplicate memory */
	dst = zalloc(sizeof(*dst) + strlen(src->key));
	strcpy(dst->key, src->key);
	/* set value & meta properly */
	if (src->value)
		dst->value = strdup(src->value);
	for (vp = src->sub; vp; vp = vp->next)
		vprop_attach_vprop(vprop_dup(vp), dst);
	return dst;
}

struct vobject *vobject_dup_root(const struct vobject *src)
{
	struct vobject *dst;
	const struct vprop *prop;

	dst = zalloc(sizeof(*dst));
	dst->type = strdup(src->type);

	for (prop = src->props; prop; prop = prop->next) 
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

/* VPROP manipulation */
void vprop_remove(const char *prop)
{
	struct vprop *vprop = usertovprop(prop);

	vprop_detach(vprop);
	vprop_free(vprop);
}
