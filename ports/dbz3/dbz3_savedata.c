/*
 * DBZ3 Switch: where the PS3 save data (cellSaveData) lives.
 *
 * 1. The SD copy: sdmc:/switch/dbz-budokai-3-switch/hdd0/home/00000001/savedata,
 *    next to the rest of the port's files and inside the emulated dev_hdd0.
 *    Builds before 2026-11-02 saved to ./gamedata/dev_hdd0/... relative to
 *    the working directory (the SD root for an installed NSP); those saves
 *    are moved here once.
 * 2. The Switch save data of the user who started the game (like installed
 *    games: Data Management, JKSV). It is created on first use (32 MiB,
 *    16 MiB journal) and filled once with a copy of the SD saves, which stay
 *    on the SD as a backup. Writes are committed after every save operation.
 *    If no user is available or the save cannot be mounted, the SD copy is
 *    used. sdmc:/switch/dbz-budokai-3-switch/savedata-sd forces the SD copy.
 *
 * cellSaveData.c asks for the root once (ps3_port_savedata_root) and calls
 * ps3_port_savedata_commit after each save.
 */
#include <switch.h>

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PORT_DIR "sdmc:/switch/dbz-budokai-3-switch"
#define SD_ROOT PORT_DIR "/hdd0/home/00000001/savedata"
#define OLD_ROOT "./gamedata/dev_hdd0/home/00000001/savedata"
#define SAVE_DEV "dbz3save"
#define SAVE_ROOT SAVE_DEV ":/savedata"
#define SAVE_SIZE (32ull << 20)
#define SAVE_JOURNAL (16ull << 20)

static char s_root[256];
static int s_system;   /* 1: SAVE_ROOT on the Switch save data */

static void slog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void slog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[savedata] ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    FILE *f = fopen(PORT_DIR "/savedata.log", "a");
    if (f) {
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fclose(f);
    }
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void make_dirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp; *p; p++) {
        if (*p != '/' || p == tmp || p[-1] == ':') continue;
        *p = '\0';
        mkdir(tmp, 0777);
        *p = '/';
    }
    mkdir(tmp, 0777);
}

/* Copies src into dst (both directories), skipping entries dst already has.
 * With commit set, the save device is committed after every file so the
 * journal never has to hold more than one file. Returns the files copied or
 * -1 on an error. */
static int copy_tree(const char *src, const char *dst, int commit)
{
    DIR *d = opendir(src);
    if (!d) return -1;
    make_dirs(dst);
    int copied = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char s[512], t[512];
        snprintf(s, sizeof s, "%s/%s", src, e->d_name);
        snprintf(t, sizeof t, "%s/%s", dst, e->d_name);
        if (is_dir(s)) {
            const int n = copy_tree(s, t, commit);
            if (n < 0) { closedir(d); return -1; }
            copied += n;
            continue;
        }
        if (exists(t)) continue;
        FILE *in = fopen(s, "rb");
        FILE *out = in ? fopen(t, "wb") : NULL;
        int ok = in && out;
        static char buf[64 * 1024];
        size_t n;
        while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0)
            ok = fwrite(buf, 1, n, out) == n;
        if (in) fclose(in);
        if (out && fclose(out) != 0) ok = 0;
        if (!ok) { slog("copy failed: %s -> %s\n", s, t); closedir(d); return -1; }
        if (commit) fsdevCommitDevice(SAVE_DEV);
        copied++;
    }
    closedir(d);
    return copied;
}

static void remove_tree(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char s[512];
            snprintf(s, sizeof s, "%s/%s", path, e->d_name);
            if (is_dir(s)) remove_tree(s);
            else remove(s);
        }
        closedir(d);
    }
    rmdir(path);
}

/* Step 1: saves of earlier builds into the SD root. Each save directory is
 * moved (renamed) when the new root does not have it yet; the old tree is
 * removed once everything is across. */
