/*
 * A custom layer written against the C ABI alone (docs/guides/extensions.mdx), shared by the Android
 * and iOS demos. Two modes:
 *  - overlay: the heatmap's colour legend, in screen space, below a top bar, tinted by the layer's opacity;
 *  - scene: a heatmap draped on the terrain. The app hands over a grid of positions (already on
 *    the ground) and values; this draws it depth tested, so a ridge in front hides it, and a tap
 *    on it finds the cell under the finger with hit_test and dims the heatmap.
 */
#include "heatmap_layer.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, "massif-demo", __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "massif-demo", __VA_ARGS__)
#else
#include <stdio.h>
#define LOG_INFO(...) (fprintf(stderr, "massif-demo: " __VA_ARGS__), fputc('\n', stderr))
#define LOG_ERROR(...) LOG_INFO(__VA_ARGS__)
#endif

/* Draped: this much of the clip depth range forward of the ground, so the heatmap wins over the ground
   mesh's chords between its nodes. A constant in NDC: it also lets it show through a ridge by a hair at range. */
#define DRAPE_BIAS 0.004f

/* EPSG3857 metres to the SDK's world units: WORLD_SIZE (2^20) over the earth's circumference. */
#define METRES_TO_WORLD (1048576.0 / 40075016.68558)

struct HeatmapLayer {
    int scene;
    GLuint program;
    GLint u_mvp, u_opacity, u_flat, u_offset, u_shift, u_bias;
    pthread_mutex_t lock;
    int n;
    double* pos;    /* n * n positions in world units, row by row */
    float* value;   /* n * n values in 0..1 */
    float* vertices;/* n * n * (x, y, z, value), relative to the camera */
    uint16_t* indices;
    int index_count;
    int dim;
    int drape;
    int init_failed;
    mm_ctx ctx;
    mm_handle handle;
    int (*request_redraw)(mm_ctx, mm_handle);
    void (*on_destroy)(HeatmapLayer*);
};

typedef struct HeatmapLayer Demo;

static void redraw(Demo* demo) {
    if (demo->request_redraw) {
        demo->request_redraw(demo->ctx, demo->handle);
    }
}

static const char* VERTEX =
    "#version 300 es\n"
    MM_CUSTOM_LAYER_GLSL_DEPTH
    "uniform mat4 u_mvp;\n"
    "uniform float u_bias;\n"
    "in vec3 a_pos;\n"
    "in float a_value;\n"
    "out float v_value;\n"
    "void main() {\n"
    "    v_value = a_value;\n"
    "    gl_Position = mm_applyDepth(u_mvp * vec4(a_pos, 1.0));\n"
    "    gl_Position.z -= u_bias * gl_Position.w;\n"
    "}\n";
static const char* FRAGMENT =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform float u_opacity;\n"
    "uniform float u_flat;\n"
    "in float v_value;\n"
    "out vec4 o;\n"
    "vec3 ramp(float t) {\n"
    "    vec3 c0 = vec3(0.10, 0.20, 0.90), c1 = vec3(0.00, 0.90, 0.90);\n"
    "    vec3 c2 = vec3(1.00, 0.90, 0.10), c3 = vec3(0.90, 0.10, 0.10);\n"
    "    return t < 0.33 ? mix(c0, c1, t / 0.33) : t < 0.66 ? mix(c1, c2, (t - 0.33) / 0.33) : mix(c2, c3, (t - 0.66) / 0.34);\n"
    "}\n"
    "void main() {\n"
    "    float v = clamp(v_value, 0.0, 1.0);\n"
    "    float a = mix(smoothstep(0.04, 0.25, v) * 0.8, 0.95, u_flat) * u_opacity;\n"
    "    o = vec4(ramp(v) * a, a);\n"
    "}\n";

static GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        LOG_ERROR("custom layer shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int init(Demo* demo) {
    if (demo->init_failed) {
        return 0;
    }
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX), fs = compile(GL_FRAGMENT_SHADER, FRAGMENT);
    if (!vs || !fs) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        demo->init_failed = 1;
        return 0;
    }
    demo->program = glCreateProgram();
    glAttachShader(demo->program, vs);
    glAttachShader(demo->program, fs);
    glBindAttribLocation(demo->program, 0, "a_pos");
    glBindAttribLocation(demo->program, 1, "a_value");
    glLinkProgram(demo->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(demo->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOG_ERROR("custom layer program did not link");
        glDeleteProgram(demo->program);
        demo->program = 0;
        demo->init_failed = 1;
        return 0;
    }
    demo->u_mvp = glGetUniformLocation(demo->program, "u_mvp");
    demo->u_opacity = glGetUniformLocation(demo->program, "u_opacity");
    demo->u_flat = glGetUniformLocation(demo->program, "u_flat");
    demo->u_offset = glGetUniformLocation(demo->program, "u_depthLayerOffset");
    demo->u_shift = glGetUniformLocation(demo->program, "u_depthShift");
    demo->u_bias = glGetUniformLocation(demo->program, "u_bias");
    return 1;
}

