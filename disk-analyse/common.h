#ifndef __MFMPARSE_COMMON_H__
#define __MFMPARSE_COMMON_H__

struct format_list {
    uint16_t nr, max, pos;
    uint16_t ent[1];
};

extern struct format_list **parse_config(char *config, char *specifier);

extern int quiet, verbose;

#endif /* __MFMPARSE_COMMON_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
