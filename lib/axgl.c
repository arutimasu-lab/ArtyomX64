#include "axgl.h"
#include <string.h>
#include "../mm/malloc.h"

static float axgl_sqrtf(float x)
{
    union { float f; uint32_t u; } c;
    if (x <= 0.0f) return 0.0f;
    c.f = x;
    c.u = 0x5f3759dfu - (c.u >> 1);
    c.f = c.f * (1.5f - 0.5f * x * c.f * c.f);
    return x * c.f;
}

static float axgl_fabsf(float x) { return x < 0.0f ? -x : x; }
static float axgl_floorf(float x) { return (float)((int)x - (x < 0.0f && x != (float)(int)x)); }
static float axgl_ceilf(float x) { return (float)((int)x + (x > 0.0f && x != (float)(int)x)); }

static float axgl_sinf(float x)
{
    float x2 = x * x;
    float r = x;
    float term = x;
    for (int i = 1; i <= 5; i++) {
        term *= -x2 / ((float)(2 * i) * (float)(2 * i + 1));
        r += term;
    }
    return r;
}

static float axgl_cosf(float x)
{
    return axgl_sinf(x + 3.14159265f / 2.0f);
}

static float axgl_tanf(float x)
{
    return axgl_sinf(x) / axgl_cosf(x);
}

#define sqrtf axgl_sqrtf
#define fabsf axgl_fabsf
#define floorf axgl_floorf
#define ceilf axgl_ceilf
#define sinf axgl_sinf
#define cosf axgl_cosf
#define tanf axgl_tanf

static void axgl_mat_identity(axgl_mat4_t *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = 1.0f;
    m->m[5] = 1.0f;
    m->m[10] = 1.0f;
    m->m[15] = 1.0f;
}

static void axgl_mat_mult(axgl_mat4_t *out, const axgl_mat4_t *a, const axgl_mat4_t *b)
{
    axgl_mat4_t r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += a->m[k * 4 + j] * b->m[i * 4 + k];
            r.m[i * 4 + j] = s;
        }
    }
    *out = r;
}

static void axgl_update_mvp(axgl_ctx_t *ctx)
{
    axgl_mat_mult(&ctx->mvp, &ctx->projection, &ctx->modelview);
}

void axgl_init(axgl_ctx_t *ctx, uint32_t *color_buf, float *depth_buf, int w, int h)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->color_buf = color_buf;
    ctx->depth_buf = depth_buf;
    ctx->width = w;
    ctx->height = h;
    ctx->vb = (axgl_vertex_t*)malloc(AXGL_MAX_VERTICES * sizeof(axgl_vertex_t));
    ctx->ib = (uint32_t*)malloc(AXGL_MAX_INDICES * sizeof(uint32_t));
    ctx->vb_count = 0;
    ctx->ib_count = 0;
    axgl_mat_identity(&ctx->modelview);
    axgl_mat_identity(&ctx->projection);
    axgl_mat_identity(&ctx->mvp);
    ctx->clear_r = 0.0f; ctx->clear_g = 0.0f; ctx->clear_b = 0.0f; ctx->clear_a = 1.0f;
    ctx->clear_depth = 1.0f;
    ctx->depth_func = AXGL_LESS;
    ctx->blend_src = AXGL_SRC_ALPHA;
    ctx->blend_dst = AXGL_ONE_MINUS_SRC_ALPHA;
    ctx->cull_mode = AXGL_BACK;
    ctx->front_face = AXGL_CCW;
    ctx->bound_texture = 0;
}

void axgl_viewport(axgl_ctx_t *ctx, int x, int y, int w, int h)
{
    (void)x; (void)y;
    ctx->width = w;
    ctx->height = h;
}

void axgl_clear_color(axgl_ctx_t *ctx, float r, float g, float b, float a)
{
    ctx->clear_r = r; ctx->clear_g = g; ctx->clear_b = b; ctx->clear_a = a;
}

void axgl_clear_depth(axgl_ctx_t *ctx, float d) { ctx->clear_depth = d; }

void axgl_clear(axgl_ctx_t *ctx, bool color, bool depth)
{
    if (color && ctx->color_buf) {
        uint8_t r = (uint8_t)(ctx->clear_r * 255.0f);
        uint8_t g = (uint8_t)(ctx->clear_g * 255.0f);
        uint8_t b = (uint8_t)(ctx->clear_b * 255.0f);
        uint8_t a = (uint8_t)(ctx->clear_a * 255.0f);
        uint32_t c = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        for (int i = 0; i < ctx->width * ctx->height; i++)
            ctx->color_buf[i] = c;
    }
    if (depth && ctx->depth_buf) {
        for (int i = 0; i < ctx->width * ctx->height; i++)
            ctx->depth_buf[i] = ctx->clear_depth;
    }
}

