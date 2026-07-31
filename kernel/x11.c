#include "x11.h"
#include "axshell.h"
#include "ksock.h"
#include "../lib/common.h"
#include "../lib/font8x8_basic.h"
#include "../mm/malloc.h"
#include <stddef.h>

extern int  ax_x_window_create(int x, int y, int w, int h, uint32_t xid);
extern int  ax_x_window_move_resize(int surf_id, int x, int y, int w, int h);
extern int  ax_x_window_map(int surf_id);
extern int  ax_x_window_unmap(int surf_id);
extern int  ax_x_window_destroy(int surf_id);
extern int  ax_x_window_raise(int surf_id);
extern int  ax_x_window_set_title(int surf_id, const char *title);
extern int  ax_x_window_get_geom(int surf_id, int *x, int *y, int *w, int *h);
extern uint32_t *ax_x_window_canvas(int surf_id);
extern int  ax_x_window_canvas_w(int surf_id);
extern int  ax_x_window_canvas_h(int surf_id);

#define X11_MAX_CLIENTS 16u
#define X11_MAX_RESOURCES 512u
#define X11_MAX_ATOMS 256u
#define X11_MAX_PROPS 128u
#define X11_MAX_EVQUEUE 64u
#define X11_RX_CAP 16384u
#define X11_TX_CAP 131072u

#define X11_RES_NONE 0u
#define X11_RES_WIN  1u
#define X11_RES_PIX  2u
#define X11_RES_GC   3u

#define X11_GC_FILL_SOLID 0u
#define X11_GC_FILL_TILED 1u
#define X11_GC_FILL_STIPPLED 2u
#define X11_GC_FILL_OPAQUE_STIPPLED 3u

#define X_EV_KEY_PRESS         2
#define X_EV_KEY_RELEASE       3
#define X_EV_BUTTON_PRESS      4
#define X_EV_BUTTON_RELEASE    5
#define X_EV_MOTION_NOTIFY     6
#define X_EV_EXPOSE            12
#define X_EV_UNMAP_NOTIFY      18
#define X_EV_MAP_NOTIFY        19
#define X_EV_CONFIGURE_NOTIFY  22
#define X_EV_CLIENT_MESSAGE    33
#define X_EV_SELECTION_NOTIFY  31
#define X_EV_FOCUS_IN          9

#define X_MASK_KEY_PRESS      0x00000001u
#define X_MASK_KEY_RELEASE    0x00000002u
#define X_MASK_BUTTON_PRESS   0x00000004u
#define X_MASK_BUTTON_RELEASE 0x00000008u
#define X_MASK_EXPOSURE       0x00008000u
#define X_MASK_POINTER_MOTION 0x00000040u
#define X_MASK_STRUCTURE      0x00020000u
#define X_MASK_SUBSTRUCTURE   0x00080000u
#define X_MASK_FOCUS_CHANGE   0x00200000u
#define X_MASK_PROPERTY_CHANGE 0x00400000u

typedef struct {
    uint32_t id;
    uint32_t type;
    int      surf_id;
    uint32_t owner;
    uint32_t w, h;
    uint32_t fg, bg;
    uint8_t  fill_style;
    uint32_t ev_mask;
    uint8_t  used;
} x11_res_t;

typedef struct {
    uint32_t atom;
    uint32_t window;
    uint32_t type;
    uint8_t  format;
    uint8_t  data[1024];
    uint32_t len;
    uint8_t  used;
} x11_prop_t;

typedef struct {
    ksock_t  *sock;
    int       fd;
    uint32_t  client_index;
    uint32_t  rid_base;
    uint32_t  rid_mask;
    uint8_t   rx[X11_RX_CAP];
    uint32_t  rx_len;
    uint8_t   tx[X11_TX_CAP];
    uint32_t  tx_len;
    uint8_t   handshake_done;
    uint8_t   used;
    uint32_t  seq;
} x11_client_t;

static x11_client_t x11_clients[X11_MAX_CLIENTS];
static x11_res_t    x11_resources[X11_MAX_RESOURCES];
static x11_prop_t   x11_props[X11_MAX_PROPS];
static char        *x11_atom_names[X11_MAX_ATOMS];
static uint32_t     x11_atom_count;
static ksock_t     *x11_listen_sock;
static int          x11_listen_fd;
static int          x11_scr_w, x11_scr_h;
static uint32_t     x11_event_seq;

static const uint32_t X11_COLORMAP_ID = 0x100;
static const uint32_t X11_ROOT_ID     = 0x101;

static void x11_put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void x11_put_u32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint16_t x11_get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t x11_get_u32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

static x11_client_t *x11_client_by_fd(int fd)
{
    for (uint32_t i = 0; i < X11_MAX_CLIENTS; i++)
        if (x11_clients[i].used && x11_clients[i].fd == fd)
            return &x11_clients[i];
    return NULL;
}

static x11_res_t *x11_res_find(uint32_t id)
{
    for (uint32_t i = 0; i < X11_MAX_RESOURCES; i++)
        if (x11_resources[i].used && x11_resources[i].id == id)
            return &x11_resources[i];
    return NULL;
}

static x11_res_t *x11_res_alloc(uint32_t id, uint32_t type)
{
    for (uint32_t i = 0; i < X11_MAX_RESOURCES; i++) {
        if (!x11_resources[i].used) {
            memset(&x11_resources[i], 0, sizeof(x11_res_t));
            x11_resources[i].used = 1;
            x11_resources[i].id = id;
            x11_resources[i].type = type;
            x11_resources[i].fg = 0xFFFFFFFFu;
            x11_resources[i].bg = 0xFF000000u;
            return &x11_resources[i];
        }
    }
    return NULL;
}

static void x11_res_free(uint32_t id)
{
    x11_res_t *r = x11_res_find(id);
    if (r) r->used = 0;
}

static x11_client_t *x11_res_owner(uint32_t id)
{
    x11_res_t *r = x11_res_find(id);
    if (!r) return NULL;
    return &x11_clients[r->owner];
}

static uint32_t x11_atom_intern(const char *name, uint8_t only_if_exists)
{
    for (uint32_t i = 0; i < x11_atom_count; i++) {
        if (x11_atom_names[i]) {
            uint32_t k = 0;
            while (x11_atom_names[i][k] && name[k] && x11_atom_names[i][k] == name[k]) k++;
            if (x11_atom_names[i][k] == 0 && name[k] == 0)
                return i + 1;
        }
    }
    if (only_if_exists) return 0;
    if (x11_atom_count >= X11_MAX_ATOMS) return 0;
    uint32_t len = 0;
    while (name[len] && len < 63) len++;
    char *copy = (char*)malloc(len + 1);
    if (!copy) return 0;
    for (uint32_t i = 0; i <= len; i++) copy[i] = name[i];
    x11_atom_names[x11_atom_count] = copy;
    x11_atom_count++;
    return x11_atom_count;
}

static const char *x11_atom_name(uint32_t atom)
{
    if (atom == 0 || atom > x11_atom_count) return NULL;
    return x11_atom_names[atom - 1];
}

static void x11_tx_append(x11_client_t *c, const void *data, uint32_t len)
{
    if (c->tx_len + len > X11_TX_CAP) {
        uint32_t sent = c->tx_len;
        if (sent > 0) {
            ksock_send(c->sock, c->tx, sent);
            c->tx_len = 0;
        }
    }
    if (c->tx_len + len > X11_TX_CAP) {
        ksock_send(c->sock, data, len);
        return;
    }
    memcpy(c->tx + c->tx_len, data, len);
    c->tx_len += len;
}

static void x11_tx_flush(x11_client_t *c)
{
    if (c->tx_len > 0) {
        int sent = ksock_send(c->sock, c->tx, c->tx_len);
        if (sent > 0) {
            uint32_t rem = c->tx_len - (uint32_t)sent;
            if (rem > 0) memmove(c->tx, c->tx + sent, rem);
            c->tx_len = rem;
        }
    }
}

