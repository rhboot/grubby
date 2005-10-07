#ifndef _NASH_MOUNT_BY_LABEL_H
#define _NASH_MOUNT_BY_LABEL_H 1

extern int display_uuid_cache(char *cmd, char *end);
extern char *get_spec_by_uuid(const char *uuid, int *major, int *minor);
extern char *get_spec_by_volume_label(const char *volumelabel, int *major,
        int *minor);

#endif /* _NASH_MOUNT_BY_LABEL_H */
