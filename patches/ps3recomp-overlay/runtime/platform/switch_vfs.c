#include "switch_vfs.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

const Ps3SwitchVfsRoots ps3_switch_vfs_default_roots = {
    "sdmc:/switch/dbz-budokai-3-switch/game",
    "sdmc:/switch/dbz-budokai-3-switch/hdd0"
};

static const char *mount_suffix(const char *guest, const char *mount)
{
    size_t length = strlen(mount);
    if (strncmp(guest, mount, length) != 0) return NULL;
    if (guest[length] == '\0') return guest + length;
    if (guest[length] == '/') return guest + length + 1;
    return NULL;
}

int ps3_switch_vfs_is_supported(const char *guest)
{
    return guest && (mount_suffix(guest, "/dev_bdvd") ||
                     mount_suffix(guest, "/app_home") ||
                     mount_suffix(guest, "/dev_hdd0"));
}

int ps3_switch_vfs_translate(const Ps3SwitchVfsRoots *roots,
                             const char *guest, char *host, size_t host_size)
{
    if (!roots || !guest || !host || host_size == 0) return -1;
    host[0] = '\0';
    const char *root = NULL;
    const char *suffix = mount_suffix(guest, "/dev_bdvd");
    if (suffix) root = roots->disc;
    if (!suffix) {
        suffix = mount_suffix(guest, "/app_home");
        if (suffix) root = roots->disc;
    }
    if (!suffix) {
        suffix = mount_suffix(guest, "/dev_hdd0");
        if (suffix) root = roots->hdd0;
    }
    if (!suffix || !root || !*root) return -1;

    size_t length = strlen(root);
    if (length >= host_size) return -1;
    memcpy(host, root, length + 1);
    while (length > 0 && host[length - 1] == '/' &&
           !(length >= 2 && host[length - 2] == ':'))
        host[--length] = '\0';

    while (*suffix) {
        const char *component = suffix;
        while (*suffix && *suffix != '/') {
            if (*suffix == '\\' || *suffix == ':') goto invalid;
            ++suffix;
        }
        size_t count = (size_t)(suffix - component);
        if (count == 0 || (count == 1 && component[0] == '.')) {
            if (*suffix) ++suffix;
            continue;
        }
        if (count == 2 && component[0] == '.' && component[1] == '.')
            goto invalid;
        size_t separator = length && host[length - 1] != '/' ? 1 : 0;
        if (length + separator >= host_size ||
            count >= host_size - length - separator) goto invalid;
        if (separator) host[length++] = '/';
        memcpy(host + length, component, count);
        length += count;
        host[length] = '\0';
        if (*suffix) ++suffix;
    }
    /* Game files in full NSPs live in RomFS. Look there first for each file;
     * keep the SD mapping as the development and missing-file fallback. */
    if (roots == &ps3_switch_vfs_default_roots &&
        root == roots->disc &&
        strncmp(host, roots->disc, strlen(roots->disc)) == 0) {
        char candidate[1024];
        int n = snprintf(candidate, sizeof candidate, "romfs:/game%s",
                         host + strlen(roots->disc));
        struct stat entry;
        if (n > 0 && (size_t)n < sizeof candidate &&
            (size_t)n < host_size && stat(candidate, &entry) == 0)
            memcpy(host, candidate, (size_t)n + 1);
    }
    return 0;

invalid:
    host[0] = '\0';
    return -1;
}
