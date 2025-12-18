#ifndef FOREST_TOOLBOX_H
#define FOREST_TOOLBOX_H

#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/time.h"

#ifndef O_RDONLY
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_END 2
#endif

static int ftb_read_file(const char *path, char *buf, int max)
{
    if (!path || !buf || max <= 1) {
        return -1;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    int n = read(fd, buf, (size_t)(max - 1));
    if (n < 0) {
        n = 0;
    }
    buf[n] = '\0';
    close(fd);
    return n;
}

static void ftb_write_stub(const char *label, const char *path, const char *payload)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0 && payload) {
        write(fd, payload, strlen(payload));
        close(fd);
    }
    printf("%s updated %s\n", label, path);
}

static void ftb_append_log(const char *path, const char *entry)
{
    int fd = open(path, O_WRONLY | O_CREAT);
    if (fd < 0) {
        return;
    }
    lseek(fd, 0, SEEK_END);
    write(fd, entry, strlen(entry));
    write(fd, "\n", 1);
    close(fd);
}

static int ftb_extract_kv(const char *source, const char *key)
{
    if (!source || !key) {
        return -1;
    }
    const char *p = strstr(source, key);
    if (!p) {
        return -1;
    }
    while (*p && *p != '\n' && (*p < '0' || *p > '9')) {
        p++;
    }
    return atoi(p);
}

static int ftb_mem_total(void)
{
    char buf[1024];
    if (ftb_read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
        return ftb_extract_kv(buf, "MemTotal");
    }
    return 0;
}

static int ftb_mem_free(void)
{
    char buf[1024];
    if (ftb_read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
        return ftb_extract_kv(buf, "MemAvailable");
    }
    return 0;
}

static int ftb_uptime_seconds(void)
{
    char buf[128];
    if (ftb_read_file("/proc/uptime", buf, sizeof(buf)) > 0) {
        return atoi(buf);
    }
    int now = time(0);
    return now > 0 ? now : 0;
}

static int ftb_proc_running(void)
{
    char buf[128];
    if (ftb_read_file("/proc/loadavg", buf, sizeof(buf)) > 0) {
        char *slash = strchr(buf, '/');
        if (slash) {
            return atoi(slash + 1);
        }
    }
    return 1;
}

static int ftb_socket_count(void)
{
    net_socket_info_t sockets[8];
    int got = netinfo(sockets, 8);
    if (got > 0) {
        return got;
    }
    return 0;
}

static void ftb_print_basic_header(const char *tool)
{
    printf("%s (Forest mini-toolbox)\n", tool);
}

static void ftb_show_memtable(void)
{
    int total = ftb_mem_total();
    int free = ftb_mem_free();
    if (total <= 0) {
        total = 1024;
    }
    if (free < 0) {
        free = 0;
    }
    int used = total - free;
    printf("Mem: total=%d kB used=%d kB free=%d kB\n", total, used, free);
}

static void ftb_command_htop(const char *name)
{
    ftb_print_basic_header(name);
    int up = ftb_uptime_seconds();
    printf("Uptime: %d seconds\n", up);
    ftb_show_memtable();
    printf("Tasks: running=%d sockets=%d\n", ftb_proc_running(), ftb_socket_count());
    printf("Load: data sourced from /proc and loopback netinfo\n");
}

static void ftb_command_vmstat(void)
{
    ftb_print_basic_header("vmstat");
    ftb_show_memtable();
    printf("Uptime (s): %d\n", ftb_uptime_seconds());
}

static void ftb_command_iostat(void)
{
    char buf[512];
    if (ftb_read_file("/proc/diskstats", buf, sizeof(buf)) > 0) {
        printf("Diskstats (first entry):\n");
        char *line = strtok(buf, "\n");
        if (line) {
            printf("%s\n", line);
        }
    } else {
        printf("iostat: no disk statistics available\n");
    }
}

static void ftb_command_uptime(void)
{
    printf("Uptime: %d seconds\n", ftb_uptime_seconds());
}

static void ftb_command_watch(void)
{
    for (int i = 0; i < 3; i++) {
        printf("[watch] tick %d uptime=%d\n", i + 1, ftb_uptime_seconds());
        struct timespec ts;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        nanosleep(&ts, 0);
    }
}

static void ftb_command_log_dump(const char *path, const char *label)
{
    char buf[1024];
    int n = ftb_read_file(path, buf, sizeof(buf));
    printf("%s from %s\n", label, path);
    if (n > 0) {
        printf("%s\n", buf);
    } else {
        printf("no log entries available\n");
    }
}

