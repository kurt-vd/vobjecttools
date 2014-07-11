#ifndef _VCARD_H_
#define _VCARD_H_

#ifdef __cplusplus
extern "C" {
#endif

/* struct holding a complete VCard */
struct vcard;
/* struct holding 1 property of a VCard */
struct vprop;

/* access the application private member */
extern void vcard_set_priv(struct vcard *vc, void *dat);
extern void *vcard_get_priv(struct vcard *vc);

/*
 * vprop walk functions
 * vcard_props() retrieves the first property
 * vprop_next() retrieves subsequent ones
 */
extern struct vprop *vcard_props(struct vcard *vc);
extern struct vprop *vprop_next(struct vprop *vp);

/* access the vprop attributes */
extern const char *vprop_name(struct vprop *vp);
extern const char *vprop_value(struct vprop *vp);
/* walk through vprop meta data (start with @str == NULL) */
extern const char *vprop_next_meta(struct vprop *vp, const char *str);

/*
 * Immediate lookup functions
 * Only the first property of equally named properties is accessible
 * Only the value is accessible
 */
extern const char *vcard_prop(struct vcard *vc, const char *propname);

/* read next vcard from file */
extern struct vcard *vcard_next(FILE *fp, int *linenr);

/* write vcards */
extern int vcard_write(const struct vcard *vc, FILE *fp);

/* free a vcard */
extern void vcard_free(struct vcard *vc);

#ifdef __cplusplus
}
#endif
#endif