static void x11_send_error(x11_client_t *c, uint8_t code, uint32_t bad_value, uint16_t major, uint8_t minor)
{
    uint8_t p[32];
    memset(p, 0, sizeof(p));
    p[0] = 0;
    p[1] = code;
    x11_put_u16(p + 2, (uint16_t)c->seq);
    x11_put_u32(p + 4, bad_value);
    x11_put_u16(p + 8, major);
    p[10] = minor;
    x11_tx_append(c, p, 32);
}

static void x11_send_reply(x11_client_t *c, uint8_t code, uint32_t extra_len, const void *body, uint32_t body_len)
{
    uint8_t hdr[32];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 1;
    hdr[1] = code;
    x11_put_u16(hdr + 2, (uint16_t)c->seq);
    x11_put_u32(hdr + 4, extra_len);
    x11_tx_append(c, hdr, 32);
    if (body && body_len > 0) x11_tx_append(c, body, body_len);
    uint32_t total = 32 + body_len;
    uint32_t pad = (4 - (total & 3)) & 3;
    if (pad) { uint8_t z[4] = {0,0,0,0}; x11_tx_append(c, z, pad); }
}

static void x11_send_event_to(x11_client_t *c, const uint8_t *ev)
{
    if (!c) return;
    x11_tx_append(c, ev, 32);
}

static void x11_broadcast_event(uint32_t window_id, const uint8_t *ev)
{
    for (uint32_t i = 0; i < X11_MAX_RESOURCES; i++) {
        if (x11_resources[i].used && x11_resources[i].id == window_id) {
            x11_client_t *c = &x11_clients[x11_resources[i].owner];
            if (c->used) x11_send_event_to(c, ev);
        }
    }
}

static x11_prop_t *x11_prop_find(uint32_t window, uint32_t atom)
{
    for (uint32_t i = 0; i < X11_MAX_PROPS; i++)
        if (x11_props[i].used && x11_props[i].window == window && x11_props[i].atom == atom)
            return &x11_props[i];
    return NULL;
}

static void x11_handshake(x11_client_t *c, const uint8_t *p, uint32_t len)
{
    (void)len;
    uint16_t major = x11_get_u16(p + 2);
    uint16_t minor = x11_get_u16(p + 4);
    (void)major; (void)minor;
    uint16_t n = x11_get_u16(p + 6);
    uint16_t d = x11_get_u16(p + 8);
    (void)n; (void)d;

    uint32_t rid_base = 0x200 + (c->client_index << 20);
    uint32_t rid_mask = 0xFFFFF;
    c->rid_base = rid_base;
    c->rid_mask = rid_mask;
    c->handshake_done = 1;

    uint8_t reply[4096];
    memset(reply, 0, sizeof(reply));
    reply[0] = 1;
    reply[1] = 0;
    x11_put_u16(reply + 2, 11);
    x11_put_u16(reply + 4, 0);
    x11_put_u16(reply + 6, 0);

    uint32_t off = 32;
    x11_put_u32(reply + off, 0); off += 4;
    x11_put_u32(reply + off, rid_mask); off += 4;
    x11_put_u32(reply + off, 0); off += 4;
    x11_put_u16(reply + off, 1024); off += 2;
    x11_put_u16(reply + off, 256); off += 2;
    reply[off++] = 1;
    reply[off++] = 0;
    reply[off++] = 1;
    reply[off++] = 1;
    reply[off++] = 32;
    reply[off++] = 1;
    reply[off++] = 0;
    reply[off++] = 0;
    reply[off++] = 8;
    reply[off++] = 8;
    reply[off++] = 8;
    reply[off++] = 8;

    uint32_t screen_off = off;
    x11_put_u32(reply + off, X11_ROOT_ID); off += 4;
    x11_put_u32(reply + off, X11_COLORMAP_ID); off += 4;
    x11_put_u32(reply + off, 0xFF000000u); off += 4;
    x11_put_u32(reply + off, 0xFFFFFFFFu); off += 4;
    x11_put_u32(reply + off, 0); off += 4;
    x11_put_u16(reply + off, (uint16_t)x11_scr_w); off += 2;
    x11_put_u16(reply + off, (uint16_t)x11_scr_h); off += 2;
    x11_put_u16(reply + off, 270); off += 2;
    x11_put_u16(reply + off, 203); off += 2;
    x11_put_u16(reply + off, 0); off += 2;
    x11_put_u16(reply + off, 0); off += 2;
    reply[off++] = 24;
    reply[off++] = 0;
    reply[off++] = 0;
    reply[off++] = 0;
    uint32_t depth_count_off = off - 1;
    reply[off++] = 0;

    uint8_t depths = 0;
    {
        uint32_t depth_mark = off;
        reply[off++] = 24;
        reply[off++] = 0;
        x11_put_u16(reply + off, 0); off += 2;
        reply[off++] = 1;
        reply[off++] = 0;
        reply[off++] = 0;
        reply[off++] = 0;
        reply[off++] = 24;
        reply[off++] = 8;
        reply[off++] = 8;
        reply[off++] = 8;
        reply[off++] = 0;
        reply[off++] = 0;
        reply[off++] = 0;
        reply[off++] = 0;
        depths++;
        reply[depth_count_off] = depths;
        (void)depth_mark;
    }

    x11_put_u16(reply + 6, (uint16_t)(off / 4));
    ksock_send(c->sock, reply, off);
}

static void x11_req_create_window(x11_client_t *c, const uint8_t *p)
{
    uint8_t depth = p[1];
    (void)depth;
    uint32_t wid = x11_get_u32(p + 4);
    uint32_t parent = x11_get_u32(p + 8);
    int16_t x = (int16_t)x11_get_u16(p + 12);
    int16_t y = (int16_t)x11_get_u16(p + 14);
    uint16_t w = x11_get_u16(p + 16);
    uint16_t h = x11_get_u16(p + 18);
    uint16_t border = x11_get_u16(p + 20);
    uint16_t klass = x11_get_u16(p + 22);
    (void)border; (void)klass;
    uint32_t value_mask = x11_get_u32(p + 28);

    if (w == 0) w = 1;
    if (h == 0) h = 1;

    x11_res_t *r = x11_res_alloc(wid, X11_RES_WIN);
    if (!r) { x11_send_error(c, 10, wid, 1, 0); return; }
    r->owner = c->client_index;
    r->w = w;
    r->h = h;
    r->surf_id = -1;

    if (parent == X11_ROOT_ID || x11_res_find(parent)) {
        r->surf_id = ax_x_window_create(x, y, w, h, wid);
    }

    if (value_mask & 0x00000800u) {
        uint32_t off = 32;
        for (uint32_t bit = 0; bit < 16; bit++) {
            if (value_mask & (1u << bit)) {
                uint32_t v = x11_get_u32(p + off);
                if (bit == 11) r->ev_mask = v;
                off += 4;
            }
        }
    }
}

static void x11_req_change_window_attributes(x11_client_t *c, const uint8_t *p)
{
    uint32_t wid = x11_get_u32(p + 4);
    uint32_t value_mask = x11_get_u32(p + 8);
    x11_res_t *r = x11_res_find(wid);
    if (!r) { x11_send_error(c, 3, wid, 2, 0); return; }
    uint32_t off = 12;
    for (uint32_t bit = 0; bit < 16; bit++) {
        if (value_mask & (1u << bit)) {
            uint32_t v = x11_get_u32(p + off);
            if (bit == 11) r->ev_mask = v;
            off += 4;
        }
    }
}

static void x11_req_destroy_window(x11_client_t *c, const uint8_t *p)
{
    uint32_t wid = x11_get_u32(p + 4);
    x11_res_t *r = x11_res_find(wid);
    if (!r) return;
    if (r->surf_id >= 0) ax_x_window_destroy(r->surf_id);
    r->used = 0;
}

static void x11_req_map_window(x11_client_t *c, const uint8_t *p)
{
    uint32_t wid = x11_get_u32(p + 4);
    x11_res_t *r = x11_res_find(wid);
    if (!r) { x11_send_error(c, 3, wid, 8, 0); return; }
    if (r->surf_id >= 0) ax_x_window_map(r->surf_id);
}