void axgl_matrix_mode(axgl_ctx_t *ctx, int mode)
{
    (void)mode;
}

void axgl_load_identity(axgl_ctx_t *ctx)
{
    axgl_mat_identity(&ctx->modelview);
    axgl_update_mvp(ctx);
}

void axgl_load_matrix(axgl_ctx_t *ctx, const axgl_mat4_t *m)
{
    ctx->modelview = *m;
    axgl_update_mvp(ctx);
}

void axgl_mult_matrix(axgl_ctx_t *ctx, const axgl_mat4_t *m)
{
    axgl_mat4_t r;
    axgl_mat_mult(&r, &ctx->modelview, m);
    ctx->modelview = r;
    axgl_update_mvp(ctx);
}

void axgl_perspective(axgl_ctx_t *ctx, float fovy, float aspect, float znear, float zfar)
{
    axgl_mat4_t m;
    memset(&m, 0, sizeof(m));
    float f = 1.0f / tanf(fovy * 0.5f * 3.14159265f / 180.0f);
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = (zfar + znear) / (znear - zfar);
    m.m[11] = -1.0f;
    m.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    ctx->projection = m;
    axgl_update_mvp(ctx);
}

void axgl_ortho(axgl_ctx_t *ctx, float l, float r, float b, float t, float n, float f)
{
    axgl_mat4_t m;
    memset(&m, 0, sizeof(m));
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    m.m[15] = 1.0f;
    ctx->projection = m;
    axgl_update_mvp(ctx);
}

void axgl_translate(axgl_ctx_t *ctx, float x, float y, float z)
{
    axgl_mat4_t m;
    axgl_mat_identity(&m);
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    axgl_mult_matrix(ctx, &m);
}

void axgl_rotate(axgl_ctx_t *ctx, float angle, float x, float y, float z)
{
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 0.0001f) return;
    x /= len; y /= len; z /= len;
    float c = cosf(angle * 3.14159265f / 180.0f);
    float s = sinf(angle * 3.14159265f / 180.0f);
    float t = 1.0f - c;
    axgl_mat4_t m;
    axgl_mat_identity(&m);
    m.m[0] = t * x * x + c;
    m.m[1] = t * x * y + s * z;
    m.m[2] = t * x * z - s * y;
    m.m[4] = t * x * y - s * z;
    m.m[5] = t * y * y + c;
    m.m[6] = t * y * z + s * x;
    m.m[8] = t * x * z + s * y;
    m.m[9] = t * y * z - s * x;
    m.m[10] = t * z * z + c;
    axgl_mult_matrix(ctx, &m);
}

void axgl_scale(axgl_ctx_t *ctx, float x, float y, float z)
{
    axgl_mat4_t m;
    memset(&m, 0, sizeof(m));
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    m.m[15] = 1.0f;
    axgl_mult_matrix(ctx, &m);
}

void axgl_enable(axgl_ctx_t *ctx, int cap)
{
    if (cap == AXGL_DEPTH_TEST) ctx->depth_test = true;
    else if (cap == AXGL_BLEND) ctx->blend = true;
    else if (cap == AXGL_CULL_FACE) ctx->cull_face = true;
    else if (cap == AXGL_TEXTURE_2D) ctx->texture_2d = true;
    else if (cap == AXGL_ALPHA_TEST) ctx->alpha_test = true;
}

void axgl_disable(axgl_ctx_t *ctx, int cap)
{
    if (cap == AXGL_DEPTH_TEST) ctx->depth_test = false;
    else if (cap == AXGL_BLEND) ctx->blend = false;
    else if (cap == AXGL_CULL_FACE) ctx->cull_face = false;
    else if (cap == AXGL_TEXTURE_2D) ctx->texture_2d = false;
    else if (cap == AXGL_ALPHA_TEST) ctx->alpha_test = false;
}

void axgl_depth_func(axgl_ctx_t *ctx, int func) { ctx->depth_func = func; }
void axgl_blend_func(axgl_ctx_t *ctx, int src, int dst) { ctx->blend_src = src; ctx->blend_dst = dst; }
void axgl_cull_face(axgl_ctx_t *ctx, int mode) { ctx->cull_mode = mode; }
void axgl_front_face(axgl_ctx_t *ctx, int mode) { ctx->front_face = mode; }
void axgl_alpha_func(axgl_ctx_t *ctx, int func, float ref) { (void)ctx; (void)func; (void)ref; }

int axgl_gen_texture(axgl_ctx_t *ctx)
{
    if (ctx->texture_count >= AXGL_MAX_TEXTURES) return 0;
    int id = ++ctx->texture_count;
    ctx->textures[id - 1].pixels = 0;
    ctx->textures[id - 1].width = 0;
    ctx->textures[id - 1].height = 0;
    ctx->textures[id - 1].has_alpha = false;
    return id;
}

