#include "shop.h"
#include "app.h"
#include "http.h"
#include <nds.h>
#include <dswifi9.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DS_SHOP_VERSION
#define DS_SHOP_VERSION "dev"
#endif

/* ---- categories ----
   url_prefix mirrors the server's folder layout; local_dir is where the files
   go on the SD card. Themes land where TWiLight Menu++ looks for them. */
const Category CAT_POPULAR   = {"Popular Titles",   "popular",   "",               "/roms/nds",     KIND_ROM};
const Category CAT_DS        = {"All DS Titles",    "nds",       "",               "/roms/nds",     KIND_ROM};
const Category CAT_DSIWARE   = {"DSiWare",          "dsiware",   "dsiware",        "/roms/dsiware", KIND_ROM};
const Category CAT_NES       = {"NES",              "nes",       "vc/nes",         "/roms/nes",     KIND_ROM};
const Category CAT_GB        = {"Game Boy",         "gb",        "vc/gb",          "/roms/gb",      KIND_ROM};
const Category CAT_GBC       = {"Game Boy Color",   "gbc",       "vc/gbc",         "/roms/gbc",     KIND_ROM};
const Category CAT_GBA       = {"Game Boy Advance", "gba",       "vc/gba",         "/roms/gba",     KIND_ROM};
const Category CAT_THEME_DSI = {"DSi Menu Themes",  "theme-dsi", "themes/dsimenu", "/_nds/TWiLightMenu/dsimenu/themes", KIND_THEME};
const Category CAT_THEME_3DS = {"3DS Menu Themes",  "theme-3ds", "themes/3dsmenu", "/_nds/TWiLightMenu/3dsmenu/themes", KIND_THEME};
const Category CAT_THEME_R4  = {"R4 Menu Themes",   "theme-r4",  "themes/r4menu",  "/_nds/TWiLightMenu/r4menu/themes",  KIND_THEME};
const Category CAT_THEME_AK  = {"Wood Menu Themes", "theme-ak",  "themes/akmenu",  "/_nds/TWiLightMenu/akmenu/themes",  KIND_THEME};

/* ---- queue ---- */
static QueueItem g_queue[QUEUE_MAX];
static int       g_queue_count = 0;

int queue_count(void) { return g_queue_count; }

const QueueItem *queue_item(int i) {
    return (i >= 0 && i < g_queue_count) ? &g_queue[i] : NULL;
}

/* Identity = where it downloads from, so the same game reached through two
   lists ("Popular" and "All DS Titles") is still one queue entry. */
static int queue_find(const Title *t, const Category *cat) {
    for (int i = 0; i < g_queue_count; i++)
        if (strcmp(g_queue[i].cat->url_prefix, cat->url_prefix) == 0 &&
            strcmp(g_queue[i].title.file, t->file) == 0)
            return i;
    return -1;
}

bool queue_contains(const Title *t, const Category *cat) {
    return queue_find(t, cat) >= 0;
}

void queue_toggle(const Title *t, const Category *cat) {
    int i = queue_find(t, cat);
    if (i >= 0) {
        for (int j = i; j < g_queue_count - 1; j++) g_queue[j] = g_queue[j + 1];
        g_queue_count--;
    } else if (g_queue_count < QUEUE_MAX) {
        g_queue[g_queue_count].title = *t;
        g_queue[g_queue_count].cat   = cat;
        g_queue_count++;
    }
}

/* ---- downloads ---- */

