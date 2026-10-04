/* The graphical shop: home -> section -> list, plus search and downloads.
   Laid out like the Wii Shop Channel: a home page of big category tiles, each
   leading to a menu of lists. Touch first; every screen also works with buttons. */
#include "app.h"
#include "gui_shop.h"
#include "gui.h"
#include "assets.h"
#include "bg_top.h"
#include "shop.h"
#include "catalog.h"
#include "icon.h"
#include "http.h"
#include "anim.h"
#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAV_BACK      (-1)
#define NAV_EXIT      (-2)
#define NAV_DOWNLOADS (-3)
#define NAV_SETTINGS  (-4)

/* ---- the shop's sections ---- */

#define ACT_BROWSE  0
#define ACT_RANDOM  1
#define ACT_SEARCH  2
#define ACT_PICK    3        /* pick a VC system, then search it */
#define ACT_UPDATE  4        /* update the shop itself */

typedef struct {
    const char     *label;
    const char     *desc;    /* shown on the top screen while focused */
    const Category *cat;
    int             action;
} Entry;

typedef struct {
    const char  *title;
    const char  *tagline;    /* home tile's second line */
    const char  *desc;
    const Entry *entries;
    int          n;
} Section;

static const Entry DS_ENTRIES[] = {
    {"Popular Titles", "A hand-picked list of well-loved DS games.",  &CAT_POPULAR, ACT_BROWSE},
    {"All DS Titles",  "Every DS game on the server, from A to Z.",   &CAT_DS,      ACT_BROWSE},
    {"DSiWare",        "Downloadable DSi software.",                  &CAT_DSIWARE, ACT_BROWSE},
    {"Random Title",   "Feeling lucky? Jump to a random DS game.",    &CAT_DS,      ACT_RANDOM},
    {"Search",         "Find a DS game by name.",                     &CAT_DS,      ACT_SEARCH},
    {"Update DS Shop", "Get the latest version of the shop.",         &CAT_DS,      ACT_UPDATE},
};
static const Entry VC_ENTRIES[] = {
    {"NES",              "Nintendo Entertainment System classics.",     &CAT_NES, ACT_BROWSE},
    {"Game Boy",         "The original Game Boy library.",              &CAT_GB,  ACT_BROWSE},
    {"Game Boy Color",   "Game Boy Color games, in full color.",        &CAT_GBC, ACT_BROWSE},
    {"Game Boy Advance", "Game Boy Advance games.",                     &CAT_GBA, ACT_BROWSE},
    {"Search",           "Search one of the Virtual Console systems.",  NULL,     ACT_PICK},
};
static const Entry VC_SEARCH_ENTRIES[] = {
    {"NES",              "Search NES games.",              &CAT_NES, ACT_SEARCH},
    {"Game Boy",         "Search Game Boy games.",         &CAT_GB,  ACT_SEARCH},
    {"Game Boy Color",   "Search Game Boy Color games.",   &CAT_GBC, ACT_SEARCH},
    {"Game Boy Advance", "Search Game Boy Advance games.", &CAT_GBA, ACT_SEARCH},
};
static const Entry THEME_ENTRIES[] = {
    {"DSi Menu",  "Themes for TWiLight Menu++'s DSi-style menu.",   &CAT_THEME_DSI, ACT_BROWSE},
    {"3DS Menu",  "Themes for TWiLight Menu++'s 3DS-style menu.",   &CAT_THEME_3DS, ACT_BROWSE},
    {"R4 Menu",   "Themes for TWiLight Menu++'s R4-style menu.",    &CAT_THEME_R4,  ACT_BROWSE},
    {"Wood Menu", "Themes for TWiLight Menu++'s Wood (akmenu) UI.", &CAT_THEME_AK,  ACT_BROWSE},
};

static const Section SECTIONS[] = {
    {"Nintendo DS & DSiWare", "DS games and DSiWare",
     "DS games and DSiWare. Browse the popular picks, the full list, or search.",
     DS_ENTRIES, 6},
    {"Virtual Console", "NES, Game Boy, GBC and GBA",
     "Classic games for TWiLight Menu++'s built-in emulators.",
     VC_ENTRIES, 5},
    {"TWiLight Menu Themes", "Make your menu your own",
     "Themes install straight into TWiLight Menu++. Pick one in its settings.",
     THEME_ENTRIES, 4},
};
#define NUM_SECTIONS 3

static const Section VC_SEARCH_SECTION = {
    "Search which system?", NULL, "Pick the system to search.", VC_SEARCH_ENTRIES, 4
};

/* short label for list badges when a title has no icon (themes get the bag) */
static const char *cat_tag(const Category *cat) {
    if (cat == &CAT_NES) return "NES";
    if (cat == &CAT_GB)  return "GB";
    if (cat == &CAT_GBC) return "GBC";
    if (cat == &CAT_GBA) return "GBA";
    if (cat == &CAT_DSIWARE) return "DSi";
    return "DS";
}

/* ---- shared bits ---- */

static void top_info(const u16 *bg, const char *heading, const char *text) {
    gui_top_frame(bg, 52, 88);
    gfx_text_center(SCR_TOP, &font_title, SCR_W / 2, 60, heading, C_TEXT);
    if (text)
        gfx_text_wrap(SCR_TOP, &font_body, 26, 64 + font_title.height, SCR_W - 52, 3,
                      text, C_TEXT_DIM);
}

