#include "../lib/axgl.h"
#include "../lib/axipc.h"
#include "../lib/libax.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../lib/unistd.h"
#include "../lib/common.h"
//extern void yield(void);

static float craft_sinf(float x)
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

static float craft_cosf(float x)
{
    return craft_sinf(x + 3.14159265f / 2.0f);
}

#define sinf craft_sinf
#define cosf craft_cosf

#define CHUNK_SIZE 16
#define CHUNK_HEIGHT 64
#define WORLD_CHUNKS_X 4
#define WORLD_CHUNKS_Z 4
#define TEX_SIZE 16

#define BLOCK_AIR   0
#define BLOCK_GRASS 1
#define BLOCK_DIRT  2
#define BLOCK_STONE 3
#define BLOCK_WOOD  4
#define BLOCK_LEAVES 5
#define BLOCK_SAND  6

#define SCR_W 800
#define SCR_H 600

static axgl_ctx_t gl;
static uint32_t color_buf[SCR_W * SCR_H];
static float depth_buf[SCR_W * SCR_H];
static uint32_t *canvas;

static uint8_t world[WORLD_CHUNKS_X * CHUNK_SIZE][CHUNK_HEIGHT][WORLD_CHUNKS_Z * CHUNK_SIZE];

static float cam_x = 32.0f, cam_y = 40.0f, cam_z = 32.0f;
static float cam_yaw = 45.0f, cam_pitch = -20.0f;
static int mouse_dx, mouse_dy;

static uint32_t tex_grass_top[TEX_SIZE * TEX_SIZE];
static uint32_t tex_grass_side[TEX_SIZE * TEX_SIZE];
static uint32_t tex_dirt[TEX_SIZE * TEX_SIZE];
static uint32_t tex_stone[TEX_SIZE * TEX_SIZE];
static uint32_t tex_wood[TEX_SIZE * TEX_SIZE];
static uint32_t tex_leaves[TEX_SIZE * TEX_SIZE];
static uint32_t tex_sand[TEX_SIZE * TEX_SIZE];

static void gen_textures(void)
{
    uint32_t seed = 12345;
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++) {
        seed = seed * 1103515245u + 12345u;
        uint32_t v = (seed >> 16) & 0xFF;
        int x = i % TEX_SIZE, y = i / TEX_SIZE;
        tex_grass_top[i] = 0xFF000000u | ((0x5A + v % 20) << 16) | ((0xA0 + v % 30) << 8) | (0x40 + v % 20);
        tex_grass_side[i] = 0xFF000000u | ((0x7A + v % 15) << 16) | ((0x5A + v % 15) << 8) | (0x3A + v % 15);
        tex_dirt[i] = 0xFF000000u | ((0x8A + v % 20) << 16) | ((0x6A + v % 15) << 8) | (0x4A + v % 10);
        tex_stone[i] = 0xFF000000u | ((0x80 + v % 15) << 16) | ((0x80 + v % 15) << 8) | (0x80 + v % 15);
        tex_wood[i] = 0xFF000000u | ((0x6A + v % 15) << 16) | ((0x4A + v % 10) << 8) | (0x2A + v % 10);
        tex_leaves[i] = 0xC0000000u | ((0x30 + v % 20) << 16) | ((0x80 + v % 25) << 8) | (0x20 + v % 15);
        tex_sand[i] = 0xFF000000u | ((0xE0 + v % 15) << 16) | ((0xD0 + v % 15) << 8) | (0xA0 + v % 10);
        if (y == 0) tex_grass_side[i] = tex_grass_top[i];
    }
}

static uint8_t get_block(int x, int y, int z)
{
    if (x < 0 || x >= WORLD_CHUNKS_X * CHUNK_SIZE) return BLOCK_AIR;
    if (y < 0 || y >= CHUNK_HEIGHT) return BLOCK_AIR;
    if (z < 0 || z >= WORLD_CHUNKS_Z * CHUNK_SIZE) return BLOCK_AIR;
    return world[x][y][z];
}

