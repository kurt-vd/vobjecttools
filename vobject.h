#ifndef _VOBJECT_H_
#define _VOBJECT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* struct holding a complete VCard */
struct vobject;
/* struct holding 1 property of a VCard */
struct vprop;

/* access the application private member */
extern void vobject_set_priv(struct vobject *vc, void *dat);
extern void *vobject_get_priv(struct vobject *vc);

/* access the type (VCALENDAR, VCARD, VEVENT, ... ) */
extern const char *vobject_type(struct vobject *vc);
/*
 * vprop walk functions
 * vobject_props() retrieves the first property
 * vprop_next() retrieves subsequent ones
 */
extern struct vprop *vobject_props(struct vobject *vc);
extern struct vprop *vprop_next(struct vprop *vp);

/* access the vprop attributes */
extern const char *vprop_name(struct vprop *vp);
extern const char *vprop_value(struct vprop *vp);
/* walk through vprop meta data (start with @str == NULL) */
extern const char *vprop_next_meta(struct vprop *vp, const char *str);

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
 * Immediate lookup functions
 * Only the first property of equally named properties is accessible
 * Only the value is accessible
 */
extern const char *vobject_prop(struct vobject *vc, const char *propname);

/* read next vobject from file */
extern struct vobject *vobject_next(FILE *fp, int *linenr);

/* write vobjects */
extern int vobject_write(const struct vobject *vc, FILE *fp);

/* free a vobject */
extern void vobject_free(struct vobject *vc);

/* create lowercase copy (cached) of a string */
extern const char *lowercase(const char *str);

#ifdef __cplusplus
}
#endif
#endif