static void ftb_command_editor(const char *tool)
{
    char path[64];
    sprintf(path, "/tmp/%s.txt", tool);
    char buf[512];
    int n = ftb_read_file(path, buf, sizeof(buf));
    if (n > 0) {
        printf("Existing %s buffer:%s\n", tool, n ? "" : " (empty)");
        printf("%s\n", buf);
    }
    ftb_append_log(path, "-- edited by Forest toolbox --");
    printf("Appended a marker line to %s\n", path);
}

static void ftb_command_diff(void)
{
    char a[512];
    char b[512];
    int na = ftb_read_file("/README.md", a, sizeof(a));
    int nb = ftb_read_file("/README.txt", b, sizeof(b));
    printf("diff /README.md /README.txt\n");
    if (na <= 0 || nb <= 0) {
        printf("unable to load one of the files\n");
        return;
    }
    if (strcmp(a, b) == 0) {
        printf("files are identical\n");
        return;
    }
    printf("files differ; showing first 120 chars of each:\n");
    a[120] = '\0';
    b[120] = '\0';
    printf("- %s\n+ %s\n", a, b);
}

static void ftb_command_patch(void)
{
    ftb_write_stub("patch", "/tmp/forest.patch", "patched content placeholder");
}

static void ftb_command_file(void)
{
    char buf[256];
    int n = ftb_read_file("/README.md", buf, sizeof(buf));
    if (n <= 0) {
        printf("file: unable to inspect /README.md\n");
        return;
    }
    int printable = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] >= 32 && buf[i] < 127) {
            printable++;
        }
    }
    if (printable == n) {
        printf("/README.md: ASCII text (%d bytes)\n", n);
    } else {
        printf("/README.md: binary data (%d bytes)\n", n);
    }
}

static void ftb_command_mkfs(void)
{
    const char *path = "/tmp/forestfs.img";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("mkfs: unable to create %s\n", path);
        return;
    }
    char zero = 0;
    for (int i = 0; i < 1024 * 1024; i++) {
        write(fd, &zero, 1);
    }
    close(fd);
    printf("mkfs: initialized 1MiB image at %s\n", path);
}

static void ftb_command_fsck(void)
{
    char buf[64];
    int n = ftb_read_file("/tmp/forestfs.img", buf, sizeof(buf));
    printf("fsck: %s\n", n >= 0 ? "image looks present" : "no filesystem image found");
}

static void ftb_command_lsblk(void)
{
    char buf[512];
    if (ftb_read_file("/proc/partitions", buf, sizeof(buf)) > 0) {
        printf("lsblk:\n%s\n", buf);
    } else {
        printf("lsblk: partitions unavailable\n");
    }
}

static void ftb_command_blkid(void)
{
    char buf[512];
    if (ftb_read_file("/proc/mounts", buf, sizeof(buf)) > 0) {
        printf("blkid (mount table):\n%s\n", buf);
    } else {
        printf("blkid: mounts unavailable\n");
    }
}

static void ftb_command_dd(void)
{
    char src[512];
    int n = ftb_read_file("/README.md", src, sizeof(src));
    if (n <= 0) {
        printf("dd: missing /README.md\n");
        return;
    }
    const char *out = "/tmp/README.copy";
    ftb_write_stub("dd", out, src);
}

static void ftb_command_part_table(const char *tool)
{
    printf("%s: showing kernel partitions\n", tool);
    ftb_command_lsblk();
}

static void ftb_service_write(const char *svc, const char *state)
{
    char line[128];
    sprintf(line, "%s %s", svc, state);
    ftb_append_log("/tmp/forest-services", line);
}

static void ftb_command_service(const char *name)
{
    ftb_service_write(name, "active");
    printf("service %s marked active in /tmp/forest-services\n", name);
}

static void ftb_command_bootctl(const char *tool)
{
    char buf[512];
    int n = ftb_read_file("/boot/grub/grub.cfg", buf, sizeof(buf));
    printf("%s: %s\n", tool, n > 0 ? "boot entries detected" : "no grub.cfg present");
}

static void ftb_command_scheduler(const char *tool)
{
    char line[128];
    sprintf(line, "%s task queued at %d", tool, ftb_uptime_seconds());
    ftb_append_log("/tmp/forest-cron", line);
    printf("queued %s task -> /tmp/forest-cron\n", tool);
}

static void ftb_command_passwd_like(const char *tool)
{
    char line[128];
    sprintf(line, "%s updated credentials", tool);
    ftb_append_log("/tmp/forest-passwd", line);
    printf("%s: credentials database noted\n", tool);
}