static void set_block(int x, int y, int z, uint8_t b)
{
    if (x < 0 || x >= WORLD_CHUNKS_X * CHUNK_SIZE) return;
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    if (z < 0 || z >= WORLD_CHUNKS_Z * CHUNK_SIZE) return;
    world[x][y][z] = b;
}

static void gen_world(void)
{
    for (int x = 0; x < WORLD_CHUNKS_X * CHUNK_SIZE; x++) {
        for (int z = 0; z < WORLD_CHUNKS_Z * CHUNK_SIZE; z++) {
            int h = 32 + (x * 7 + z * 13) % 8;
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                if (y > h) set_block(x, y, z, BLOCK_AIR);
                else if (y == h) set_block(x, y, z, BLOCK_GRASS);
                else if (y > h - 3) set_block(x, y, z, BLOCK_DIRT);
                else set_block(x, y, z, BLOCK_STONE);
            }
            if ((x + z) % 17 == 0 && h + 1 < CHUNK_HEIGHT - 4) {
                for (int y = h + 1; y <= h + 3; y++)
                    set_block(x, y, z, BLOCK_WOOD);
                for (int dx = -2; dx <= 2; dx++)
                    for (int dy = 0; dy <= 1; dy++)
                        for (int dz = -2; dz <= 2; dz++)
                            if (dx * dx + dz * dz + dy * dy < 6)
                                set_block(x + dx, h + 4 + dy, z + dz, BLOCK_LEAVES);
            }
        }
    }
}

static bool is_face_visible(int x, int y, int z, int face)
{
    uint8_t n = 0;
    switch (face) {
        case 0: n = get_block(x, y + 1, z); break;
        case 1: n = get_block(x, y - 1, z); break;
        case 2: n = get_block(x, y, z + 1); break;
        case 3: n = get_block(x, y, z - 1); break;
        case 4: n = get_block(x + 1, y, z); break;
        case 5: n = get_block(x - 1, y, z); break;
    }
    return n == BLOCK_AIR || n == BLOCK_LEAVES;
}

static int get_face_texture(uint8_t block, int face)
{
    switch (block) {
        case BLOCK_GRASS: return (face == 0) ? 1 : (face == 1) ? 3 : 2;
        case BLOCK_DIRT:  return 3;
        case BLOCK_STONE: return 4;
        case BLOCK_WOOD:  return (face == 0 || face == 1) ? 5 : 6;
        case BLOCK_LEAVES: return 7;
        case BLOCK_SAND:  return 8;
        default: return 4;
    }
}

