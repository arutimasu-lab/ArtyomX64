#ifndef AXGL_H
#define AXGL_H

#include <stdint.h>
#include <stdbool.h>

#define AXGL_MAX_TEXTURES 64
#define AXGL_MAX_VERTICES 65536
#define AXGL_MAX_INDICES  98304

#define AXGL_TRIANGLES 4
#define AXGL_QUADS     7

#define AXGL_MODELVIEW  0x1700
#define AXGL_PROJECTION 0x1701

#define AXGL_DEPTH_TEST 0x0B71
#define AXGL_BLEND      0x0BE2
#define AXGL_CULL_FACE  0x0B44
#define AXGL_TEXTURE_2D 0x0DE1
#define AXGL_ALPHA_TEST 0x0BC0

#define AXGL_SRC_ALPHA 0x0302
#define AXGL_ONE_MINUS_SRC_ALPHA 0x0303
#define AXGL_ONE 1
#define AXGL_ZERO 0

#define AXGL_LESS 0x0201
#define AXGL_LEQUAL 0x0203
#define AXGL_ALWAYS 0x0207

#define AXGL_BACK 0x0405
#define AXGL_FRONT 0x0404
#define AXGL_CCW 0x0901
#define AXGL_CW 0x0900

typedef struct {
    float m[16];
} axgl_mat4_t;

typedef struct {
    uint32_t *pixels;
    int width, height;
    bool has_alpha;
} axgl_texture_t;

typedef struct {
    float x, y, z;
    float u, v;
    float r, g, b, a;
} axgl_vertex_t;

typedef struct {
    uint32_t *color_buf;
    float    *depth_buf;
    int width, height;
    axgl_texture_t textures[AXGL_MAX_TEXTURES];
    int texture_count;
    int bound_texture;
    int mode;
    axgl_mat4_t modelview;
    axgl_mat4_t projection;
    axgl_mat4_t mvp;
    bool depth_test;
    bool blend;
    bool cull_face;
    bool texture_2d;
    bool alpha_test;
    int depth_func;
    int blend_src;
    int blend_dst;
    int cull_mode;
    int front_face;
    axgl_vertex_t *vb;
    uint32_t *ib;
    int vb_count;
    int ib_count;
    float clear_r, clear_g, clear_b, clear_a;
    float clear_depth;
} axgl_ctx_t;

void axgl_init(axgl_ctx_t *ctx, uint32_t *color_buf, float *depth_buf, int w, int h);
void axgl_viewport(axgl_ctx_t *ctx, int x, int y, int w, int h);
void axgl_clear_color(axgl_ctx_t *ctx, float r, float g, float b, float a);
void axgl_clear_depth(axgl_ctx_t *ctx, float d);
void axgl_clear(axgl_ctx_t *ctx, bool color, bool depth);

void axgl_matrix_mode(axgl_ctx_t *ctx, int mode);
void axgl_load_identity(axgl_ctx_t *ctx);
void axgl_load_matrix(axgl_ctx_t *ctx, const axgl_mat4_t *m);
void axgl_mult_matrix(axgl_ctx_t *ctx, const axgl_mat4_t *m);
void axgl_perspective(axgl_ctx_t *ctx, float fovy, float aspect, float znear, float zfar);
void axgl_ortho(axgl_ctx_t *ctx, float l, float r, float b, float t, float n, float f);
void axgl_translate(axgl_ctx_t *ctx, float x, float y, float z);
void axgl_rotate(axgl_ctx_t *ctx, float angle, float x, float y, float z);
void axgl_scale(axgl_ctx_t *ctx, float x, float y, float z);

void axgl_enable(axgl_ctx_t *ctx, int cap);
void axgl_disable(axgl_ctx_t *ctx, int cap);
void axgl_depth_func(axgl_ctx_t *ctx, int func);
void axgl_blend_func(axgl_ctx_t *ctx, int src, int dst);
void axgl_cull_face(axgl_ctx_t *ctx, int mode);
void axgl_front_face(axgl_ctx_t *ctx, int mode);
void axgl_alpha_func(axgl_ctx_t *ctx, int func, float ref);

int  axgl_gen_texture(axgl_ctx_t *ctx);
void axgl_bind_texture(axgl_ctx_t *ctx, int tex);
void axgl_tex_image_2d(axgl_ctx_t *ctx, int tex, int w, int h, const uint32_t *pixels, bool has_alpha);

void axgl_begin(axgl_ctx_t *ctx, int mode);
void axgl_end(axgl_ctx_t *ctx);
void axgl_vertex3f(axgl_ctx_t *ctx, float x, float y, float z);
void axgl_tex_coord2f(axgl_ctx_t *ctx, float u, float v);
void axgl_color4f(axgl_ctx_t *ctx, float r, float g, float b, float a);
void axgl_color3f(axgl_ctx_t *ctx, float r, float g, float b);

void axgl_flush(axgl_ctx_t *ctx);

#endif
