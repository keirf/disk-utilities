#ifndef __PRIVATE_UTIL_H__
#define __PRIVATE_UTIL_H__

#define cyl(trk) ((trk)/2)
#define hd(trk) ((trk)&1)

#define trk_warn(ti,trk,msg,a...)                           \
    printf("*** T%u.%u: %s: " msg "\n", cyl(trk), hd(trk),  \
           (ti)->typename, ## a)

#endif /* __PRIVATE_UTIL_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