static void emit_face(int x, int y, int z, int face, int tex)
{
    float fx = (float)x, fy = (float)y, fz = (float)z;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    axgl_bind_texture(&gl, tex);
    axgl_begin(&gl, AXGL_QUADS);
    axgl_color4f(&gl, 1.0f, 1.0f, 1.0f, 1.0f);
    switch (face) {
        case 0:
            axgl_tex_coord2f(&gl, u0, v0); axgl_vertex3f(&gl, fx, fy + 1, fz);
            axgl_tex_coord2f(&gl, u1, v0); axgl_vertex3f(&gl, fx + 1, fy + 1, fz);
            axgl_tex_coord2f(&gl, u1, v1); axgl_vertex3f(&gl, fx + 1, fy + 1, fz + 1);
            axgl_tex_coord2f(&gl, u0, v1); axgl_vertex3f(&gl, fx, fy + 1, fz + 1);
            break;
        case 1:
            axgl_tex_coord2f(&gl, u0, v0); axgl_vertex3f(&gl, fx, fy, fz);
            axgl_tex_coord2f(&gl, u1, v0); axgl_vertex3f(&gl, fx + 1, fy, fz);
            axgl_tex_coord2f(&gl, u1, v1); axgl_vertex3f(&gl, fx + 1, fy, fz + 1);
            axgl_tex_coord2f(&gl, u0, v1); axgl_vertex3f(&gl, fx, fy, fz + 1);
            break;
        case 2:
            axgl_tex_coord2f(&gl, u0, v0); axgl_vertex3f(&gl, fx, fy, fz + 1);
            axgl_tex_coord2f(&gl, u1, v0); axgl_vertex3f(&gl, fx + 1, fy, fz + 1);
            axgl_tex_coord2f(&gl, u1, v1); axgl_vertex3f(&gl, fx + 1, fy + 1, fz + 1);
            axgl_tex_coord2f(&gl, u0, v1); axgl_vertex3f(&gl, fx, fy + 1, fz + 1);
            break;
        case 3:
            axgl_tex_coord2f(&gl, u0, v0); axgl_vertex3f(&gl, fx, fy, fz);
            axgl_tex_coord2f(&gl, u1, v0); axgl_vertex3f(&gl, fx + 1, fy, fz);
            axgl_tex_coord2f(&gl, u1, v1); axgl_vertex3f(&gl, fx + 1, fy + 1, fz);
            axgl_tex_coord2f(&gl, u0, v1); axgl_vertex3f(&gl, fx, fy + 1, fz);
            break;
        case 4:
            axgl_tex_coord2f(&gl, u0, v0); axgl_vertex3f(&gl, fx + 1, fy, fz);
            axgl_tex_coord2f(&gl, u1, v0); axgl_vertex3f(&gl, fx + 1, fy, fz + 1);
            axgl_tex_coord2f(&gl, u1, v1); axgl_vertex3f(&gl, fx + 1, fy + 1, fz + 1);
            axgl_tex_coord2f(&gl, u0, v1); axgl_vertex3f(&gl, fx + 1, fy + 1, fz);
            break;
        case 5:
            axgl_tex_coord2f(&gl, u0, v0); axgl_vertex3f(&gl, fx, fy, fz);
            axgl_tex_coord2f(&gl, u1, v0); axgl_vertex3f(&gl, fx, fy, fz + 1);
            axgl_tex_coord2f(&gl, u1, v1); axgl_vertex3f(&gl, fx, fy + 1, fz + 1);
            axgl_tex_coord2f(&gl, u0, v1); axgl_vertex3f(&gl, fx, fy + 1, fz);
            break;
    }
    axgl_end(&gl);
}

static void build_chunk_mesh(int cx, int cz)
{
    int x0 = cx * CHUNK_SIZE;
    int z0 = cz * CHUNK_SIZE;
    for (int x = x0; x < x0 + CHUNK_SIZE; x++) {
        for (int z = z0; z < z0 + CHUNK_SIZE; z++) {
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                uint8_t b = get_block(x, y, z);
                if (b == BLOCK_AIR) continue;
                for (int f = 0; f < 6; f++) {
                    if (is_face_visible(x, y, z, f)) {
                        int tex = get_face_texture(b, f);
                        emit_face(x, y, z, f, tex);
                    }
                }
            }
        }
    }
}

static void render_world(void)
{
    axgl_clear(&gl, true, true);
    axgl_matrix_mode(&gl, AXGL_PROJECTION);
    axgl_load_identity(&gl);
    axgl_perspective(&gl, 70.0f, (float)SCR_W / (float)SCR_H, 0.1f, 100.0f);
    axgl_matrix_mode(&gl, AXGL_MODELVIEW);
    axgl_load_identity(&gl);
    axgl_rotate(&gl, cam_pitch, 1, 0, 0);
    axgl_rotate(&gl, cam_yaw, 0, 1, 0);
    axgl_translate(&gl, -cam_x, -cam_y, -cam_z);
    for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
        for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
            build_chunk_mesh(cx, cz);
}