static void x11_req_unmap_window(x11_client_t *c, const uint8_t *p)
{
    uint32_t wid = x11_get_u32(p + 4);
    x11_res_t *r = x11_res_find(wid);
    if (!r) return;
    if (r->surf_id >= 0) ax_x_window_unmap(r->surf_id);
}

static void x11_req_configure_window(x11_client_t *c, const uint8_t *p)
{
    uint32_t wid = x11_get_u32(p + 4);
    uint16_t value_mask = x11_get_u16(p + 8);
    x11_res_t *r = x11_res_find(wid);
    if (!r) { x11_send_error(c, 3, wid, 12, 0); return; }
    int x = 0, y = 0, w = 0, h = 0;
    if (r->surf_id >= 0)
        ax_x_window_get_geom(r->surf_id, &x, &y, &w, &h);
    uint32_t off = 12;
    for (uint32_t bit = 0; bit < 7; bit++) {
        if (value_mask & (1u << bit)) {
            int16_t v = (int16_t)x11_get_u16(p + off);
            if (bit == 0) x = v;
            if (bit == 1) y = v;
            if (bit == 2) w = v;
            if (bit == 3) h = v;
            off += 4;
        }
    }
    if (r->surf_id >= 0)
        ax_x_window_move_resize(r->surf_id, x, y, w, h);
}

static void x11_req_create_gc(x11_client_t *c, const uint8_t *p)
{
    uint32_t gid = x11_get_u32(p + 4);
    uint32_t drawable = x11_get_u32(p + 8);
    uint32_t value_mask = x11_get_u32(p + 12);
    (void)drawable;
    x11_res_t *r = x11_res_alloc(gid, X11_RES_GC);
    if (!r) { x11_send_error(c, 10, gid, 55, 0); return; }
    r->owner = c->client_index;
    uint32_t off = 16;
    for (uint32_t bit = 0; bit < 23; bit++) {
        if (value_mask & (1u << bit)) {
            uint32_t v = x11_get_u32(p + off);
            if (bit == 0) r->fg = v | 0xFF000000u;
            if (bit == 1) r->bg = v | 0xFF000000u;
            if (bit == 2) r->fill_style = (uint8_t)v;
            off += 4;
        }
    }
}

static void x11_req_change_gc(x11_client_t *c, const uint8_t *p)
{
    uint32_t gid = x11_get_u32(p + 4);
    uint32_t value_mask = x11_get_u32(p + 8);
    x11_res_t *r = x11_res_find(gid);
    if (!r || r->type != X11_RES_GC) { x11_send_error(c, 13, gid, 56, 0); return; }
    uint32_t off = 12;
    for (uint32_t bit = 0; bit < 23; bit++) {
        if (value_mask & (1u << bit)) {
            uint32_t v = x11_get_u32(p + off);
            if (bit == 0) r->fg = v | 0xFF000000u;
            if (bit == 1) r->bg = v | 0xFF000000u;
            if (bit == 2) r->fill_style = (uint8_t)v;
            off += 4;
        }
    }
}

static void x11_req_free_gc(x11_client_t *c, const uint8_t *p)
{
    uint32_t gid = x11_get_u32(p + 4);
    x11_res_free(gid);
}

static x11_res_t *x11_drawable_lookup(x11_client_t *c, uint32_t id)
{
    x11_res_t *r = x11_res_find(id);
    if (!r) { x11_send_error(c, 9, id, 0, 0); return NULL; }
    if (r->type != X11_RES_WIN && r->type != X11_RES_PIX) {
        x11_send_error(c, 9, id, 0, 0);
        return NULL;
    }
    return r;
}

static void x11_blit_glyph(uint32_t *canvas, int cw, int ch, char glyph, int x, int y, uint32_t color)
{
    if (glyph < 0) glyph = 0;
    const uint8_t *bits = font8x8_basic[(uint8_t)glyph];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (bits[row] & (1 << col)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < cw && py >= 0 && py < ch)
                    canvas[py * cw + px] = color;
            }
        }
    }
}

static void x11_req_poly_fill_rectangle(x11_client_t *c, const uint8_t *p, uint32_t req_len)
{
    uint32_t drawable = x11_get_u32(p + 4);
    uint32_t gc = x11_get_u32(p + 8);
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    x11_res_t *g = x11_res_find(gc);
    if (!d || !g) return;
    uint32_t *canvas = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int cw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int ch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!canvas) return;
    uint32_t color = g->fg;
    uint32_t off = 12;
    while (off + 8 <= req_len * 4) {
        int16_t rx = (int16_t)x11_get_u16(p + off);
        int16_t ry = (int16_t)x11_get_u16(p + off + 2);
        uint16_t rw = x11_get_u16(p + off + 4);
        uint16_t rh = x11_get_u16(p + off + 6);
        for (int yy = ry; yy < ry + rh; yy++) {
            if (yy < 0 || yy >= ch) continue;
            for (int xx = rx; xx < rx + rw; xx++) {
                if (xx < 0 || xx >= cw) continue;
                canvas[yy * cw + xx] = color;
            }
        }
        off += 8;
    }
}

static void x11_req_poly_rectangle(x11_client_t *c, const uint8_t *p, uint32_t req_len)
{
    uint32_t drawable = x11_get_u32(p + 4);
    uint32_t gc = x11_get_u32(p + 8);
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    x11_res_t *g = x11_res_find(gc);
    if (!d || !g) return;
    uint32_t *canvas = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int cw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int ch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!canvas) return;
    uint32_t color = g->fg;
    uint32_t off = 12;
    while (off + 8 <= req_len * 4) {
        int16_t rx = (int16_t)x11_get_u16(p + off);
        int16_t ry = (int16_t)x11_get_u16(p + off + 2);
        uint16_t rw = x11_get_u16(p + off + 4);
        uint16_t rh = x11_get_u16(p + off + 6);
        for (int xx = rx; xx < rx + rw; xx++) {
            if (xx >= 0 && xx < cw && ry >= 0 && ry < ch) canvas[ry * cw + xx] = color;
            if (xx >= 0 && xx < cw && ry + rh - 1 >= 0 && ry + rh - 1 < ch) canvas[(ry + rh - 1) * cw + xx] = color;
        }
        for (int yy = ry; yy < ry + rh; yy++) {
            if (yy >= 0 && yy < ch && rx >= 0 && rx < cw) canvas[yy * cw + rx] = color;
            if (yy >= 0 && yy < ch && rx + rw - 1 >= 0 && rx + rw - 1 < cw) canvas[yy * cw + rx + rw - 1] = color;
        }
        off += 8;
    }
}

static void x11_req_poly_line(x11_client_t *c, const uint8_t *p, uint32_t req_len)
{
    uint32_t drawable = x11_get_u32(p + 4);
    uint32_t gc = x11_get_u32(p + 8);
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    x11_res_t *g = x11_res_find(gc);
    if (!d || !g) return;
    uint32_t *canvas = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int cw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int ch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!canvas) return;
    uint32_t color = g->fg;
    uint32_t off = 12;
    int16_t px = 0, py = 0;
    uint8_t first = 1;
    while (off + 4 <= req_len * 4) {
        int16_t x = (int16_t)x11_get_u16(p + off);
        int16_t y = (int16_t)x11_get_u16(p + off + 2);
        if (!first) {
            int dx = x - px, dy = y - py;
            int steps = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            if (ady > steps) steps = ady;
            if (steps == 0) steps = 1;
            for (int i = 0; i <= steps; i++) {
                int lx = px + dx * i / steps;
                int ly = py + dy * i / steps;
                if (lx >= 0 && lx < cw && ly >= 0 && ly < ch)
                    canvas[ly * cw + lx] = color;
            }
        }
        px = x; py = y; first = 0;
        off += 4;
    }
}