void axgl_bind_texture(axgl_ctx_t *ctx, int tex)
{
    if (tex >= 0 && tex <= ctx->texture_count)
        ctx->bound_texture = tex;
}

void axgl_tex_image_2d(axgl_ctx_t *ctx, int tex, int w, int h, const uint32_t *pixels, bool has_alpha)
{
    if (tex < 1 || tex > ctx->texture_count) return;
    axgl_texture_t *t = &ctx->textures[tex - 1];
    t->pixels = (uint32_t*)malloc((size_t)w * h * 4);
    if (t->pixels && pixels) {
        memcpy(t->pixels, pixels, (size_t)w * h * 4);
    }
    t->width = w;
    t->height = h;
    t->has_alpha = has_alpha;
}

void axgl_begin(axgl_ctx_t *ctx, int mode)
{
    ctx->mode = mode;
    ctx->vb_count = 0;
    ctx->ib_count = 0;
}

static void axgl_emit_vertex(axgl_ctx_t *ctx, float x, float y, float z,
                              float u, float v, float r, float g, float b, float a)
{
    if (ctx->vb_count >= AXGL_MAX_VERTICES) return;
    axgl_vertex_t *vt = &ctx->vb[ctx->vb_count];
    float cx = ctx->mvp.m[0] * x + ctx->mvp.m[4] * y + ctx->mvp.m[8] * z + ctx->mvp.m[12];
    float cy = ctx->mvp.m[1] * x + ctx->mvp.m[5] * y + ctx->mvp.m[9] * z + ctx->mvp.m[13];
    float cz = ctx->mvp.m[2] * x + ctx->mvp.m[6] * y + ctx->mvp.m[10] * z + ctx->mvp.m[14];
    float cw = ctx->mvp.m[3] * x + ctx->mvp.m[7] * y + ctx->mvp.m[11] * z + ctx->mvp.m[15];
    if (cw != 0.0f) {
        cx /= cw;
        cy /= cw;
        cz /= cw;
    }
    vt->x = (cx + 1.0f) * 0.5f * (float)ctx->width;
    vt->y = (1.0f - cy) * 0.5f * (float)ctx->height;
    vt->z = (cz + 1.0f) * 0.5f;
    vt->u = u;
    vt->v = v;
    vt->r = r;
    vt->g = g;
    vt->b = b;
    vt->a = a;
    ctx->vb_count++;
}