static char g_dl_label[24];

static const char *downloads_label(void) {
    snprintf(g_dl_label, sizeof(g_dl_label), "Downloads (%d)", queue_count());
    return g_dl_label;
}

/* ---- menus: the home tiles and each section's 2-column grid ---- */

#define ID_BACK       100
#define ID_DOWNLOADS  101
#define ID_SETTINGS   102

/* Returns the chosen index or a NAV_* action. */
static int menu_screen(const Section *sec, bool home) {
    Widget w[9];              /* up to 6 entries + Back + Settings + Downloads */
    int n = 0;
    int count = home ? NUM_SECTIONS : sec->n;

    for (int i = 0; i < count; i++) {
        Widget *x = &w[n++];
        memset(x, 0, sizeof(*x));
        x->id = i;
        if (home) {
            x->r = (Rect){14, (s16)(30 + i * 42), 228, 38};
            x->label = SECTIONS[i].title;
            x->sublabel = SECTIONS[i].tagline;
            x->style = STYLE_TILE;
        } else {
            x->r = (Rect){(s16)(10 + (i % 2) * 122), (s16)(32 + (i / 2) * 42), 114, 36};
            x->label = sec->entries[i].label;
            x->style = STYLE_NORMAL;
        }
    }
    w[n++] = (Widget){ {6, 165, 64, 22}, home ? "Exit" : "Back", NULL, ID_BACK, STYLE_NORMAL, false };
    if (home)
        w[n++] = (Widget){ {76, 165, 68, 22}, "Settings", NULL, ID_SETTINGS, STYLE_NORMAL, false };
    w[n++] = (Widget){ {150, 165, 100, 22}, downloads_label(), NULL, ID_DOWNLOADS,
                       STYLE_PRIMARY, queue_count() == 0 };

    WidgetSet ws = { w, n, 0, -1, true };
    int last_focus = -2;
    bool dirty = true;
    Input in;

    for (;;) {
        if (ws.focus != last_focus) {
            /* the top screen describes whatever is focused */
            last_focus = ws.focus;
            if (home) {
                const Section *s = (ws.focus >= 0 && ws.focus < NUM_SECTIONS) ? &SECTIONS[ws.focus] : NULL;
                top_info(bg_home_top, s ? s->title : "Nintendo DS Shop",
                         s ? s->desc : "Welcome! Tap a category to start shopping.");
            } else {
                const Entry *e = (ws.focus >= 0 && ws.focus < sec->n) ? &sec->entries[ws.focus] : NULL;
                top_info(bg_top, e ? e->label : sec->title, e ? e->desc : sec->desc);
            }
            gfx_present(1);
        }
        if (dirty) {
            gui_bottom_frame(home ? "Nintendo DS Shop" : sec->title, NULL);
            widgets_draw(&ws);
            gfx_present(2);
            dirty = false;
        }

        gui_input(&in);
        if (in.down & KEY_START) return NAV_EXIT;
        if ((in.down & KEY_B) && !home) return NAV_BACK;

        int id = widgets_update(&ws, &in, &dirty);
        if (id == ID_BACK)      return home ? NAV_EXIT : NAV_BACK;
        if (id == ID_DOWNLOADS) return NAV_DOWNLOADS;
        if (id == ID_SETTINGS)  return NAV_SETTINGS;
        if (id >= 0)            return id;
    }
}

/* ---- the on-screen keyboard ---- */