static void x11_req_poly_segment(x11_client_t *c, const uint8_t *p, uint32_t req_len)
{
    uint32_t drawable = x11_get_u32(p + 4);
    uint32_t gc = x11_get_u32(p + 8);
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    x11_res_t *g = x11_res_find(gc);
    if (!d || !g) return;
    uint32_t *canvas = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int cw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int ch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!canvas) return;
    uint32_t color = g->fg;
    uint32_t off = 12;
    while (off + 8 <= req_len * 4) {
        int16_t x1 = (int16_t)x11_get_u16(p + off);
        int16_t y1 = (int16_t)x11_get_u16(p + off + 2);
        int16_t x2 = (int16_t)x11_get_u16(p + off + 4);
        int16_t y2 = (int16_t)x11_get_u16(p + off + 6);
        int dx = x2 - x1, dy = y2 - y1;
        int steps = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        if (ady > steps) steps = ady;
        if (steps == 0) steps = 1;
        for (int i = 0; i <= steps; i++) {
            int lx = x1 + dx * i / steps;
            int ly = y1 + dy * i / steps;
            if (lx >= 0 && lx < cw && ly >= 0 && ly < ch)
                canvas[ly * cw + lx] = color;
        }
        off += 8;
    }
}

static void x11_req_image_text(x11_client_t *c, const uint8_t *p, uint32_t req_len, uint8_t big)
{
    uint32_t drawable = x11_get_u32(p + 4);
    uint32_t gc = x11_get_u32(p + 8);
    int16_t x = (int16_t)x11_get_u16(p + 12);
    int16_t y = (int16_t)x11_get_u16(p + 14);
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    x11_res_t *g = x11_res_find(gc);
    if (!d || !g) return;
    uint32_t *canvas = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int cw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int ch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!canvas) return;
    uint32_t color = g->fg;
    uint32_t off = 16;
    if (big) {
        uint32_t n = (req_len * 4 - 16) / 2;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t ch2 = p[off + i * 2 + 1];
            x11_blit_glyph(canvas, cw, ch, (char)ch2, x + (int)i * 8, y - 8, color);
        }
    } else {
        uint32_t n = req_len * 4 - 16;
        for (uint32_t i = 0; i < n; i++) {
            x11_blit_glyph(canvas, cw, ch, (char)p[off + i], x + (int)i * 8, y - 8, color);
        }
    }
}

static void x11_req_put_image(x11_client_t *c, const uint8_t *p, uint32_t req_len)
{
    uint8_t format = p[1];
    (void)format;
    uint32_t drawable = x11_get_u32(p + 4);
    uint16_t w = x11_get_u16(p + 12);
    uint16_t h = x11_get_u16(p + 14);
    int16_t dst_x = (int16_t)x11_get_u16(p + 16);
    int16_t dst_y = (int16_t)x11_get_u16(p + 18);
    uint8_t depth = p[22];
    (void)depth;
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    if (!d) return;
    uint32_t *canvas = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int cw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int ch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!canvas) return;
    const uint8_t *img = p + 24;
    uint32_t avail = req_len * 4 - 24;
    uint64_t need64 = (uint64_t)w * (uint64_t)h * 4u;
    uint32_t need = (need64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)need64;
    if (avail < need) need = avail;
    for (uint32_t i = 0; i < need / 4; i++) {
        int px = (int)(i % w);
        int py = (int)(i / w);
        int cx = dst_x + px;
        int cy = dst_y + py;
        if (cx >= 0 && cx < cw && cy >= 0 && cy < ch) {
            uint8_t b = img[i * 4 + 0];
            uint8_t g = img[i * 4 + 1];
            uint8_t r = img[i * 4 + 2];
            uint8_t a = img[i * 4 + 3];
            canvas[cy * cw + cx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

static void x11_req_get_image(x11_client_t *c, const uint8_t *p)
{
    uint8_t format = p[1];
    (void)format;
    uint32_t drawable = x11_get_u32(p + 4);
    int16_t x = (int16_t)x11_get_u16(p + 8);
    int16_t y = (int16_t)x11_get_u16(p + 10);
    uint16_t w = x11_get_u16(p + 12);
    uint16_t h = x11_get_u16(p + 14);
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    if (!d) return;
    uint32_t *canvas = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int cw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int ch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!canvas) return;
    uint64_t need64 = (uint64_t)w * (uint64_t)h * 4u;
    uint32_t need = (need64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)need64;
    uint8_t *buf = (uint8_t*)malloc(need);
    if (!buf) return;
    memset(buf, 0, need);
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        int px = (int)(i % w);
        int py = (int)(i / w);
        int cx = x + px;
        int cy = y + py;
        if (cx >= 0 && cx < cw && cy >= 0 && cy < ch) {
            uint32_t pxv = canvas[cy * cw + cx];
            buf[i * 4 + 0] = (uint8_t)(pxv & 0xFF);
            buf[i * 4 + 1] = (uint8_t)((pxv >> 8) & 0xFF);
            buf[i * 4 + 2] = (uint8_t)((pxv >> 16) & 0xFF);
            buf[i * 4 + 3] = (uint8_t)((pxv >> 24) & 0xFF);
        }
    }
    uint8_t hdr[32];
    memset(hdr, 0, 32);
    hdr[0] = 1;
    hdr[1] = 24;
    x11_put_u16(hdr + 2, (uint16_t)c->seq);
    x11_put_u32(hdr + 4, need / 4);
    x11_put_u32(hdr + 8, 0x101);
    x11_tx_append(c, hdr, 32);
    x11_tx_append(c, buf, need);
    free(buf);
    uint32_t total = 32 + need;
    uint32_t pad = (4 - (total & 3)) & 3;
    if (pad) { uint8_t z[4] = {0,0,0,0}; x11_tx_append(c, z, pad); }
}

static void x11_req_intern_atom(x11_client_t *c, const uint8_t *p)
{
    uint8_t only_if_exists = p[1];
    uint16_t name_len = x11_get_u16(p + 8);
    char name[64];
    uint32_t copy = name_len < 63 ? name_len : 63;
    for (uint32_t i = 0; i < copy; i++) name[i] = (char)p[12 + i];
    name[copy] = 0;
    uint32_t atom = x11_atom_intern(name, only_if_exists);
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 8, atom);
    x11_tx_append(c, reply, 32);
}

static void x11_req_get_atom_name(x11_client_t *c, const uint8_t *p)
{
    uint32_t atom = x11_get_u32(p + 4);
    const char *name = x11_atom_name(atom);
    uint16_t len = 0;
    if (name) while (name[len] && len < 255) len++;
    uint8_t hdr[32];
    memset(hdr, 0, 32);
    hdr[0] = 1;
    x11_put_u16(hdr + 2, (uint16_t)c->seq);
    x11_put_u32(hdr + 4, (len + 3) / 4);
    x11_put_u16(hdr + 8, len);
    x11_tx_append(c, hdr, 32);
    if (len > 0) x11_tx_append(c, name, len);
    uint32_t total = 32 + len;
    uint32_t pad = (4 - (total & 3)) & 3;
    if (pad) { uint8_t z[4] = {0,0,0,0}; x11_tx_append(c, z, pad); }
}

static void x11_req_change_property(x11_client_t *c, const uint8_t *p, uint32_t req_len)
{
    uint8_t mode = p[1];
    (void)mode;
    uint32_t window = x11_get_u32(p + 4);
    uint32_t property = x11_get_u32(p + 8);
    uint32_t type = x11_get_u32(p + 12);
    uint8_t format = p[16];
    uint32_t data_len = x11_get_u32(p + 20);
    const uint8_t *data = p + 24;
    uint32_t byte_len = (format == 32) ? data_len * 4 : (format == 16) ? data_len * 2 : data_len;
    if (byte_len > 1024) byte_len = 1024;
    if (24 + byte_len > req_len * 4) byte_len = req_len * 4 - 24;

    x11_prop_t *prop = x11_prop_find(window, property);
    if (!prop) {
        for (uint32_t i = 0; i < X11_MAX_PROPS; i++) {
            if (!x11_props[i].used) {
                prop = &x11_props[i];
                memset(prop, 0, sizeof(*prop));
                prop->used = 1;
                prop->window = window;
                prop->atom = property;
                break;
            }
        }
    }
    if (!prop) return;
    prop->type = type;
    prop->format = format;
    prop->len = byte_len;
    memcpy(prop->data, data, byte_len);

    if (window != X11_ROOT_ID) {
        x11_res_t *r = x11_res_find(window);
        if (r && r->surf_id >= 0) {
            const char *wm_name = x11_atom_name(property);
            if (wm_name && wm_name[0] == 'W' && wm_name[1] == 'M' && wm_name[2] == '_' && wm_name[3] == 'N') {
                char title[48];
                uint32_t tl = byte_len < 47 ? byte_len : 47;
                for (uint32_t i = 0; i < tl; i++) title[i] = (char)data[i];
                title[tl] = 0;
                ax_x_window_set_title(r->surf_id, title);
            }
        }
    }
}