/* mkdir each component of `path` so nested dirs (e.g. /roms/nds) exist. */
static void mkdir_p(const char *path) {
    static char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

/* http_download reports per-file progress; themes are many files, so add the
   bytes of the files already done before handing it to the UI. */
static void (*g_progress)(size_t, size_t);
static size_t g_done_before, g_item_total;

static void item_progress(size_t received, size_t file_total) {
    (void)file_total;
    if (g_progress) g_progress(g_done_before + received, g_item_total);
}

static void file_progress(size_t received, size_t total) {
    if (g_progress) g_progress(received, total);
}

static bool download_rom(const Config *config, const QueueItem *item) {
    const Category *cat = item->cat;
    mkdir_p(cat->local_dir);

    char dest[320];
    snprintf(dest, sizeof(dest), "%s/%s", cat->local_dir, item->title.file);

    /* URL-encode: spaces/specials would break the request line (server 400s) */
    char enc[256];
    url_encode(item->title.file, enc, sizeof(enc));
    char url[320];
    if (cat->url_prefix[0])
        snprintf(url, sizeof(url), "/roms/%s/%s", cat->url_prefix, enc);
    else
        snprintf(url, sizeof(url), "/roms/%s", enc);

    return http_download(config->server, config->port, url, dest, file_progress) >= 0;
}

/* A theme's file list: one "relative/path|size" per line. Big enough for
   themes with a few hundred files, plus the HTTP headers http_get buffers. */
#define MANIFEST_BUF 32768
static char g_manifest[MANIFEST_BUF];

static bool download_theme(const Config *config, const QueueItem *item) {
    const Category *cat = item->cat;
    const char *theme = item->title.file;

    char enc_name[160];
    url_encode(theme, enc_name, sizeof(enc_name));
    char path[256];
    snprintf(path, sizeof(path), "/theme_files?cat=%s&name=%s", cat->server_cat, enc_name);

    HttpResponse resp;
    int len = http_get(config->server, config->port, path, g_manifest, sizeof(g_manifest), &resp);
    if (len < 0 || resp.status != 200) return false;

    g_item_total = item->title.size;
    g_done_before = 0;

    char *line = g_manifest;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';

        char *bar = strrchr(line, '|');
        if (bar) {
            *bar = '\0';
            size_t size = (size_t)strtoul(bar + 1, NULL, 10);

            /* static: these would take 1.5 KB of the 8 KB stack; an encoded
               URL can be 3x the path */
            static char dest[512], rel[384], full[1152];
            if ((size_t)snprintf(dest, sizeof(dest), "%s/%s/%s", cat->local_dir, theme, line)
                    >= sizeof(dest) ||
                (size_t)snprintf(rel, sizeof(rel), "%s/%s/%s", cat->url_prefix, theme, line)
                    >= sizeof(rel))
                return false;                       /* absurdly long path */
            char *slash = strrchr(dest, '/');
            *slash = '\0';
            mkdir_p(dest);
            *slash = '/';

            memcpy(full, "/roms/", 6);
            url_encode(rel, full + 6, sizeof(full) - 6);

            if (http_download(config->server, config->port, full, dest, item_progress) < 0)
                return false;
            g_done_before += size;
        }
        line = next;
    }
    if (g_progress) g_progress(g_item_total, g_item_total);
    return true;
}

bool shop_download(const Config *config, const QueueItem *item,
                   void (*progress)(size_t done, size_t total)) {
#ifdef TEST_MODE
    /* offline build: pretend, so the download screens can be exercised */
    (void)config;
    size_t total = item->title.size ? item->title.size : 1024 * 1024;
    for (int i = 1; i <= 90; i++) {
        app_vblank();
        if (progress) progress((size_t)((u64)total * i / 90), total);
    }
    return true;
#endif
    g_progress = progress;
    return item->cat->kind == KIND_THEME ? download_theme(config, item)
                                         : download_rom(config, item);
}