void axgl_vertex3f(axgl_ctx_t *ctx, float x, float y, float z)
{
    axgl_emit_vertex(ctx, x, y, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
}

void axgl_tex_coord2f(axgl_ctx_t *ctx, float u, float v)
{
    if (ctx->vb_count > 0) {
        ctx->vb[ctx->vb_count - 1].u = u;
        ctx->vb[ctx->vb_count - 1].v = v;
    }
}

void axgl_color4f(axgl_ctx_t *ctx, float r, float g, float b, float a)
{
    if (ctx->vb_count > 0) {
        ctx->vb[ctx->vb_count - 1].r = r;
        ctx->vb[ctx->vb_count - 1].g = g;
        ctx->vb[ctx->vb_count - 1].b = b;
        ctx->vb[ctx->vb_count - 1].a = a;
    }
}

void axgl_color3f(axgl_ctx_t *ctx, float r, float g, float b)
{
    axgl_color4f(ctx, r, g, b, 1.0f);
}

static uint32_t axgl_sample_texture(const axgl_texture_t *t, float u, float v)
{
    if (!t || !t->pixels || t->width <= 0 || t->height <= 0)
        return 0xFFFFFFFFu;
    u = u - floorf(u);
    v = v - floorf(v);
    int x = (int)(u * (float)t->width) % t->width;
    int y = (int)(v * (float)t->height) % t->height;
    if (x < 0) x += t->width;
    if (y < 0) y += t->height;
    return t->pixels[y * t->width + x];
}

static void axgl_draw_triangle(axgl_ctx_t *ctx, const axgl_vertex_t *v0,
                                const axgl_vertex_t *v1, const axgl_vertex_t *v2)
{
    float min_x = v0->x;
    if (v1->x < min_x) min_x = v1->x;
    if (v2->x < min_x) min_x = v2->x;
    float min_y = v0->y;
    if (v1->y < min_y) min_y = v1->y;
    if (v2->y < min_y) min_y = v2->y;
    float max_x = v0->x;
    if (v1->x > max_x) max_x = v1->x;
    if (v2->x > max_x) max_x = v2->x;
    float max_y = v0->y;
    if (v1->y > max_y) max_y = v1->y;
    if (v2->y > max_y) max_y = v2->y;

    int x0 = (int)floorf(min_x);
    int y0 = (int)floorf(min_y);
    int x1 = (int)ceilf(max_x);
    int y1 = (int)ceilf(max_y);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= ctx->width) x1 = ctx->width - 1;
    if (y1 >= ctx->height) y1 = ctx->height - 1;
    if (x0 > x1 || y0 > y1) return;

    float d = (v1->y - v2->y) * (v0->x - v2->x) + (v2->x - v1->x) * (v0->y - v2->y);
    if (fabsf(d) < 0.000001f) return;

    const axgl_texture_t *tex = (ctx->texture_2d && ctx->bound_texture > 0) ?
        &ctx->textures[ctx->bound_texture - 1] : 0;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = ((v1->y - v2->y) * (px - v2->x) + (v2->x - v1->x) * (py - v2->y)) / d;
            float w1 = ((v2->y - v0->y) * (px - v2->x) + (v0->x - v2->x) * (py - v2->y)) / d;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            float z = w0 * v0->z + w1 * v1->z + w2 * v2->z;
            int idx = y * ctx->width + x;

            if (ctx->depth_test) {
                bool pass = false;
                float old = ctx->depth_buf[idx];
                if (ctx->depth_func == AXGL_LESS) pass = z < old;
                else if (ctx->depth_func == AXGL_LEQUAL) pass = z <= old;
                else if (ctx->depth_func == AXGL_ALWAYS) pass = true;
                if (!pass) continue;
                ctx->depth_buf[idx] = z;
            }

            float u = w0 * v0->u + w1 * v1->u + w2 * v2->u;
            float v = w0 * v0->v + w1 * v1->v + w2 * v2->v;
            float r = w0 * v0->r + w1 * v1->r + w2 * v2->r;
            float g = w0 * v0->g + w1 * v1->g + w2 * v2->g;
            float b = w0 * v0->b + w1 * v1->b + w2 * v2->b;
            float a = w0 * v0->a + w1 * v1->a + w2 * v2->a;

            if (tex) {
                uint32_t tc = axgl_sample_texture(tex, u, v);
                float ta = (float)((tc >> 24) & 0xFF) / 255.0f;
                float tr = (float)((tc >> 16) & 0xFF) / 255.0f;
                float tg = (float)((tc >> 8) & 0xFF) / 255.0f;
                float tb = (float)(tc & 0xFF) / 255.0f;
                r *= tr; g *= tg; b *= tb;
                if (tex->has_alpha) a *= ta;
                if (ctx->alpha_test && a < 0.5f) continue;
            }

            uint8_t cr = (uint8_t)(r * 255.0f);
            uint8_t cg = (uint8_t)(g * 255.0f);
            uint8_t cb = (uint8_t)(b * 255.0f);
            uint8_t ca = (uint8_t)(a * 255.0f);
            uint32_t col = ((uint32_t)ca << 24) | ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | cb;

            if (ctx->blend && a < 1.0f) {
                uint32_t dst = ctx->color_buf[idx];
                float da = (float)((dst >> 24) & 0xFF) / 255.0f;
                float dr = (float)((dst >> 16) & 0xFF) / 255.0f;
                float dg = (float)((dst >> 8) & 0xFF) / 255.0f;
                float db = (float)(dst & 0xFF) / 255.0f;
                float src_a = a;
                float inv_a = 1.0f - a;
                float out_a = src_a + da * inv_a;
                float out_r = (r * src_a + dr * da * inv_a) / (out_a > 0.0f ? out_a : 1.0f);
                float out_g = (g * src_a + dg * da * inv_a) / (out_a > 0.0f ? out_a : 1.0f);
                float out_b = (b * src_a + db * da * inv_a) / (out_a > 0.0f ? out_a : 1.0f);
                cr = (uint8_t)(out_r * 255.0f);
                cg = (uint8_t)(out_g * 255.0f);
                cb = (uint8_t)(out_b * 255.0f);
                ca = (uint8_t)(out_a * 255.0f);
                col = ((uint32_t)ca << 24) | ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | cb;
            }

            ctx->color_buf[idx] = col;
        }
    }
}

void axgl_end(axgl_ctx_t *ctx)
{
    if (ctx->mode == AXGL_TRIANGLES) {
        for (int i = 0; i + 2 < ctx->vb_count; i += 3)
            axgl_draw_triangle(ctx, &ctx->vb[i], &ctx->vb[i + 1], &ctx->vb[i + 2]);
    } else if (ctx->mode == AXGL_QUADS) {
        for (int i = 0; i + 3 < ctx->vb_count; i += 4) {
            axgl_draw_triangle(ctx, &ctx->vb[i], &ctx->vb[i + 1], &ctx->vb[i + 2]);
            axgl_draw_triangle(ctx, &ctx->vb[i], &ctx->vb[i + 2], &ctx->vb[i + 3]);
        }
    }
}

void axgl_flush(axgl_ctx_t *ctx)
{
    axgl_end(ctx);
}