static void ftb_command_label(const char *tool)
{
    char line[128];
    sprintf(line, "%s applied default security context", tool);
    ftb_append_log("/tmp/forest-selinux", line);
    printf("%s: wrote security context entry\n", tool);
}

static void ftb_command_crypto(const char *tool)
{
    const char *in = "/README.md";
    char buf[512];
    int n = ftb_read_file(in, buf, sizeof(buf));
    if (n <= 0) {
        printf("%s: unable to read %s\n", tool, in);
        return;
    }
    for (int i = 0; i < n; i++) {
        buf[i] ^= 0x5A;
    }
    char out[64];
    sprintf(out, "/tmp/%s.out", tool);
    ftb_write_stub(tool, out, buf);
}

static void ftb_command_builder(const char *tool)
{
    printf("%s: Forest toolchain shim active. Use host tool if available.\n", tool);
}

static void ftb_command_doc(const char *tool)
{
    char buf[512];
    if (ftb_read_file("/README.md", buf, sizeof(buf)) > 0) {
        printf("%s: displaying project README snippet:\n%s\n", tool, buf);
    } else {
        printf("%s: no docs found\n", tool);
    }
}

static void ftb_run_tool(const char *tool)
{
    if (!tool) {
        return;
    }
    if (!strcmp(tool, "htop") || !strcmp(tool, "top")) { ftb_command_htop(tool); return; }
    if (!strcmp(tool, "free")) { ftb_show_memtable(); return; }
    if (!strcmp(tool, "vmstat")) { ftb_command_vmstat(); return; }
    if (!strcmp(tool, "iostat")) { ftb_command_iostat(); return; }
    if (!strcmp(tool, "uptime")) { ftb_command_uptime(); return; }
    if (!strcmp(tool, "watch")) { ftb_command_watch(); return; }
    if (!strcmp(tool, "dmesg")) { ftb_command_log_dump("/var/log/dmesg", "dmesg"); return; }
    if (!strcmp(tool, "journalctl") || !strcmp(tool, "syslog")) { ftb_command_log_dump("/var/log/syslog", tool); return; }
    if (!strcmp(tool, "vi") || !strcmp(tool, "vim") || !strcmp(tool, "nvim") || !strcmp(tool, "nano") || !strcmp(tool, "ed")) { ftb_command_editor(tool); return; }
    if (!strcmp(tool, "diff")) { ftb_command_diff(); return; }
    if (!strcmp(tool, "patch")) { ftb_command_patch(); return; }
    if (!strcmp(tool, "file")) { ftb_command_file(); return; }
    if (!strcmp(tool, "mkfs")) { ftb_command_mkfs(); return; }
    if (!strcmp(tool, "fsck")) { ftb_command_fsck(); return; }
    if (!strcmp(tool, "lsblk")) { ftb_command_lsblk(); return; }
    if (!strcmp(tool, "blkid")) { ftb_command_blkid(); return; }
    if (!strcmp(tool, "dd")) { ftb_command_dd(); return; }
    if (!strcmp(tool, "parted") || !strcmp(tool, "fdisk")) { ftb_command_part_table(tool); return; }
    if (!strcmp(tool, "systemctl") || !strcmp(tool, "service")) { ftb_command_service(tool); return; }
    if (!strcmp(tool, "grub") || !strcmp(tool, "loaderctl")) { ftb_command_bootctl(tool); return; }
    if (!strcmp(tool, "cron") || !strcmp(tool, "at") || !strcmp(tool, "systemd-timer")) { ftb_command_scheduler(tool); return; }
    if (!strcmp(tool, "passwd") || !strcmp(tool, "login") || !strcmp(tool, "logout")) { ftb_command_passwd_like(tool); return; }
    if (!strcmp(tool, "chcon") || !strcmp(tool, "semanage")) { ftb_command_label(tool); return; }
    if (!strcmp(tool, "gpg") || !strcmp(tool, "openssl")) { ftb_command_crypto(tool); return; }
    if (!strcmp(tool, "make") || !strcmp(tool, "gcc") || !strcmp(tool, "clang") || !strcmp(tool, "ld") || !strcmp(tool, "pkg-config") || !strcmp(tool, "cmake") || !strcmp(tool, "meson") || !strcmp(tool, "git")) { ftb_command_builder(tool); return; }
    if (!strcmp(tool, "man") || !strcmp(tool, "info") || !strcmp(tool, "doc")) { ftb_command_doc(tool); return; }
    printf("%s: tool not recognized\n", tool);
}

#endif
