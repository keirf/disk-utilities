#ifndef __PRIVATE_UTIL_H__
#define __PRIVATE_UTIL_H__

#define FALSE 0
#define TRUE 1

#define track_usecs_from_rpm(rpm) (200000u*300u/(rpm))
#define track_nsecs_from_rpm(rpm) (track_usecs_from_rpm(rpm) * 1000u)

#define cyl(trk) ((trk)/2)
#define hd(trk) ((trk)&1)

#define trk_warn(ti,trk,msg,a...)                                   \
    fprintf(stderr, "*** T%u.%u: %s: " msg "\n", cyl(trk), hd(trk), \
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