bool shop_download_update(const Config *config,
                          void (*progress)(size_t done, size_t total)) {
#ifdef TEST_MODE
    (void)config;
    size_t total = 1024 * 1024;
    for (int i = 1; i <= 90; i++) {
        app_vblank();
        if (progress) progress((size_t)((u64)total * i / 90), total);
    }
    return true;
#else
    const char *dest = config->update_path[0] ? config->update_path : DEFAULT_UPDATE_PATH;
    char dest_dir[MAX_PATH_LEN];
    char temp[MAX_PATH_LEN + 8];
    char backup[MAX_PATH_LEN + 8];
    snprintf(dest_dir, sizeof(dest_dir), "%s", dest);
    if (snprintf(temp, sizeof(temp), "%s.part", dest) >= (int)sizeof(temp)
            || snprintf(backup, sizeof(backup), "%s.bak", dest) >= (int)sizeof(backup))
        return false;

    FILE *installed = fopen(dest, "rb");
    if (installed) {
        fclose(installed);
    } else {
        rename(backup, dest);
    }

    char *slash = strrchr(dest_dir, '/');
    if (slash) {
        if (slash == dest_dir) slash[1] = '\0';
        else *slash = '\0';
        mkdir_p(dest_dir);
    }
    if (http_download(config->server, config->port, "/roms/ds-shop.nds",
                      temp, progress) < 0)
        return false;

    installed = fopen(dest, "rb");
    bool had_installed = installed != NULL;
    if (installed) fclose(installed);
    if (had_installed) {
        remove(backup);
        if (rename(dest, backup) != 0) {
            remove(temp);
            return false;
        }
    }
    if (rename(temp, dest) == 0) {
        if (had_installed) remove(backup);
        return true;
    }
    if (had_installed) rename(backup, dest);
    remove(temp);
    return false;
#endif
}

void shop_run_queue(const Config *config, const ShopDownloadUI *ui, int *ok, int *failed) {
    int total = g_queue_count, good = 0, keep = 0;
    for (int i = 0; i < g_queue_count; i++) {
        if (ui->begin) ui->begin(&g_queue[i], i + 1, total);
        bool ok = shop_download(config, &g_queue[i], ui->progress);
        if (ui->end) ui->end(&g_queue[i], ok);
        if (ok)
            good++;
        else
            g_queue[keep++] = g_queue[i];      /* retain failed items */
    }
    g_queue_count = keep;
    if (ok) *ok = good;
    if (failed) *failed = total - good;
}

/* ---- the server ---- */

/* true if `host` (an IP address) is on the DS's own network */
static bool on_my_network(const char *host) {
#ifdef TEST_MODE
    (void)host;
    return false;
#else
    struct in_addr gw, mask, dns1, dns2;
    struct in_addr me = Wifi_GetIPInfo(&gw, &mask, &dns1, &dns2);
    unsigned long h = inet_addr(host);
    return h != INADDR_NONE && ((h ^ me.s_addr) & mask.s_addr) == 0;
#endif
}

static bool answers(const Config *config) {
    return catalog_total(config->server, config->port, "nds", "") >= 0;
}

bool shop_pick_server(Config *config) {
    if (config->using_backup) config_swap_servers(config);     /* the user's order */
    if (!config->server2[0]) return answers(config);

    /* try the local one first: an unreachable address can take a while */
    if (on_my_network(config->server2) && !on_my_network(config->server))
        config_swap_servers(config);
    if (answers(config)) return true;
    config_swap_servers(config);
    if (answers(config)) return true;
    config_swap_servers(config);
    if (config->using_backup) config_swap_servers(config);
    return false;
}

bool shop_update_available(const Config *config) {
#ifdef TEST_MODE
    (void)config;
    return false;
#else
    char response[1024];
    HttpResponse http_response;
    int len = http_get(config->server, config->port,
                       "/update_status?version=" DS_SHOP_VERSION,
                       response, sizeof(response), &http_response);
    return len == 1 && response[0] == '1';
#endif
}

/* ---- misc ---- */
static unsigned g_entropy = 0;

void shop_stir(void) { g_entropy++; }

unsigned shop_random(unsigned n) {
    if (n == 0) return 0;
    srand(g_entropy);
    return (unsigned)rand() % n;
}

void shop_format_size(size_t bytes, char *buf, int buf_len) {
    if (bytes >= 1024 * 1024)
        snprintf(buf, (size_t)buf_len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, (size_t)buf_len, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, (size_t)buf_len, "%u B", (unsigned)bytes);
}