static const char *const KB_ROWS[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-'" };
#define ID_KB_CANCEL  200
#define ID_KB_SPACE   201
#define ID_KB_DEL     202
#define ID_KB_OK      203
#define ID_KB_SHIFT   204

typedef struct {
    const char *title;       /* header */
    const char *heading;     /* top screen */
    const char *help;
    const char *ok;          /* the confirm button's label */
    bool        lower;       /* start in lower case */
    bool        allow_empty;
} KbSetup;

/* Edits `out` (which may hold starting text); returns true on OK, false if
   cancelled. The last key of the third row switches upper and lower case. */
static bool keyboard(const KbSetup *k, char *out, int out_size) {
    static char labels[40][2];
    Widget w[45];
    int n = 0;
    bool lower = k->lower;
    for (int r = 0; r < 4; r++) {
        int len = strlen(KB_ROWS[r]);
        int x0 = (SCR_W - 10 * 25 + 2) / 2;
        for (int c = 0; c < len; c++) {
            labels[n][0] = KB_ROWS[r][c];
            labels[n][1] = '\0';
            w[n] = (Widget){ {(s16)(x0 + c * 25), (s16)(54 + r * 26), 23, 24}, labels[n], NULL,
                             KB_ROWS[r][c], STYLE_KEY, false };
            n++;
        }
        if (r == 2)
            w[n++] = (Widget){ {(s16)(x0 + len * 25), 54 + 2 * 26, 23, 24}, "aA", NULL,
                               ID_KB_SHIFT, STYLE_KEY, false };
    }
    int nkeys = n;
    w[n++] = (Widget){ {4, 165, 58, 22},   "Cancel", NULL, ID_KB_CANCEL, STYLE_NORMAL, false };
    w[n++] = (Widget){ {66, 165, 62, 22},  "Space",  NULL, ID_KB_SPACE,  STYLE_NORMAL, false };
    w[n++] = (Widget){ {132, 165, 54, 22}, "Delete", NULL, ID_KB_DEL,    STYLE_NORMAL, false };
    w[n++] = (Widget){ {190, 165, 62, 22}, k->ok,    NULL, ID_KB_OK,     STYLE_PRIMARY, false };

    WidgetSet ws = { w, n, 10 /* "Q" */, -1, true };
    int len = strlen(out);

    top_info(bg_top, k->heading, k->help);
    gfx_present(1);

    bool dirty = true, relabel = true;
    Input in;
    for (;;) {
        if (relabel) {
            /* letter keys follow the case */
            for (int i = 0; i < nkeys; i++) {
                int c = w[i].id;
                if (c >= 'A' && c <= 'Z' && lower) w[i].id = c - 'A' + 'a';
                else if (c >= 'a' && c <= 'z' && !lower) w[i].id = c - 'a' + 'A';
                if (w[i].id < 128) labels[i][0] = (char)w[i].id;
            }
            relabel = false;
            dirty = true;
        }
        if (dirty) {
            gui_bottom_frame(k->title, NULL);
            gfx_round_rect(SCR_BOT, 8, 27, SCR_W - 16, 22, 5, C_WHITE, C_ACCENT);
            int x = gfx_text_fit(SCR_BOT, &font_body, 14, 27 + (22 - font_body.height) / 2,
                                 SCR_W - 34, out, C_TEXT);
            gfx_vline(SCR_BOT, x + 1, 31, 14, C_ACCENT_DARK);          /* caret */
            widgets_draw(&ws);
            gfx_present(2);
            dirty = false;
        }

        gui_input(&in);
        int id = widgets_update(&ws, &in, &dirty);
        if ((in.down & KEY_START) || id == ID_KB_CANCEL) return false;
        if ((in.down & KEY_X) || id == ID_KB_OK) {
            if (len > 0 || k->allow_empty) return true;
            continue;
        }
        if ((in.down & KEY_L) || (in.down & KEY_R) || id == ID_KB_SHIFT) { lower = !lower; relabel = true; }
        if (((in.down & KEY_B) || id == ID_KB_DEL) && len > 0) { out[--len] = '\0'; dirty = true; }
        if (in.down & KEY_Y) { len = 0; out[0] = '\0'; dirty = true; }
        char ch = 0;
        if (id == ID_KB_SPACE) ch = ' ';
        else if (id > 0 && id < 128) ch = (char)id;
        if (ch && len < out_size - 1) { out[len++] = ch; out[len] = '\0'; dirty = true; }
    }
}

/* the search box: returns true to search */
static bool keyboard_screen(const char *what, char *out, int out_size) {
    char heading[48];
    snprintf(heading, sizeof(heading), "Search %s", what);
    KbSetup k = { "Search", heading, "Type part of a name, then tap Search. "
                  "B deletes, X searches, START cancels.", "Search", false, false };
    out[0] = '\0';
    return keyboard(&k, out, out_size);
}

/* ---- downloads ---- */

static int g_dl_n, g_dl_total, g_dl_pct;
static const QueueItem *g_dl_item;
static bool g_dl_waiting;           /* no data for a while: say so */
static size_t g_dl_done, g_dl_size;

static void dl_draw(size_t done, size_t total) {
    char head[40];
    if (g_dl_total > 1) snprintf(head, sizeof(head), "Downloading %d of %d", g_dl_n, g_dl_total);
    else                snprintf(head, sizeof(head), "Downloading");
    gui_bottom_frame(head, NULL);

    /* the storage box fills with blue as the download progresses: the DSi
       Shop's own animation when its sprites are on the SD card (anim.c),
       otherwise a drawn box */
    if (anim_has_dl()) {
        anim_dl_progress(g_dl_pct);
    } else {
        const int bx = 88, by = 36, bw = 80, bh = 72;
        gfx_round_rect(SCR_BOT, bx, by, bw, bh, 8, C_WHITE, C_BORDER);
        int fill = (bh - 8) * g_dl_pct / 100;
        if (fill > 0)
            gfx_round_rect(SCR_BOT, bx + 4, by + bh - 4 - fill, bw - 8, fill, 4, C_ACCENT, C_ACCENT);
    }

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", g_dl_pct);
    gfx_text_center(SCR_BOT, &font_title, SCR_W / 2, 116, pct, C_TEXT);

    char a[16], b[16], line[48];
    shop_format_size(done, a, sizeof(a));
    shop_format_size(total, b, sizeof(b));
    snprintf(line, sizeof(line), "%s of %s", a, b);
    gfx_text_center(SCR_BOT, &font_small, SCR_W / 2, 138, line, C_TEXT_DIM);
    gfx_text_center(SCR_BOT, &font_small, SCR_W / 2, 166,
                    g_dl_waiting ? "Waiting for the server..." : "Please don't turn off the power.",
                    g_dl_waiting ? C_ACCENT_DARK : C_TEXT_DIM);
    gfx_present(2);
    g_dl_done = done;
    g_dl_size = total;
}

void gui_net_waiting(int frames) {
    /* after 3 s without data, the download screen says it's waiting (the
       download gives up after 20 s, see http.c) */
    if (g_dl_item && frames == 3 * 60 && !g_dl_waiting) {
        g_dl_waiting = true;
        dl_draw(g_dl_done, g_dl_size);
    }
}

static void dl_begin(const QueueItem *item, int n, int total) {
    g_dl_item = item;
    g_dl_n = n;
    g_dl_total = total;
    g_dl_pct = 0;
    g_dl_waiting = false;

    gui_top_frame(bg_detail_top, 52, 88);
    gfx_text_wrap(SCR_TOP, &font_title, 26, 60, SCR_W - 52, 2, item->title.name, C_TEXT);
    char sz[16], line[64];
    shop_format_size(item->title.size, sz, sizeof(sz));
    snprintf(line, sizeof(line), "%s  -  %s", item->cat->name, sz);
    gfx_text_center(SCR_TOP, &font_small, SCR_W / 2, 116, line, C_TEXT_DIM);
    gfx_present(1);

    anim_dl_start();
    dl_draw(0, item->title.size);
}

static void dl_end(const QueueItem *item, bool ok) {
    (void)item;
    if (!ok || !anim_has_dl()) return;
    /* let the box close up before the next item or the result */
    anim_dl_finish();
    for (int i = 0; i < 3 * 60 && !anim_dl_finished(); i++) app_vblank();
}

static void dl_progress(size_t done, size_t total) {
    if (total == 0) return;
    int pct = (int)((u64)done * 100 / total);      /* 64-bit: size_t*100 wraps */
    if (pct > 100) pct = 100;
    if (pct == g_dl_pct && !g_dl_waiting) return;  /* only redraw on change */
    g_dl_pct = pct;
    g_dl_waiting = false;
    dl_draw(done, total);
}

static void run_downloads(const Config *config) {
    if (queue_count() == 0) return;
    static const ShopDownloadUI ui = { dl_begin, dl_progress, dl_end };
    int ok, fail;
    shop_run_queue(config, &ui, &ok, &fail);
    g_dl_item = NULL;
    anim_dl_stop();

    char l1[40], l2[40];
    snprintf(l1, sizeof(l1), "%d item%s downloaded.", ok, ok == 1 ? "" : "s");
    if (fail) snprintf(l2, sizeof(l2), "%d failed and stayed in the queue.", fail);
    gui_message(fail ? "Some downloads failed" : "Download complete", l1, fail ? l2 : NULL);
}

static void run_update(const Config *config) {
    static QueueItem update_item;
    g_dl_item = &update_item;
    g_dl_n = 1;
    g_dl_total = 1;
    g_dl_pct = 0;
    g_dl_waiting = false;

    gui_top_frame(bg_detail_top, 52, 88);
    gfx_text_wrap(SCR_TOP, &font_title, 26, 60, SCR_W - 52, 2, "Update DS Shop", C_TEXT);
    gfx_text_center(SCR_TOP, &font_small, SCR_W / 2, 116,
                    "Downloading the latest version", C_TEXT_DIM);
    gfx_present(1);
    anim_dl_start();
    dl_draw(0, 0);

    bool ok = shop_download_update(config, dl_progress);
    dl_end(&update_item, ok);
    g_dl_item = NULL;
    anim_dl_stop();

    char saved_to[MAX_PATH_LEN + 16];
    snprintf(saved_to, sizeof(saved_to), "Saved to %s.",
             config->update_path[0] ? config->update_path : DEFAULT_UPDATE_PATH);
    gui_message(ok ? "Update downloaded" : "Update failed",
                ok ? saved_to : "Couldn't download ds-shop.nds from the server.",
                ok ? NULL : "Check the server and try again.");
}

/* ---- the list browser ---- */

#define ROWS      4                  /* visible rows */
#define ROW_H     34
#define LIST_Y    (HEADER_H + 2)
#define LIST_W    222

#define ID_B_BACK    300
#define ID_B_SEARCH  301
#define ID_B_QUEUE   302
#define ID_B_GET     303
#define ID_B_UP      304
#define ID_B_DOWN    305

static Catalog g_page;               /* the one page in RAM */
static int     g_loaded_page = -1;

/* theme previews: 128x96 BGR555, fetched once the selection rests */
#define PREVIEW_W 128
#define PREVIEW_H 96
static u16  g_preview[PREVIEW_W * PREVIEW_H + 512] __attribute__((aligned(4)));
static char g_preview_for[MAX_FILE_LEN];     /* which theme g_preview holds */

static bool fetch_preview(const Config *config, const Category *cat, const Title *t) {
#ifdef TEST_MODE
    (void)config; (void)cat; (void)t;
    return false;
#else
    char enc[160], path[224];
    url_encode(t->file, enc, sizeof(enc));
    snprintf(path, sizeof(path), "/preview.bin?cat=%s&name=%s", cat->server_cat, enc);
    HttpResponse resp;
    int len = http_get(config->server, config->port, path, (char *)g_preview,
                       sizeof(g_preview), &resp);
    if (len != PREVIEW_W * PREVIEW_H * 2) return false;
    snprintf(g_preview_for, sizeof(g_preview_for), "%s", t->file);
    return true;
#endif
}

static bool load_page(const Config *config, const Category *cat, const char *query, int p) {
    if (p == g_loaded_page) return true;
    gfx_fill_blend(SCR_BOT, 0, LIST_Y, SCR_W, BAR_Y - LIST_Y, C_WHITE, 12);
    gfx_text_center(SCR_BOT, &font_body, SCR_W / 2, 86, "Loading...", C_TEXT_DIM);
    gfx_present(2);
    if (catalog_fetch_page(&g_page, config->server, config->port, cat->server_cat, query, p) < 0)
        return false;
    icons_fetch_page(config->server, config->port, cat->server_cat, query, p);
    g_loaded_page = p;
    return true;
}

static void draw_detail(const Category *cat, const Title *t, bool have_preview) {
    gui_top_frame(bg_detail_top, 10, 172);
    char sz[16], line[64];
    shop_format_size(t->size, sz, sizeof(sz));
    bool queued = queue_contains(t, cat);
    int row = g_page.titles <= t && t < g_page.titles + g_page.count ? (int)(t - g_page.titles) : -1;

    if (cat->kind == KIND_THEME) {
        const int px = (SCR_W - PREVIEW_W) / 2, py = 18;
        gfx_fill(SCR_TOP, px - 1, py - 1, PREVIEW_W + 2, PREVIEW_H + 2, C_BORDER);
        if (have_preview)
            gfx_image(SCR_TOP, px, py, PREVIEW_W, PREVIEW_H, g_preview);
        else {
            gfx_fill(SCR_TOP, px, py, PREVIEW_W, PREVIEW_H, C_ACCENT_PALE);
            gfx_text_center(SCR_TOP, &font_small, SCR_W / 2, py + 40, "Preview", C_TEXT_DIM);
        }
        gfx_text_center(SCR_TOP, &font_title, SCR_W / 2, 120, t->name, C_TEXT);
        snprintf(line, sizeof(line), "%s  -  %s", cat->name, sz);
        gfx_text_center(SCR_TOP, &font_small, SCR_W / 2, 142, line, C_TEXT_DIM);
    } else {
        const void *tiles = row >= 0 ? icon_tiles(row) : NULL;
        const void *pal   = row >= 0 ? icon_palette(row) : NULL;
        gfx_round_rect(SCR_TOP, 22, 22, 72, 72, 8, C_WHITE, C_BORDER);
        if (tiles && pal && ((const u16 *)pal)[1])       /* blank blocks have no palette */
            gfx_icon(SCR_TOP, 26, 26, tiles, pal, 2);
        else
            gfx_text_center(SCR_TOP, &font_title, 58, 48, cat_tag(cat), C_ACCENT_DARK);
        gfx_text_wrap(SCR_TOP, &font_title, 104, 24, SCR_W - 126, 3, t->name, C_TEXT);
        gfx_text(SCR_TOP, &font_small, 104, 84, cat->name, C_TEXT_DIM);
        snprintf(line, sizeof(line), "Size: %s", sz);
        gfx_text(SCR_TOP, &font_body, 26, 108, line, C_TEXT);
    }

    gfx_text(SCR_TOP, &font_body, 26, 160,
             queued ? "In your download queue" : "Not in your queue",
             queued ? C_GREEN : C_TEXT_DIM);
    snprintf(line, sizeof(line), "Queue: %d", queue_count());
    gfx_text(SCR_TOP, &font_small, SCR_W - 26 - gfx_text_width(&font_small, line), 162,
             line, C_TEXT_DIM);
}

static void draw_arrow(Rect r, bool up, bool enabled) {
    widget_draw(&(Widget){ r, "", NULL, 0, STYLE_NORMAL, !enabled }, false, false);
    u16 c = enabled ? C_ACCENT_DARK : C_BORDER;
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    for (int i = 0; i < 6; i++) {
        int y = up ? cy - 3 + i : cy + 2 - i;
        gfx_hline(SCR_BOT, cx - i, y, i * 2 + 1, c);
    }
}

static void draw_list(const Category *cat, const char *query, int sel_row, int scroll,
                      int total_pages, WidgetSet *bar) {
    char title[64], note[24];
    if (query[0]) snprintf(title, sizeof(title), "Search: %s", query);
    else          snprintf(title, sizeof(title), "%s", cat->name);
    snprintf(note, sizeof(note), "%d/%d", g_loaded_page + 1, total_pages);
    gui_bottom_frame(title, note);

    for (int v = 0; v < ROWS; v++) {
        int row = scroll + v;
        if (row >= g_page.count) break;
        const Title *t = &g_page.titles[row];
        int y = LIST_Y + v * ROW_H;
        if (row == sel_row)
            gfx_round_rect(SCR_BOT, 2, y + 1, LIST_W, ROW_H - 2, 6, C_ACCENT_PALE, C_ACCENT);
        else
            gfx_hline(SCR_BOT, 8, y + ROW_H - 1, LIST_W - 12, C_BORDER);

        const void *tiles = icon_tiles(row), *pal = icon_palette(row);
        if (tiles && pal && ((const u16 *)pal)[1]) {
            gfx_icon(SCR_BOT, 6, y + 1, tiles, pal, 1);
        } else {
            gfx_round_rect(SCR_BOT, 6, y + 3, 32, 28, 5, C_WHITE, C_BORDER);
            if (cat->kind == KIND_THEME)
                gfx_image(SCR_BOT, 22 - bag_icon_w / 2, y + 17 - bag_icon_h / 2,
                          bag_icon_w, bag_icon_h, bag_icon);
            else
                gfx_text_center(SCR_BOT, &font_small, 22, y + 9, cat_tag(cat), C_ACCENT_DARK);
        }

        gfx_text_fit(SCR_BOT, &font_body, 44, y + 3, LIST_W - 48, t->name, C_TEXT);
        char sz[16];
        shop_format_size(t->size, sz, sizeof(sz));
        int x = gfx_text(SCR_BOT, &font_small, 44, y + 18, sz, C_TEXT_DIM);
        if (queue_contains(t, cat))
            gfx_text(SCR_BOT, &font_small, x + 8, y + 18, "In queue", C_GREEN);
    }

    draw_arrow((Rect){228, LIST_Y + 2, 24, 64}, true, sel_row > 0 || g_loaded_page > 0);
    draw_arrow((Rect){228, LIST_Y + 70, 24, 64}, false, true);
    widgets_draw(bar);
}

/* Browse one list, optionally filtered by `query`, starting at title `start`.
   Returns NAV_BACK or NAV_EXIT. */
static int browse_screen(const Config *config, const Category *cat,
                         const char *initial_query, int start) {
    char query[40];
    snprintf(query, sizeof(query), "%s", initial_query ? initial_query : "");
    int selected = start > 0 ? start : 0;

    Widget bar_w[4] = {
        { {4, 165, 56, 22},   "Back",   NULL, ID_B_BACK,   STYLE_NORMAL,  false },
        { {64, 165, 56, 22},  "Search", NULL, ID_B_SEARCH, STYLE_NORMAL,  false },
        { {124, 165, 56, 22}, "Add",    NULL, ID_B_QUEUE,  STYLE_NORMAL,  false },
        { {184, 165, 68, 22}, "",       NULL, ID_B_GET,    STYLE_PRIMARY, false },
    };
    WidgetSet bar = { bar_w, 4, -1, -1, false };
    static char get_label[16];

    for (;;) {    /* re-entered whenever the search query changes */
        int total = catalog_total(config->server, config->port, cat->server_cat, query);
        if (total <= 0) {
            gui_message(total < 0 ? "Couldn't load this list" : (query[0] ? "No matches" : "Nothing here yet"),
                        total < 0 ? "Is the server still running?" : NULL, NULL);
            return NAV_BACK;
        }
        int total_pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
        if (selected >= total) selected = 0;
        g_loaded_page = -1;
        if (!load_page(config, cat, query, selected / PAGE_SIZE)) {
            gui_message("Couldn't load this list", "Is the server still running?", NULL);
            return NAV_BACK;
        }

        int scroll = -1;               /* first visible row within the page */
        bool dirty_top = true, dirty_bot = true, requery = false;
        int preview_wait = 0;
        Input in;

        while (!requery) {
            int row = selected - g_loaded_page * PAGE_SIZE;
            if (scroll < 0 || row < scroll) scroll = row;
            if (row >= scroll + ROWS) scroll = row - ROWS + 1;
            if (scroll > g_page.count - ROWS) scroll = g_page.count - ROWS;
            if (scroll < 0) scroll = 0;

            const Title *t = &g_page.titles[row];
            if (dirty_top) {
                bool have = cat->kind == KIND_THEME && strcmp(g_preview_for, t->file) == 0;
                draw_detail(cat, t, have);
                gfx_present(1);
                dirty_top = false;
                if (cat->kind == KIND_THEME && !have) preview_wait = 20;
            }
            if (dirty_bot) {
                bar_w[2].label = queue_contains(t, cat) ? "Remove" : "Add";
                snprintf(get_label, sizeof(get_label), "Get (%d)", queue_count());
                bar_w[3].label = get_label;
                bar_w[3].disabled = queue_count() == 0;
                draw_list(cat, query, row, scroll, total_pages, &bar);
                gfx_present(2);
                dirty_bot = false;
            }

            gui_input(&in);

            /* theme preview: fetch once the selection has rested for a moment */
            if (preview_wait > 0 && --preview_wait == 0 && fetch_preview(config, cat, t))
                dirty_top = true;

            int id = widgets_update(&bar, &in, &dirty_bot);
            int prev = selected;

            /* touch the list: tap a row to select it, tap it again to (un)queue */
            if (in.touch_up && in.sx < LIST_W && in.sy >= LIST_Y && in.sy < BAR_Y) {
                int v0 = (in.sy - LIST_Y) / ROW_H, v1 = (in.ty - LIST_Y) / ROW_H;
                int r = scroll + v0;
                if (v0 == v1 && in.ty >= LIST_Y && r < g_page.count) {
                    if (r == row) id = ID_B_QUEUE;
                    else selected = g_loaded_page * PAGE_SIZE + r;
                }
            }
            if (in.touch_up && in.sx >= 228 && in.sy >= LIST_Y && in.sy < BAR_Y)
                id = (in.sy < LIST_Y + 68) ? ID_B_UP : ID_B_DOWN;

            if (in.down & KEY_START) return NAV_EXIT;
            if ((in.down & KEY_B) || id == ID_B_BACK) return NAV_BACK;
            if ((in.down & KEY_UP) && selected > 0) selected--;
            if ((in.down & KEY_DOWN) && selected < total - 1) selected++;
            if (id == ID_B_UP)   { selected -= ROWS; if (selected < 0) selected = 0; }
            if (id == ID_B_DOWN) { selected += ROWS; if (selected > total - 1) selected = total - 1; }
            if (in.down & KEY_L) { selected -= PAGE_SIZE; if (selected < 0) selected = 0; }
            if (in.down & KEY_R) { selected += PAGE_SIZE; if (selected > total - 1) selected = total - 1; }

            if (selected != prev) {
                int pg = selected / PAGE_SIZE;
                if (pg != g_loaded_page) {
                    if (!load_page(config, cat, query, pg)) selected = prev;
                    scroll = -1;
                }
                dirty_top = dirty_bot = true;
                preview_wait = 0;
            }

            if ((in.down & KEY_A) || id == ID_B_QUEUE) {
                queue_toggle(&g_page.titles[selected - g_loaded_page * PAGE_SIZE], cat);
                dirty_top = dirty_bot = true;
            }
            if (((in.down & KEY_X) || id == ID_B_GET) && queue_count() > 0) {
                run_downloads(config);
                dirty_top = dirty_bot = true;
            }
            if ((in.down & KEY_Y) || id == ID_B_SEARCH) {
                char q2[40];
                if (keyboard_screen(cat->name, q2, sizeof(q2))) {
                    snprintf(query, sizeof(query), "%s", q2);
                    selected = 0;
                    requery = true;
                }
                dirty_top = dirty_bot = true;
            }
        }
    }
}

/* ---- settings ---- */

enum { SET_SERVER, SET_PORT, SET_SERVER2, SET_PORT2, SET_MUSIC, SET_VOLUME, SET_UI, SET_UPDATE_PATH, SET_COUNT };
#define ID_SET_CANCEL 300
#define ID_SET_SAVE   301
#define SET_Y   28
#define SET_H   17

static const char *const SET_LABELS[SET_COUNT] = {
    "Server", "Port", "Backup server", "Backup port", "Music", "Music volume", "Start in", "Update path"
};
static const char *const SET_HELP[SET_COUNT] = {
    "The address of the PC or Pi running the shop server. Tap to change it.",
    "The server's port (8888 unless you changed it).",
    "A second server, like a travel hotspot's. The DS tries the one on its own network first.",
    "The backup server's port.",
    "The DSi Shop music (needs music.bin). SELECT also mutes it anywhere.",
    "How loud the music plays. Left and Right change it.",
    "Which interface to start in. Holding SELECT at startup picks the text one.",
    "Where to save the updated DS Shop rom. Defaults to /roms/nds/ds-shop.nds",
};

static void setting_value(const Config *c, int i, char *buf, int len) {
    switch (i) {
    case SET_SERVER:      snprintf(buf, len, "%s", c->server); break;
    case SET_PORT:        snprintf(buf, len, "%d", c->port); break;
    case SET_SERVER2:     snprintf(buf, len, "%s", c->server2[0] ? c->server2 : "None"); break;
    case SET_PORT2:       snprintf(buf, len, "%d", c->port2); break;
    case SET_MUSIC:       snprintf(buf, len, "%s", c->music ? "On" : "Off"); break;
    case SET_VOLUME:      snprintf(buf, len, "<  %d%%  >", c->music_volume); break;
    case SET_UPDATE_PATH: snprintf(buf, len, "%s", c->update_path); break;
    default:              snprintf(buf, len, "%s", c->text_ui ? "Text menu" : "Shop"); break;
    }
}

/* edit a text or number setting with the keyboard */
static void edit_setting(Config *c, int i) {
    char buf[MAX_PATH_LEN];
    KbSetup k = { SET_LABELS[i], SET_LABELS[i], SET_HELP[i], "OK", true, i == SET_SERVER2 };
    if (i == SET_SERVER || i == SET_SERVER2) {
        snprintf(buf, sizeof(buf), "%s", i == SET_SERVER ? c->server : c->server2);
        if (!keyboard(&k, buf, MAX_SERVER_LEN)) return;
        snprintf(i == SET_SERVER ? c->server : c->server2, MAX_SERVER_LEN,
             "%.*s", MAX_SERVER_LEN - 1, buf);
    } else if (i == SET_UPDATE_PATH) {
        snprintf(buf, sizeof(buf), "%s", c->update_path);
        if (!keyboard(&k, buf, sizeof(buf))) return;
        snprintf(c->update_path, sizeof(c->update_path), "%s", buf);
    } else {
        int *port = (i == SET_PORT) ? &c->port : &c->port2;
        snprintf(buf, sizeof(buf), "%d", *port);
        if (!keyboard(&k, buf, 6)) return;
        int v = atoi(buf);
        if (v > 0 && v < 65536) *port = v;
    }
}

static void settings_screen(Config *config) {
    /* edit a copy, in the user's order (not swapped for the backup) */
    static Config c;
    c = *config;
    if (c.using_backup) config_swap_servers(&c);

    Widget w[SET_COUNT + 2];
    for (int i = 0; i < SET_COUNT; i++)
        w[i] = (Widget){ {8, (s16)(SET_Y + i * SET_H), 240, SET_H - 2}, "", NULL, i, STYLE_NORMAL, false };
    w[SET_COUNT]     = (Widget){ {6, 165, 64, 22},    "Cancel", NULL, ID_SET_CANCEL, STYLE_NORMAL, false };
    w[SET_COUNT + 1] = (Widget){ {170, 165, 80, 22},  "Save",   NULL, ID_SET_SAVE,   STYLE_PRIMARY, false };
    WidgetSet ws = { w, SET_COUNT + 2, 0, -1, true };

    int last_focus = -2;
    bool dirty = true;
    Input in;
    for (;;) {
        if (ws.focus != last_focus || dirty) {
            last_focus = ws.focus;
            bool row = ws.focus >= 0 && ws.focus < SET_COUNT;
            top_info(bg_top, row ? SET_LABELS[ws.focus] : "Settings",
                     row ? SET_HELP[ws.focus] : "Save keeps the changes in /ds-shop/config.ini.");
            gfx_present(1);
        }
        if (dirty) {
            gui_bottom_frame("Settings", NULL);
            widgets_draw(&ws);
            for (int i = 0; i < SET_COUNT; i++) {
                char v[MAX_PATH_LEN + 8];
                setting_value(&c, i, v, sizeof(v));
                int y = SET_Y + i * SET_H + (SET_H - 2 - font_small.height) / 2;
                gfx_text(SCR_BOT, &font_small, 16, y, SET_LABELS[i], C_TEXT);
                int vw = gfx_text_width(&font_small, v);
                if (vw > 128) vw = 128;
                gfx_text_fit(SCR_BOT, &font_small, 240 - vw, y, 128, v, C_ACCENT_DARK);
            }
            gfx_present(2);
            dirty = false;
        }

        gui_input(&in);
        int f = ws.focus;
        bool on_row = f >= 0 && f < SET_COUNT;

        /* Left/Right change the focused setting instead of moving the focus */
        int step = 0;
        if (on_row && (in.down & KEY_LEFT))  step = -1;
        if (on_row && (in.down & KEY_RIGHT)) step = 1;
        Input nav = in;
        if (on_row) nav.down &= ~(KEY_LEFT | KEY_RIGHT);

        int id = widgets_update(&ws, &nav, &dirty);
        if ((in.down & KEY_B) || id == ID_SET_CANCEL) {
            music_apply(config);                  /* undo any music preview */
            return;
        }
        if ((in.down & KEY_START) || id == ID_SET_SAVE) break;

        /* a tap on the volume row's left or right half turns it down or up */
        if (id == SET_VOLUME && in.touch_up) step = in.tx < 128 + 60 ? -1 : 1;
        if (step && f == SET_VOLUME) {
            int v = c.music_volume + step * 10;
            c.music_volume = v < 0 ? 0 : v > 100 ? 100 : v;
            music_apply(&c);                      /* hear it right away */
            dirty = true;
        } else if (step && (f == SET_MUSIC || f == SET_UI)) {
            id = f;
        }
        switch (id) {
        case SET_SERVER: case SET_PORT: case SET_SERVER2: case SET_PORT2: case SET_UPDATE_PATH:
            edit_setting(&c, id);
            dirty = true;
            last_focus = -2;
            break;
        case SET_MUSIC:
            c.music = !c.music;
            music_apply(&c);
            dirty = true;
            break;
        case SET_UI:
            c.text_ui = !c.text_ui;
            dirty = true;
            break;
        }
    }

    /* save, then use the new settings */
    bool server_changed = strcmp(c.server, config->using_backup ? config->server2 : config->server)
        || strcmp(c.server2, config->using_backup ? config->server : config->server2)
        || c.port != (config->using_backup ? config->port2 : config->port)
        || c.port2 != (config->using_backup ? config->port : config->port2);
    c.using_backup = 0;
    bool saved = config_save(&c, CONFIG_PATH);
    *config = c;
    music_apply(config);
    if (!saved) {
        gui_message("Couldn't save", "The SD card couldn't be written.",
                    "The settings apply until you exit.");
    }
    if (server_changed) {
        gui_status("Connecting...", "Contacting the shop server.", NULL);
        if (!shop_pick_server(config)) {
            char where[96];
            snprintf(where, sizeof(where), "Is it running at %s:%d?", config->server, config->port);
            gui_message("Server not reachable", where, "Check the address in Settings.");
        }
    }
}

/* ---- running a menu entry ---- */

static int run_entry(const Config *config, const Entry *e) {
    if (e->action == ACT_UPDATE) {
        run_update(config);
        return NAV_BACK;
    }
    if (e->action == ACT_SEARCH) {
        char q[40];
        if (!keyboard_screen(e->cat->name, q, sizeof(q))) return NAV_BACK;
        return browse_screen(config, e->cat, q, 0);
    }
    if (e->action == ACT_RANDOM) {
        int total = catalog_total(config->server, config->port, e->cat->server_cat, "");
        return browse_screen(config, e->cat, "", total > 0 ? (int)shop_random(total) : 0);
    }
    return browse_screen(config, e->cat, "", 0);
}

static int run_section(const Config *config, const Section *sec) {
    for (;;) {
        int r = menu_screen(sec, false);
        if (r == NAV_BACK || r == NAV_EXIT) return r;
        if (r == NAV_DOWNLOADS) { run_downloads(config); continue; }

        const Entry *e = &sec->entries[r];
        int res = (e->action == ACT_PICK) ? run_section(config, &VC_SEARCH_SECTION)
                                          : run_entry(config, e);
        if (res == NAV_EXIT) return NAV_EXIT;
    }
}

void gui_run(Config *config) {
    for (;;) {
        int r = menu_screen(NULL, true);
        if (r == NAV_EXIT) return;
        if (r == NAV_DOWNLOADS) { run_downloads(config); continue; }
        if (r == NAV_SETTINGS)  { settings_screen(config); continue; }
        if (r >= 0 && run_section(config, &SECTIONS[r]) == NAV_EXIT) return;
    }
}
