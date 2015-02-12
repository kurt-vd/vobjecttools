#ifndef _VOBJECT_H_
#define _VOBJECT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* struct holding a complete VCard */
struct vobject;

/* access the application private member */
extern void vobject_set_priv(struct vobject *vc, void *dat);
extern void *vobject_get_priv(const struct vobject *vc);

/* access the type (VCALENDAR, VCARD, VEVENT, ... ) */
extern const char *vobject_type(const struct vobject *vc);
/*
 * vprop walk functions
 * vobject_first_prop() retrieves the first property
 * vprop_first_prop() does similar for sub-properties
 * vprop_next() retrieves subsequent ones
 */
extern const char *vobject_first_prop(const struct vobject *vc);
/* start walking through vprop meta data */
extern const char *vprop_first_meta(const char *str);
extern const char *vprop_next(const char *str);

/* access the vprop attributes */
extern const char *vprop_value(const char *str);

/* control hierarchy:
 *
 * vobject_first returns the first child vobject of a parent
 * vobject_next returns the next sibling of a child
 */
extern struct vobject *vobject_first_child(const struct vobject *parent);
extern struct vobject *vobject_next_child(const struct vobject *prevchild);

/* manually attach/detach a child to/from a parent */
extern void vobject_attach(struct vobject *obj, struct vobject *parent);
extern void vobject_detach(struct vobject *vo);

/*
 * Immediate lookup (value!) functions
 * Only the first property of equally named properties is accessible
 * Only the value is accessible
 */
extern const char *vobject_prop(const struct vobject *vc, const char *propname);

/*
 * Lookup metadata value immediate
 *
 * Returns
 * - the value when present
 * - an empty string when metadata found without value
 * - NULL when not found
 */
extern const char *vprop_meta(const char *prop, const char *metaname);

/* FILE IO */

/* read next vobject from file */
extern struct vobject *vobject_next(FILE *fp, int *linenr);

/* write vobjects */
extern int vobject_write(const struct vobject *vc, FILE *fp);
extern int vobject_write2(const struct vobject *vc, FILE *fp, int flags);
#define VOF_NOBREAK	0x01 /* allow lines >80 characters */
#define VOF_UTF8	0x02 /* break lines on UTF8 start charachters */

/* free a vobject */
extern void vobject_free(struct vobject *vc);

/* duplication */
extern struct vobject *vobject_dup(const struct vobject *vobj);
/* duplicate, without recursion */
extern struct vobject *vobject_dup_root(const struct vobject *vobj);

/* create lowercase copy (cached) of a string */
extern const char *lowercase(const char *str);

#ifdef __cplusplus
}
#endif
#endif