static void migrate_old_sd_saves(void)
{
    make_dirs(SD_ROOT);
    if (!is_dir(OLD_ROOT)) return;
    DIR *d = opendir(OLD_ROOT);
    if (!d) return;
    int moved = 0, left = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char s[512], t[512];
        snprintf(s, sizeof s, "%s/%s", OLD_ROOT, e->d_name);
        snprintf(t, sizeof t, "%s/%s", SD_ROOT, e->d_name);
        if (exists(t)) { left++; continue; }
        if (rename(s, t) == 0) { moved++; continue; }
        /* rename across the relative and absolute paths failed: copy */
        if (is_dir(s) && copy_tree(s, t, 0) >= 0) { remove_tree(s); moved++; }
        else left++;
    }
    closedir(d);
    slog("old saves in " OLD_ROOT ": %d moved to " SD_ROOT ", %d left\n", moved, left);
    if (!left) {
        rmdir(OLD_ROOT);
        rmdir("./gamedata/dev_hdd0/home/00000001");
        rmdir("./gamedata/dev_hdd0/home");
        rmdir("./gamedata/dev_hdd0");
        rmdir("./gamedata");
    }
}

static int sd_flag(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, PORT_DIR "/%s", name);
    return exists(path);
}

/* Step 2: mount (and create on first use) the Switch save data of the
 * current user. */
static int mount_system_save(void)
{
    if (sd_flag("savedata-sd")) { slog("savedata-sd: SD saves forced\n"); return 0; }
    AccountUid uid = { 0 };
    Result rc = accountInitialize(AccountServiceType_Application);
    if (R_FAILED(rc)) { slog("account init failed 0x%X\n", rc); return 0; }
    rc = accountGetPreselectedUser(&uid);
    accountExit();
    if (R_FAILED(rc) || !accountUidIsValid(&uid)) {
        PselUserSelectionSettings settings;
        memset(&settings, 0, sizeof settings);
        rc = pselShowUserSelector(&uid, &settings);
        if (R_FAILED(rc) || !accountUidIsValid(&uid)) {
            slog("no user selected (0x%X)\n", rc);
            return 0;
        }
    }
    u64 program_id = 0;
    rc = svcGetInfo(&program_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc) || !program_id) { slog("program id unknown 0x%X\n", rc); return 0; }
    rc = fsdevMountSaveData(SAVE_DEV, program_id, uid);
    if (R_FAILED(rc)) {
        FsSaveDataAttribute attr;
        memset(&attr, 0, sizeof attr);
        attr.application_id = program_id;
        attr.uid = uid;
        attr.save_data_type = FsSaveDataType_Account;
        FsSaveDataCreationInfo info;
        memset(&info, 0, sizeof info);
        info.save_data_size = SAVE_SIZE;
        info.journal_size = SAVE_JOURNAL;
        info.available_size = 0x4000;
        info.owner_id = program_id;
        info.save_data_space_id = FsSaveDataSpaceId_User;
        FsSaveDataMetaInfo meta;
        memset(&meta, 0, sizeof meta);
        meta.size = 0x40060;
        meta.type = FsSaveDataMetaType_Thumbnail;
        const Result created = fsCreateSaveDataFileSystem(&attr, &info, &meta);
        slog("created Switch save data for %016llX: 0x%X\n", (unsigned long long)program_id, created);
        rc = fsdevMountSaveData(SAVE_DEV, program_id, uid);
    }
    if (R_FAILED(rc)) { slog("mount failed 0x%X\n", rc); return 0; }
    return 1;
}

const char *ps3_port_savedata_root(void)
{
    if (s_root[0]) return s_root;
    migrate_old_sd_saves();
    snprintf(s_root, sizeof s_root, "%s", SD_ROOT);
    if (mount_system_save()) {
        if (!is_dir(SAVE_ROOT)) {
            /* First use: bring the SD saves over (they stay on the SD). */
            const int n = copy_tree(SD_ROOT, SAVE_ROOT, 1);
            make_dirs(SAVE_ROOT);
            fsdevCommitDevice(SAVE_DEV);
            slog("first use of the Switch save data: %d files copied from the SD\n", n);
        }
        if (is_dir(SAVE_ROOT)) {
            snprintf(s_root, sizeof s_root, "%s", SAVE_ROOT);
            s_system = 1;
        } else {
            fsdevUnmountDevice(SAVE_DEV);
        }
    }
    slog("save root: %s\n", s_root);
    return s_root;
}

void ps3_port_savedata_commit(void)
{
    if (!s_system) return;
    const Result rc = fsdevCommitDevice(SAVE_DEV);
    if (R_FAILED(rc)) slog("commit failed 0x%X\n", rc);
}