static void draw(Demo* demo, const float* vertices, const uint16_t* indices, int count, const float* mvp,
                 float opacity, float flat, float depth_offset, float depth_shift, float bias) {
    glUniformMatrix4fv(demo->u_mvp, 1, GL_FALSE, mvp);
    glUniform1f(demo->u_opacity, opacity);
    glUniform1f(demo->u_flat, flat);
    glUniform1f(demo->u_offset, depth_offset);
    glUniform1f(demo->u_shift, depth_shift);
    glUniform1f(demo->u_bias, bias);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices + 3);
    if (indices) {
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, indices);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, count);
    }
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

static void render(void* user, const mm_render_params* p) {
    Demo* demo = user;
    if (!demo->program && !init(demo)) {
        return;
    }
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glBindFramebuffer(GL_FRAMEBUFFER, p->framebuffer);
    glUseProgram(demo->program);

    if (!demo->scene) {
        static const float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        static const float legend[] = {
            -0.95f, 0.66f, 0, 0, -0.35f, 0.66f, 0, 1, -0.95f, 0.73f, 0, 0,
            -0.35f, 0.66f, 0, 1, -0.35f, 0.73f, 0, 1, -0.95f, 0.73f, 0, 0,
        };
        draw(demo, legend, NULL, 6, identity, p->opacity, 1.0f, 0, 0, 0);
    } else {
        pthread_mutex_lock(&demo->lock);
        if (demo->n) {
            /* Relative to the camera, subtracted in double: the whole point of mvp_local. */
            for (int i = 0; i < demo->n * demo->n; i++) {
                demo->vertices[4 * i] = (float)(demo->pos[3 * i] - p->origin[0]);
                demo->vertices[4 * i + 1] = (float)(demo->pos[3 * i + 1] - p->origin[1]);
                demo->vertices[4 * i + 2] = (float)(demo->pos[3 * i + 2] - p->origin[2]);
                demo->vertices[4 * i + 3] = demo->value[i];
            }
            GLboolean cull = glIsEnabled(GL_CULL_FACE);
            glDisable(GL_CULL_FACE);
            draw(demo, demo->vertices, demo->indices, demo->index_count, p->mvp_local,
                 p->opacity * (demo->dim ? 0.4f : 1.0f), 0.0f, p->depth_layer_offset, p->depth_shift,
                 demo->drape ? DRAPE_BIAS : 0.0f);
            if (cull) {
                glEnable(GL_CULL_FACE);
            }
        }
        pthread_mutex_unlock(&demo->lock);
    }
    glUseProgram(program);
}

static void on_removed(void* user) {
    Demo* demo = user;
    if (demo->program) {
        glDeleteProgram(demo->program);
        demo->program = 0;
    }
}

static void on_context_lost(void* user) {
    ((Demo*) user)->program = 0;
    ((Demo*) user)->init_failed = 0;
}