static void process_input(void)
{
    ax_event ev;
    while (ax_poll(canvas, &ev) > 0) {
        if (ev.type == AX_EV_KEY) {
            float mx = 0.0f, mz = 0.0f;
            float speed = 0.5f;
            float rad_yaw = cam_yaw * 3.14159265f / 180.0f;
            if (ev.key == 'w') { mx = -sinf(rad_yaw) * speed; mz = cosf(rad_yaw) * speed; }
            if (ev.key == 's') { mx = sinf(rad_yaw) * speed; mz = -cosf(rad_yaw) * speed; }
            if (ev.key == 'a') { mx = -cosf(rad_yaw) * speed; mz = -sinf(rad_yaw) * speed; }
            if (ev.key == 'd') { mx = cosf(rad_yaw) * speed; mz = sinf(rad_yaw) * speed; }
            if (ev.key == ' ') cam_y += speed;
            if (ev.key == 'c') cam_y -= speed;
            if (ev.key == 'q') cam_yaw -= 5.0f;
            if (ev.key == 'e') cam_yaw += 5.0f;
            if (ev.key == 'r') cam_pitch -= 5.0f;
            if (ev.key == 'f') cam_pitch += 5.0f;
            cam_x += mx; cam_z += mz;
        }
        if (ev.type == AX_EV_MOUSE) {
            static int prev_mx = 0, prev_my = 0;
            mouse_dx = ev.mx - prev_mx;
            mouse_dy = ev.my - prev_my;
            prev_mx = ev.mx; prev_my = ev.my;
            cam_yaw += (float)mouse_dx * 0.5f;
            cam_pitch += (float)mouse_dy * 0.5f;
            if (cam_pitch > 89.0f) cam_pitch = 89.0f;
            if (cam_pitch < -89.0f) cam_pitch = -89.0f;
        }
        if (ev.type == AX_EV_CLOSE) {
            for (;;) yield();
        }
    }
}

static void blit_to_canvas(void)
{
    for (int i = 0; i < SCR_W * SCR_H; i++)
        canvas[i] = color_buf[i];
    ax_commit(canvas);
}

int main(void)
{
    canvas = ax_surface("craft", SCR_W, SCR_H);
    if (!canvas) return 1;
    axgl_init(&gl, color_buf, depth_buf, SCR_W, SCR_H);
    gen_textures();
    int t_grass_top = axgl_gen_texture(&gl);
    int t_grass_side = axgl_gen_texture(&gl);
    int t_dirt = axgl_gen_texture(&gl);
    int t_stone = axgl_gen_texture(&gl);
    int t_wood_top = axgl_gen_texture(&gl);
    int t_wood_side = axgl_gen_texture(&gl);
    int t_leaves = axgl_gen_texture(&gl);
    int t_sand = axgl_gen_texture(&gl);
    axgl_tex_image_2d(&gl, t_grass_top, TEX_SIZE, TEX_SIZE, tex_grass_top, false);
    axgl_tex_image_2d(&gl, t_grass_side, TEX_SIZE, TEX_SIZE, tex_grass_side, false);
    axgl_tex_image_2d(&gl, t_dirt, TEX_SIZE, TEX_SIZE, tex_dirt, false);
    axgl_tex_image_2d(&gl, t_stone, TEX_SIZE, TEX_SIZE, tex_stone, false);
    axgl_tex_image_2d(&gl, t_wood_top, TEX_SIZE, TEX_SIZE, tex_wood, false);
    axgl_tex_image_2d(&gl, t_wood_side, TEX_SIZE, TEX_SIZE, tex_wood, false);
    axgl_tex_image_2d(&gl, t_leaves, TEX_SIZE, TEX_SIZE, tex_leaves, true);
    axgl_tex_image_2d(&gl, t_sand, TEX_SIZE, TEX_SIZE, tex_sand, false);
    axgl_enable(&gl, AXGL_DEPTH_TEST);
    axgl_enable(&gl, AXGL_TEXTURE_2D);
    axgl_enable(&gl, AXGL_CULL_FACE);
    axgl_enable(&gl, AXGL_ALPHA_TEST);
    axgl_clear_color(&gl, 0.5f, 0.7f, 1.0f, 1.0f);
    axgl_clear_depth(&gl, 1.0f);
    gen_world();
    for (;;) {
        process_input();
        render_world();
        blit_to_canvas();
        yield();
    }
}
