#ifndef PS3RECOMP_SWITCH_VFS_H
#define PS3RECOMP_SWITCH_VFS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ps3SwitchVfsRoots {
    const char *disc;
    const char *hdd0;
} Ps3SwitchVfsRoots;

/* Host roots contain the contents of the PS3 device, not the device name. */
extern const Ps3SwitchVfsRoots ps3_switch_vfs_default_roots;

int ps3_switch_vfs_is_supported(const char *guest);

/* Returns zero on success; rejects unknown mounts, traversal and truncation. */
int ps3_switch_vfs_translate(const Ps3SwitchVfsRoots *roots,
                             const char *guest, char *host, size_t host_size);

#ifdef __cplusplus
}
#endif

#endif