/* Marches the ray over the grid's bounds until it dips below the ground: the first cell it enters. */
static int hit_test(void* user, const double o[3], const double d[3], double* out_t, uint64_t* out_feature) {
    Demo* demo = user;
    if (!demo->scene) {
        return 0;
    }
    pthread_mutex_lock(&demo->lock);
    int n = demo->n, found = 0;
    if (n) {
        double x0 = demo->pos[0], y0 = demo->pos[1];
        double x1 = demo->pos[3 * (n - 1)], y1 = demo->pos[3 * (n - 1) * n + 1];
        if (x0 == x1 || y0 == y1) {
            pthread_mutex_unlock(&demo->lock);
            return 0;
        }
        double t0 = 0, t1 = 1e30;
        const double lo[2] = { fmin(x0, x1), fmin(y0, y1) }, hi[2] = { fmax(x0, x1), fmax(y0, y1) };
        for (int axis = 0; axis < 2 && t0 < t1; axis++) {
            if (fabs(d[axis]) < 1e-300) {
                if (o[axis] < lo[axis] || o[axis] > hi[axis]) {
                    t1 = -1;
                }
                continue;
            }
            double a = (lo[axis] - o[axis]) / d[axis], b = (hi[axis] - o[axis]) / d[axis];
            t0 = fmax(t0, fmin(a, b));
            t1 = fmin(t1, fmax(a, b));
        }
        for (int step = 0; step <= 512 && t0 < t1; step++) {
            double t = t0 + (t1 - t0) * step / 512.0;
            int i = (int) lround((o[0] + t * d[0] - x0) / (x1 - x0) * (n - 1));
            int j = (int) lround((o[1] + t * d[1] - y0) / (y1 - y0) * (n - 1));
            if (i >= 0 && i < n && j >= 0 && j < n && o[2] + t * d[2] <= demo->pos[3 * (j * n + i) + 2]) {
                *out_t = t;
                *out_feature = (uint64_t) (j * n + i) + 1;
                found = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&demo->lock);
    return found;
}

static int on_click(void* user, uint64_t feature, int click_type, const double hit_pos[3]) {
    Demo* demo = user;
    pthread_mutex_lock(&demo->lock);
    int cell = (int) feature - 1;
    float value = demo->n && cell < demo->n * demo->n ? demo->value[cell] : 0.0f;
    demo->dim = !demo->dim;
    int n = demo->n;
    pthread_mutex_unlock(&demo->lock);
    redraw(demo);
    LOG_INFO("custom layer heatmap cell (%d, %d) = %.2f clicked (type %d), dim %d",
                        n ? cell % n : 0, n ? cell / n : 0, value, click_type, demo->dim);
    return 1;
}

static void destroy(void* user) {
    Demo* demo = user;
    if (demo->on_destroy) {
        demo->on_destroy(demo);
    }
    free(demo->pos);
    free(demo->value);
    free(demo->vertices);
    free(demo->indices);
    pthread_mutex_destroy(&demo->lock);
    free(demo);
}

HeatmapLayer* heatmap_layer_create(const HeatmapApi* api, const char* id, int scene, void (*on_destroy)(HeatmapLayer*)) {
    Demo* demo = calloc(1, sizeof(Demo));
    demo->scene = scene;
    demo->drape = 1;
    demo->on_destroy = on_destroy;
    pthread_mutex_init(&demo->lock, NULL);
    mm_custom_layer layer;
    memset(&layer, 0, sizeof(layer));
    layer.size = sizeof(layer);
    layer.mode = scene ? MM_CUSTOM_LAYER_SCENE : MM_CUSTOM_LAYER_OVERLAY;
    layer.render = render;
    layer.on_removed = on_removed;
    layer.on_context_lost = on_context_lost;
    layer.hit_test = hit_test;
    layer.on_click = on_click;
    layer.destroy = destroy;
    layer.user_data = demo;

    mm_handle handle = MM_NULL_HANDLE;
    mm_ctx ctx = api->context_default();
    if (api->create(ctx, id, &layer, &handle) != MM_OK) {
        demo->on_destroy = NULL;
        destroy(demo);
        return NULL;
    }
    demo->ctx = ctx;
    demo->handle = handle;
    demo->request_redraw = api->request_redraw;
    return demo;
}

mm_handle heatmap_layer_handle(const HeatmapLayer* layer) {
    return layer->handle;
}

void heatmap_layer_set(HeatmapLayer* demo, const double* xyz, const float* values, int n) {
    if (n < 2 || n > 255) {
        return;
    }
    int count = n * n;
    double* pos = malloc(sizeof(double) * 3 * count);
    float* value = malloc(sizeof(float) * count);
    float* vertices = malloc(sizeof(float) * 4 * count);
    uint16_t* indices = malloc(sizeof(uint16_t) * 6 * (n - 1) * (n - 1));
    if (!pos || !value || !vertices || !indices) {
        free(pos); free(value); free(vertices); free(indices);
        return;
    }
    memcpy(value, values, sizeof(float) * count);
    for (int i = 0; i < 3 * count; i++) {
        pos[i] = xyz[i] * METRES_TO_WORLD;
    }
    int k = 0;
    for (int j = 0; j < n - 1; j++) {
        for (int i = 0; i < n - 1; i++) {
            uint16_t a = (uint16_t) (j * n + i), b = a + 1, c = (uint16_t) (a + n), e = c + 1;
            indices[k++] = a; indices[k++] = b; indices[k++] = c;
            indices[k++] = b; indices[k++] = e; indices[k++] = c;
        }
    }
    pthread_mutex_lock(&demo->lock);
    free(demo->pos);
    free(demo->value);
    free(demo->vertices);
    free(demo->indices);
    demo->pos = pos;
    demo->value = value;
    demo->vertices = vertices;
    demo->indices = indices;
    demo->index_count = k;
    demo->n = n;
    pthread_mutex_unlock(&demo->lock);
    redraw(demo);
}

void heatmap_layer_set_drape(HeatmapLayer* demo, int drape) {
    pthread_mutex_lock(&demo->lock);
    demo->drape = drape;
    pthread_mutex_unlock(&demo->lock);
    redraw(demo);
}