static void x11_req_get_property(x11_client_t *c, const uint8_t *p)
{
    uint8_t delete_flag = p[1];
    (void)delete_flag;
    uint32_t window = x11_get_u32(p + 4);
    uint32_t property = x11_get_u32(p + 8);
    uint32_t type_req = x11_get_u32(p + 12);
    uint32_t long_offset = x11_get_u32(p + 16);
    uint32_t long_length = x11_get_u32(p + 20);
    (void)long_offset; (void)long_length;

    x11_prop_t *prop = x11_prop_find(window, property);
    if (!prop) {
        uint8_t reply[32];
        memset(reply, 0, 32);
        reply[0] = 1;
        x11_put_u16(reply + 2, (uint16_t)c->seq);
        x11_put_u32(reply + 8, 0);
        x11_put_u32(reply + 12, 0);
        x11_put_u32(reply + 16, 0);
        x11_put_u32(reply + 20, 0);
        x11_tx_append(c, reply, 32);
        return;
    }
    uint32_t format = prop->format;
    uint32_t len = prop->len;
    if (type_req != 0 && type_req != prop->type) {
        format = 0;
        len = 0;
    }
    uint8_t hdr[32];
    memset(hdr, 0, 32);
    hdr[0] = 1;
    hdr[1] = (uint8_t)format;
    x11_put_u16(hdr + 2, (uint16_t)c->seq);
    x11_put_u32(hdr + 4, (len + 3) / 4);
    x11_put_u32(hdr + 8, prop->type);
    x11_put_u32(hdr + 12, 0);
    x11_put_u32(hdr + 16, (format == 32) ? len / 4 : (format == 16) ? len / 2 : len);
    x11_tx_append(c, hdr, 32);
    if (len > 0) x11_tx_append(c, prop->data, len);
    uint32_t total = 32 + len;
    uint32_t pad = (4 - (total & 3)) & 3;
    if (pad) { uint8_t z[4] = {0,0,0,0}; x11_tx_append(c, z, pad); }
}

static void x11_req_get_geometry(x11_client_t *c, const uint8_t *p)
{
    uint32_t drawable = x11_get_u32(p + 4);
    x11_res_t *d = x11_drawable_lookup(c, drawable);
    if (!d) return;
    int x = 0, y = 0, w = 100, h = 100;
    if (d->surf_id >= 0)
        ax_x_window_get_geom(d->surf_id, &x, &y, &w, &h);
    else { w = (int)d->w; h = (int)d->h; }
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    reply[1] = 24;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 8, X11_ROOT_ID);
    x11_put_u16(reply + 12, (uint16_t)x);
    x11_put_u16(reply + 14, (uint16_t)y);
    x11_put_u16(reply + 16, (uint16_t)w);
    x11_put_u16(reply + 18, (uint16_t)h);
    x11_put_u16(reply + 20, 0);
    x11_tx_append(c, reply, 32);
}

static void x11_req_query_tree(x11_client_t *c, const uint8_t *p)
{
    uint32_t window = x11_get_u32(p + 4);
    uint32_t children[128];
    uint32_t nchildren = 0;
    if (window == X11_ROOT_ID) {
        for (uint32_t i = 0; i < X11_MAX_RESOURCES && nchildren < 128; i++) {
            if (x11_resources[i].used && x11_resources[i].type == X11_RES_WIN)
                children[nchildren++] = x11_resources[i].id;
        }
    }
    uint8_t hdr[32];
    memset(hdr, 0, 32);
    hdr[0] = 1;
    x11_put_u16(hdr + 2, (uint16_t)c->seq);
    x11_put_u32(hdr + 4, nchildren);
    x11_put_u32(hdr + 8, X11_ROOT_ID);
    x11_put_u32(hdr + 12, window == X11_ROOT_ID ? 0 : X11_ROOT_ID);
    x11_put_u16(hdr + 16, (uint16_t)nchildren);
    x11_tx_append(c, hdr, 32);
    if (nchildren > 0) x11_tx_append(c, children, nchildren * 4);
    uint32_t total = 32 + nchildren * 4;
    uint32_t pad = (4 - (total & 3)) & 3;
    if (pad) { uint8_t z[4] = {0,0,0,0}; x11_tx_append(c, z, pad); }
}

static void x11_req_no_operation(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_get_input_focus(x11_client_t *c)
{
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 8, 0);
    reply[12] = 0;
    x11_tx_append(c, reply, 32);
}

static void x11_req_set_input_focus(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_query_pointer(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 8, X11_ROOT_ID);
    x11_put_u32(reply + 12, 0);
    x11_put_u16(reply + 16, 0);
    x11_put_u16(reply + 18, 0);
    x11_put_u16(reply + 20, 0);
    x11_put_u16(reply + 22, 0);
    x11_put_u16(reply + 24, 0);
    x11_tx_append(c, reply, 32);
}

static void x11_req_grab_server(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_ungrab_server(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_allow_events(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_bell(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_set_screen_saver(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_list_extensions(x11_client_t *c)
{
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    reply[1] = 0;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_tx_append(c, reply, 32);
}

static void x11_req_query_extension(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    reply[8] = 0;
    x11_tx_append(c, reply, 32);
}

static void x11_req_kill_client(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_set_close_down_mode(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_force_screen_saver(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_change_hosts(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_list_hosts(x11_client_t *c)
{
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    reply[8] = 0;
    x11_tx_append(c, reply, 32);
}

static void x11_req_set_access_control(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_rotate_properties(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_get_selection_owner(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 8, 0);
    x11_tx_append(c, reply, 32);
}

static void x11_req_set_selection_owner(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_convert_selection(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_send_event(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_grab_pointer(x11_client_t *c, const uint8_t *p)
{
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    reply[1] = 0;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_tx_append(c, reply, 32);
}

static void x11_req_ungrab_pointer(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_grab_keyboard(x11_client_t *c, const uint8_t *p)
{
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    reply[1] = 0;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_tx_append(c, reply, 32);
}

static void x11_req_ungrab_keyboard(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_query_keymap(x11_client_t *c)
{
    uint8_t reply[40];
    memset(reply, 0, 40);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 4, 2);
    x11_tx_append(c, reply, 40);
}

static void x11_req_query_font(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[60];
    memset(reply, 0, 60);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 4, 7);
    x11_put_u16(reply + 12, 8);
    x11_put_u16(reply + 14, 8);
    x11_put_u16(reply + 16, 0);
    x11_put_u16(reply + 18, 8);
    x11_put_u16(reply + 20, 0);
    x11_put_u16(reply + 22, 8);
    x11_tx_append(c, reply, 60);
}

static void x11_req_open_font(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_close_font(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_list_fonts(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    const char *fixed = "fixed";
    uint16_t len = 5;
    uint8_t hdr[32];
    memset(hdr, 0, 32);
    hdr[0] = 1;
    x11_put_u16(hdr + 2, (uint16_t)c->seq);
    x11_put_u32(hdr + 4, (2 + len + 3) / 4);
    x11_put_u16(hdr + 8, 1);
    x11_tx_append(c, hdr, 32);
    uint8_t nm[8];
    nm[0] = (uint8_t)len;
    nm[1] = 0;
    memcpy(nm + 2, fixed, len);
    x11_tx_append(c, nm, 2 + len);
    uint32_t total = 32 + 2 + len;
    uint32_t pad = (4 - (total & 3)) & 3;
    if (pad) { uint8_t z[4] = {0,0,0,0}; x11_tx_append(c, z, pad); }
}

static void x11_req_get_font_path(x11_client_t *c)
{
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u16(reply + 8, 0);
    x11_tx_append(c, reply, 32);
}

static void x11_req_create_pixmap(x11_client_t *c, const uint8_t *p)
{
    uint8_t depth = p[1];
    (void)depth;
    uint32_t pid = x11_get_u32(p + 4);
    uint32_t drawable = x11_get_u32(p + 8);
    (void)drawable;
    uint16_t w = x11_get_u16(p + 12);
    uint16_t h = x11_get_u16(p + 14);
    x11_res_t *r = x11_res_alloc(pid, X11_RES_PIX);
    if (!r) { x11_send_error(c, 10, pid, 53, 0); return; }
    r->owner = c->client_index;
    r->w = w;
    r->h = h;
}

static void x11_req_free_pixmap(x11_client_t *c, const uint8_t *p)
{
    uint32_t pid = x11_get_u32(p + 4);
    x11_res_free(pid);
}

static void x11_req_copy_area(x11_client_t *c, const uint8_t *p)
{
    uint32_t src = x11_get_u32(p + 4);
    uint32_t dst = x11_get_u32(p + 8);
    int16_t src_x = (int16_t)x11_get_u16(p + 16);
    int16_t src_y = (int16_t)x11_get_u16(p + 18);
    int16_t dst_x = (int16_t)x11_get_u16(p + 20);
    int16_t dst_y = (int16_t)x11_get_u16(p + 22);
    uint16_t w = x11_get_u16(p + 24);
    uint16_t h = x11_get_u16(p + 26);
    x11_res_t *s = x11_drawable_lookup(c, src);
    x11_res_t *d = x11_drawable_lookup(c, dst);
    if (!s || !d) return;
    uint32_t *sc = s->surf_id >= 0 ? ax_x_window_canvas(s->surf_id) : NULL;
    uint32_t *dc = d->surf_id >= 0 ? ax_x_window_canvas(d->surf_id) : NULL;
    int scw = s->surf_id >= 0 ? ax_x_window_canvas_w(s->surf_id) : 0;
    int sch = s->surf_id >= 0 ? ax_x_window_canvas_h(s->surf_id) : 0;
    int dcw = d->surf_id >= 0 ? ax_x_window_canvas_w(d->surf_id) : 0;
    int dch = d->surf_id >= 0 ? ax_x_window_canvas_h(d->surf_id) : 0;
    if (!sc || !dc) return;
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int sx = src_x + xx;
            int sy = src_y + yy;
            int dx = dst_x + xx;
            int dy = dst_y + yy;
            if (sx >= 0 && sx < scw && sy >= 0 && sy < sch &&
                dx >= 0 && dx < dcw && dy >= 0 && dy < dch)
                dc[dy * dcw + dx] = sc[sy * scw + sx];
        }
    }
}

static void x11_req_free_colors(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_alloc_color(x11_client_t *c, const uint8_t *p)
{
    uint16_t r = x11_get_u16(p + 8);
    uint16_t g = x11_get_u16(p + 10);
    uint16_t b = x11_get_u16(p + 12);
    uint32_t pixel = 0xFF000000u | ((uint32_t)(r >> 8) << 16) | ((uint32_t)(g >> 8) << 8) | (b >> 8);
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u16(reply + 8, r);
    x11_put_u16(reply + 10, g);
    x11_put_u16(reply + 12, b);
    x11_put_u32(reply + 16, pixel);
    x11_tx_append(c, reply, 32);
}

static void x11_req_lookup_color(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u16(reply + 8, 0xFFFF);
    x11_put_u16(reply + 10, 0xFFFF);
    x11_put_u16(reply + 12, 0xFFFF);
    x11_put_u16(reply + 16, 0xFFFF);
    x11_put_u16(reply + 18, 0xFFFF);
    x11_put_u16(reply + 20, 0xFFFF);
    x11_tx_append(c, reply, 32);
}

static void x11_req_create_colormap(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_free_colormap(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_query_colors(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_tx_append(c, reply, 32);
}

static void x11_req_change_keyboard_mapping(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_get_keyboard_mapping(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[40];
    memset(reply, 0, 40);
    reply[0] = 1;
    reply[1] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 4, 2);
    x11_tx_append(c, reply, 40);
}

static void x11_req_change_keyboard_control(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_get_keyboard_control(x11_client_t *c)
{
    uint8_t reply[52];
    memset(reply, 0, 52);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 4, 5);
    x11_tx_append(c, reply, 52);
}

static void x11_req_get_motion_events(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 8, 0);
    x11_tx_append(c, reply, 32);
}

static void x11_req_translate_coords(x11_client_t *c, const uint8_t *p)
{
    uint32_t src = x11_get_u32(p + 4);
    uint32_t dst = x11_get_u32(p + 8);
    int16_t x = (int16_t)x11_get_u16(p + 12);
    int16_t y = (int16_t)x11_get_u16(p + 14);
    (void)src; (void)dst;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    reply[1] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u16(reply + 8, 0);
    x11_put_u16(reply + 10, (uint16_t)x);
    x11_put_u16(reply + 12, (uint16_t)y);
    x11_tx_append(c, reply, 32);
}

static void x11_req_warp_pointer(x11_client_t *c, const uint8_t *p)
{
    (void)c; (void)p;
}

static void x11_req_set_pointer_mapping(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    reply[1] = 0;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_tx_append(c, reply, 32);
}

static void x11_req_get_pointer_mapping(x11_client_t *c)
{
    uint8_t reply[40];
    memset(reply, 0, 40);
    reply[0] = 1;
    reply[1] = 3;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 4, 2);
    reply[8] = 1;
    reply[9] = 2;
    reply[10] = 3;
    x11_tx_append(c, reply, 40);
}

static void x11_req_set_modifier_mapping(x11_client_t *c, const uint8_t *p)
{
    (void)p;
    uint8_t reply[32];
    memset(reply, 0, 32);
    reply[0] = 1;
    reply[1] = 0;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_tx_append(c, reply, 32);
}

static void x11_req_get_modifier_mapping(x11_client_t *c)
{
    uint8_t reply[40];
    memset(reply, 0, 40);
    reply[0] = 1;
    x11_put_u16(reply + 2, (uint16_t)c->seq);
    x11_put_u32(reply + 4, 2);
    x11_tx_append(c, reply, 40);
}

static void x11_dispatch(x11_client_t *c)
{
    while (c->rx_len >= 4) {
        uint8_t opcode = c->rx[0];
        uint16_t req_len = x11_get_u16(c->rx + 2);
        if (req_len == 0) { c->rx_len = 0; return; }
        uint32_t total = (uint32_t)req_len * 4;
        if (c->rx_len < total) return;
        const uint8_t *p = c->rx;
        c->seq++;

        switch (opcode) {
            case 1:  x11_req_create_window(c, p); break;
            case 2:  x11_req_change_window_attributes(c, p); break;
            case 3:  {
                uint32_t wid = x11_get_u32(p + 4);
                x11_res_t *r = x11_res_find(wid);
                uint8_t reply[32];
                memset(reply, 0, 32);
                reply[0] = 1;
                x11_put_u16(reply + 2, (uint16_t)c->seq);
                if (r) {
                    reply[8] = 2;
                    int x = 0, y = 0, w = 100, h = 100;
                    if (r->surf_id >= 0) ax_x_window_get_geom(r->surf_id, &x, &y, &w, &h);
                    x11_put_u16(reply + 12, (uint16_t)x);
                    x11_put_u16(reply + 14, (uint16_t)y);
                    x11_put_u16(reply + 16, (uint16_t)w);
                    x11_put_u16(reply + 18, (uint16_t)h);
                } else {
                    reply[8] = 0;
                }
                x11_tx_append(c, reply, 32);
                break;
            }
            case 4:  x11_req_destroy_window(c, p); break;
            case 5:  x11_req_destroy_window(c, p); break;
            case 6:  x11_req_no_operation(c, p); break;
            case 7:  x11_req_no_operation(c, p); break;
            case 8:  x11_req_map_window(c, p); break;
            case 9:  x11_req_no_operation(c, p); break;
            case 10: x11_req_unmap_window(c, p); break;
            case 11: x11_req_no_operation(c, p); break;
            case 12: x11_req_configure_window(c, p); break;
            case 13: x11_req_no_operation(c, p); break;
            case 14: x11_req_get_geometry(c, p); break;
            case 15: x11_req_query_tree(c, p); break;
            case 16: x11_req_intern_atom(c, p); break;
            case 17: x11_req_get_atom_name(c, p); break;
            case 18: x11_req_change_property(c, p, req_len); break;
            case 19: {
                uint32_t window = x11_get_u32(p + 4);
                uint32_t property = x11_get_u32(p + 8);
                x11_prop_t *prop = x11_prop_find(window, property);
                if (prop) prop->used = 0;
                break;
            }
            case 20: x11_req_get_property(c, p); break;
            case 21: x11_req_list_extensions(c); break;
            case 22: x11_req_set_selection_owner(c, p); break;
            case 23: x11_req_get_selection_owner(c, p); break;
            case 24: x11_req_convert_selection(c, p); break;
            case 25: x11_req_send_event(c, p); break;
            case 26: x11_req_grab_pointer(c, p); break;
            case 27: x11_req_ungrab_pointer(c, p); break;
            case 28: x11_req_no_operation(c, p); break;
            case 29: x11_req_no_operation(c, p); break;
            case 30: x11_req_no_operation(c, p); break;
            case 31: x11_req_grab_keyboard(c, p); break;
            case 32: x11_req_ungrab_keyboard(c, p); break;
            case 33: x11_req_no_operation(c, p); break;
            case 34: x11_req_no_operation(c, p); break;
            case 35: x11_req_no_operation(c, p); break;
            case 36: x11_req_grab_server(c, p); break;
            case 37: x11_req_ungrab_server(c, p); break;
            case 38: x11_req_query_pointer(c, p); break;
            case 39: x11_req_get_motion_events(c, p); break;
            case 40: x11_req_translate_coords(c, p); break;
            case 41: x11_req_warp_pointer(c, p); break;
            case 42: x11_req_set_input_focus(c, p); break;
            case 43: x11_req_get_input_focus(c); break;
            case 44: x11_req_query_keymap(c); break;
            case 45: x11_req_open_font(c, p); break;
            case 46: x11_req_close_font(c, p); break;
            case 47: x11_req_query_font(c, p); break;
            case 48: x11_req_no_operation(c, p); break;
            case 49: x11_req_list_fonts(c, p); break;
            case 50: x11_req_no_operation(c, p); break;
            case 51: x11_req_get_font_path(c); break;
            case 52: x11_req_no_operation(c, p); break;
            case 53: x11_req_create_pixmap(c, p); break;
            case 54: x11_req_free_pixmap(c, p); break;
            case 55: x11_req_create_gc(c, p); break;
            case 56: x11_req_change_gc(c, p); break;
            case 57: x11_req_no_operation(c, p); break;
            case 58: x11_req_no_operation(c, p); break;
            case 59: x11_req_no_operation(c, p); break;
            case 60: x11_req_free_gc(c, p); break;
            case 61: x11_req_no_operation(c, p); break;
            case 62: x11_req_copy_area(c, p); break;
            case 63: x11_req_no_operation(c, p); break;
            case 64: x11_req_no_operation(c, p); break;
            case 65: x11_req_poly_line(c, p, req_len); break;
            case 66: x11_req_poly_segment(c, p, req_len); break;
            case 67: x11_req_poly_rectangle(c, p, req_len); break;
            case 68: x11_req_no_operation(c, p); break;
            case 69: x11_req_no_operation(c, p); break;
            case 70: x11_req_poly_fill_rectangle(c, p, req_len); break;
            case 71: x11_req_no_operation(c, p); break;
            case 72: x11_req_put_image(c, p, req_len); break;
            case 73: x11_req_get_image(c, p); break;
            case 74: x11_req_image_text(c, p, req_len, 0); break;
            case 75: x11_req_image_text(c, p, req_len, 1); break;
            case 76: x11_req_image_text(c, p, req_len, 0); break;
            case 77: x11_req_image_text(c, p, req_len, 1); break;
            case 78: x11_req_create_colormap(c, p); break;
            case 79: x11_req_free_colormap(c, p); break;
            case 80: x11_req_no_operation(c, p); break;
            case 81: x11_req_no_operation(c, p); break;
            case 82: x11_req_no_operation(c, p); break;
            case 83: x11_req_no_operation(c, p); break;
            case 84: x11_req_alloc_color(c, p); break;
            case 85: x11_req_no_operation(c, p); break;
            case 86: x11_req_no_operation(c, p); break;
            case 87: x11_req_no_operation(c, p); break;
            case 88: x11_req_free_colors(c, p); break;
            case 89: x11_req_no_operation(c, p); break;
            case 90: x11_req_no_operation(c, p); break;
            case 91: x11_req_query_colors(c, p); break;
            case 92: x11_req_lookup_color(c, p); break;
            case 93: x11_req_no_operation(c, p); break;
            case 94: x11_req_no_operation(c, p); break;
            case 95: x11_req_no_operation(c, p); break;
            case 96: x11_req_no_operation(c, p); break;
            case 97: x11_req_no_operation(c, p); break;
            case 98: x11_req_query_extension(c, p); break;
            case 99: x11_req_list_extensions(c); break;
            case 100: x11_req_change_keyboard_mapping(c, p); break;
            case 101: x11_req_get_keyboard_mapping(c, p); break;
            case 102: x11_req_change_keyboard_control(c, p); break;
            case 103: x11_req_get_keyboard_control(c); break;
            case 104: x11_req_bell(c, p); break;
            case 105: x11_req_no_operation(c, p); break;
            case 106: x11_req_no_operation(c, p); break;
            case 107: x11_req_set_screen_saver(c, p); break;
            case 108: x11_req_no_operation(c, p); break;
            case 109: x11_req_force_screen_saver(c, p); break;
            case 110: x11_req_change_hosts(c, p); break;
            case 111: x11_req_list_hosts(c); break;
            case 112: x11_req_set_access_control(c, p); break;
            case 113: x11_req_no_operation(c, p); break;
            case 114: x11_req_rotate_properties(c, p); break;
            case 115: x11_req_set_close_down_mode(c, p); break;
            case 116: x11_req_kill_client(c, p); break;
            case 117: x11_req_no_operation(c, p); break;
            case 118: x11_req_set_modifier_mapping(c, p); break;
            case 119: x11_req_get_modifier_mapping(c); break;
            case 120: x11_req_no_operation(c, p); break;
            case 121: x11_req_set_pointer_mapping(c, p); break;
            case 122: x11_req_get_pointer_mapping(c); break;
            case 127: x11_req_no_operation(c, p); break;
            default:
                x11_send_error(c, 1, opcode, opcode, 0);
                break;
        }

        memmove(c->rx, c->rx + total, c->rx_len - total);
        c->rx_len -= total;
        x11_tx_flush(c);
    }
}

static x11_client_t *x11_client_alloc(int fd)
{
    for (uint32_t i = 0; i < X11_MAX_CLIENTS; i++) {
        if (!x11_clients[i].used) {
            memset(&x11_clients[i], 0, sizeof(x11_client_t));
            x11_clients[i].used = 1;
            x11_clients[i].fd = fd;
            x11_clients[i].client_index = i;
            return &x11_clients[i];
        }
    }
    return NULL;
}

static void x11_client_close(x11_client_t *c)
{
    for (uint32_t i = 0; i < X11_MAX_RESOURCES; i++) {
        if (x11_resources[i].used && x11_resources[i].owner == c->client_index) {
            if (x11_resources[i].type == X11_RES_WIN && x11_resources[i].surf_id >= 0)
                ax_x_window_destroy(x11_resources[i].surf_id);
            x11_resources[i].used = 0;
        }
    }
    if (c->sock) ksock_close(c->sock);
    c->used = 0;
}

void x11_init(int screen_w, int screen_h)
{
    x11_scr_w = screen_w;
    x11_scr_h = screen_h;
    x11_atom_count = 0;
    x11_event_seq = 0;
    memset(x11_clients, 0, sizeof(x11_clients));
    memset(x11_resources, 0, sizeof(x11_resources));
    memset(x11_props, 0, sizeof(x11_props));

    x11_atom_intern("PRIMARY", 0);
    x11_atom_intern("SECONDARY", 0);
    x11_atom_intern("ARC", 0);
    x11_atom_intern("ATOM", 0);
    x11_atom_intern("BITMAP", 0);
    x11_atom_intern("CARDINAL", 0);
    x11_atom_intern("COLORMAP", 0);
    x11_atom_intern("CURSOR", 0);
    x11_atom_intern("DRAWABLE", 0);
    x11_atom_intern("FONT", 0);
    x11_atom_intern("INTEGER", 0);
    x11_atom_intern("PIXMAP", 0);
    x11_atom_intern("POINT", 0);
    x11_atom_intern("RECTANGLE", 0);
    x11_atom_intern("STRING", 0);
    x11_atom_intern("VISUALID", 0);
    x11_atom_intern("WINDOW", 0);
    x11_atom_intern("WM_COMMAND", 0);
    x11_atom_intern("WM_HINTS", 0);
    x11_atom_intern("WM_CLIENT_MACHINE", 0);
    x11_atom_intern("WM_ICON_NAME", 0);
    x11_atom_intern("WM_ICON_SIZE", 0);
    x11_atom_intern("WM_NAME", 0);
    x11_atom_intern("WM_NORMAL_HINTS", 0);
    x11_atom_intern("WM_SIZE_HINTS", 0);
    x11_atom_intern("WM_ZOOM_HINTS", 0);
    x11_atom_intern("WM_STATE", 0);
    x11_atom_intern("WM_CLASS", 0);
    x11_atom_intern("WM_TRANSIENT_FOR", 0);
    x11_atom_intern("WM_PROTOCOLS", 0);
    x11_atom_intern("WM_DELETE_WINDOW", 0);
    x11_atom_intern("WM_TAKE_FOCUS", 0);
    x11_atom_intern("_NET_SUPPORTED", 0);
    x11_atom_intern("_NET_SUPPORTING_WM_CHECK", 0);
    x11_atom_intern("_NET_WM_NAME", 0);
    x11_atom_intern("_NET_WM_STATE", 0);
    x11_atom_intern("_NET_WM_WINDOW_TYPE", 0);
    x11_atom_intern("_NET_WM_PID", 0);
    x11_atom_intern("_NET_ACTIVE_WINDOW", 0);
    x11_atom_intern("_NET_CLIENT_LIST", 0);
    x11_atom_intern("UTF8_STRING", 0);

    x11_listen_sock = ksock_create();
    if (x11_listen_sock) {
        if (ksock_bind(x11_listen_sock, "/tmp/.X11-unix/X0") == 0) {
            ksock_listen(x11_listen_sock, 8);
            x11_listen_fd = 1;
        } else {
            ksock_close(x11_listen_sock);
            x11_listen_sock = NULL;
            x11_listen_fd = -1;
        }
    } else {
        x11_listen_fd = -1;
    }
}

int x11_listener_fd(void)
{
    return x11_listen_fd;
}

int x11_is_x_window(int surf_id)
{
    for (uint32_t i = 0; i < X11_MAX_RESOURCES; i++)
        if (x11_resources[i].used && x11_resources[i].type == X11_RES_WIN &&
            x11_resources[i].surf_id == surf_id)
            return 1;
    return 0;
}

void x11_on_window_destroyed(int surf_id)
{
    for (uint32_t i = 0; i < X11_MAX_RESOURCES; i++) {
        if (x11_resources[i].used && x11_resources[i].type == X11_RES_WIN &&
            x11_resources[i].surf_id == surf_id) {
            x11_resources[i].used = 0;
        }
    }
}

void x11_notify_window_event(int surf_id, int ev_kind, int x, int y, int w, int h)
{
    x11_res_t *r = NULL;
    for (uint32_t i = 0; i < X11_MAX_RESOURCES; i++) {
        if (x11_resources[i].used && x11_resources[i].type == X11_RES_WIN &&
            x11_resources[i].surf_id == surf_id) {
            r = &x11_resources[i];
            break;
        }
    }
    if (!r) return;
    x11_client_t *c = &x11_clients[r->owner];
    if (!c->used) return;

    uint8_t ev[32];
    memset(ev, 0, 32);
    x11_put_u16(ev + 2, (uint16_t)x11_event_seq++);
    x11_put_u32(ev + 4, r->id);
    x11_put_u32(ev + 8, r->id);

    switch (ev_kind) {
        case X11_EV_EXPOSE:
            ev[0] = X_EV_EXPOSE;
            x11_put_u16(ev + 12, (uint16_t)w);
            x11_put_u16(ev + 14, (uint16_t)h);
            break;
        case X11_EV_RESIZE:
            ev[0] = X_EV_CONFIGURE_NOTIFY;
            x11_put_u16(ev + 12, (uint16_t)x);
            x11_put_u16(ev + 14, (uint16_t)y);
            x11_put_u16(ev + 16, (uint16_t)w);
            x11_put_u16(ev + 18, (uint16_t)h);
            break;
        case X11_EV_DESTROY:
            ev[0] = 17;
            break;
        case X11_EV_FOCUS_IN:
            ev[0] = X_EV_FOCUS_IN;
            break;
        case X11_EV_BUTTON:
            ev[0] = X_EV_BUTTON_PRESS;
            x11_put_u16(ev + 12, (uint16_t)x);
            x11_put_u16(ev + 14, (uint16_t)y);
            break;
        case X11_EV_MOTION:
            ev[0] = X_EV_MOTION_NOTIFY;
            x11_put_u16(ev + 12, (uint16_t)x);
            x11_put_u16(ev + 14, (uint16_t)y);
            break;
        case X11_EV_KEY:
            ev[0] = X_EV_KEY_PRESS;
            ev[1] = (uint8_t)w;
            break;
        default:
            return;
    }

    if (r->ev_mask & X_MASK_EXPOSURE || ev_kind != X11_EV_EXPOSE)
        x11_send_event_to(c, ev);
    x11_tx_flush(c);
}

void x11_poll(void)
{
    if (!x11_listen_sock) return;

    ksock_t *incoming = ksock_accept(x11_listen_sock);
    while (incoming) {
        x11_client_t *c = NULL;
        for (uint32_t i = 0; i < X11_MAX_CLIENTS; i++) {
            if (!x11_clients[i].used) {
                c = &x11_clients[i];
                break;
            }
        }
        if (!c) { ksock_close(incoming); break; }
        memset(c, 0, sizeof(*c));
        c->used = 1;
        c->sock = incoming;
        c->fd = -1;
        c->client_index = (uint32_t)(c - x11_clients);
        incoming = ksock_accept(x11_listen_sock);
    }

    for (uint32_t i = 0; i < X11_MAX_CLIENTS; i++) {
        x11_client_t *c = &x11_clients[i];
        if (!c->used) continue;
        uint8_t tmp[4096];
        int got = ksock_recv(c->sock, tmp, sizeof(tmp), 1);
        if (got == KSOCK_EAGAIN) {
            x11_tx_flush(c);
            continue;
        }
        if (got <= 0) {
            if (got == 0) x11_client_close(c);
            continue;
        }
        if (c->rx_len + (uint32_t)got > X11_RX_CAP) {
            c->rx_len = 0;
            continue;
        }
        memcpy(c->rx + c->rx_len, tmp, (uint32_t)got);
        c->rx_len += (uint32_t)got;

        if (!c->handshake_done) {
            if (c->rx_len >= 12) {
                uint16_t n = x11_get_u16(c->rx + 6);
                uint16_t d = x11_get_u16(c->rx + 8);
                uint32_t need = 12 + ((n + 3) & ~3u) + ((d + 3) & ~3u);
                if (c->rx_len >= need) {
                    x11_handshake(c, c->rx, need);
                    memmove(c->rx, c->rx + need, c->rx_len - need);
                    c->rx_len -= need;
                }
            }
        }
        if (c->handshake_done)
            x11_dispatch(c);
    }
}
