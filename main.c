#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <stdbool.h>
#include <wayland-client.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "wayland-wlr-output-management-client-protocol.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_GL3_IMPLEMENTATION
#define NK_KEYSTATE_BASED_INPUT
#include "nuklear.h"
#include "nuklear_glfw_gl3.h"

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
      _a > _b ? _a : _b; })
#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
      _a < _b ? _a : _b; })

struct wlay_state {
    /* Wayland state */
    struct {
        struct wl_display *display;
        struct wl_registry *registry;
        struct wl_shm *shm;
        struct wl_list heads;
        struct zwlr_output_manager_v1 *output_manager;
    } wl;

    /* GL/nuklear state */
    struct {
        // TODO: Does this need to be a struct?
        GLFWwindow *window;
    } gl;
    struct nk_context *nk;

    struct {
        struct nk_vec2 screen_size;
        bool dragging;
    } gui;
    bool should_apply;

    uint32_t serial;
};

struct wlay_mode;

struct wlay_head {
    char *name;
    char *description;

    struct wlay_mode *current_mode;

    int32_t x;
    int32_t y;
    int32_t physical_width;
    int32_t physical_height;
    bool enabled;
    int32_t transform;
    wl_fixed_t scale;

    int32_t w;
    int32_t h;

    bool focused;

    struct wlay_state *wlay;
    struct zwlr_output_head_v1 *wlr;
    struct wl_list link;
    struct wl_list modes;
};


struct wlay_mode {
    int32_t width;
    int32_t height;
    int32_t refresh_rate;
    bool preferred;

    struct wlay_head *head;
    struct zwlr_output_mode_v1 *wlr;
    struct wl_list link;
};


void log_info(const char *format, ...)
{
    va_list vas;
    va_start(vas, format);
    vfprintf(stderr, format, vas);
    va_end(vas);
    fprintf(stderr, "\n");
}


void fail(const char *format, ...)
{
    va_list vas;
    va_start(vas, format);
    vfprintf(stderr, format, vas);
    va_end(vas);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}


void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fail("malloc failed");
    }
    memset(ptr, 0, size);
    return ptr;
}


static void error_callback(int e, const char *d)
{
    printf("Error %d: %s\n", e, d);
}


static void handle_mode_size(void *data,
                             struct zwlr_output_mode_v1 *wlr_mode,
		                     int32_t width, int32_t height)
{
    struct wlay_mode *mode = data;
    mode->width = width;
    mode->height = height;
}

static void handle_mode_refresh(void *data,
	                        struct zwlr_output_mode_v1 *wlr_mode,
                                int32_t refresh)
{
    struct wlay_mode *mode = data;
    mode->refresh_rate = refresh;
}

static void handle_mode_preferred(void *data,
		                  struct zwlr_output_mode_v1 *wlr_mode)
{
    struct wlay_mode *mode = data;
    mode->preferred = true;
}

static void handle_mode_finished(void *data,
		                         struct zwlr_output_mode_v1 *wlr_mode)
{
    struct wlay_mode *mode = data;
    wl_list_remove(&mode->link);
    zwlr_output_mode_v1_destroy(mode->wlr);
    free(mode);
}

static const struct zwlr_output_mode_v1_listener wlr_output_mode_listener = {
	.size = handle_mode_size,
	.refresh = handle_mode_refresh,
	.preferred = handle_mode_preferred,
	.finished = handle_mode_finished,
};

static void handle_head_name(void *data,
                             struct zwlr_output_head_v1 *wlr_head,
                             const char *name)
{
    struct wlay_head *head = data;
    head->name = strdup(name);
}

static void handle_head_description(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *description)
{
    struct wlay_head *head = data;
    head->description = strdup(description);
}

static void handle_head_physical_size(void *data,
		                      struct zwlr_output_head_v1 *wlr_head,
                                      int32_t width, int32_t height)
{
    struct wlay_head *head = data;
    head->physical_width = width;
    head->physical_height = height;
}

static void handle_head_mode(void *data,
		             struct zwlr_output_head_v1 *wlr_head,
		             struct zwlr_output_mode_v1 *wlr_mode)
{
    struct wlay_head *head = data;

    struct wlay_mode *mode = xmalloc(sizeof(*mode));

    mode->head = head;
    mode->wlr = wlr_mode;
    wl_list_insert(&head->modes, &mode->link);
    zwlr_output_mode_v1_add_listener(wlr_mode, &wlr_output_mode_listener, mode);
}

static void handle_head_enabled(void *data,
		                struct zwlr_output_head_v1 *wlr_head,
                                int32_t enabled)
{
    struct wlay_head *head = data;
    head->enabled = true;
}

static void handle_head_current_mode(void *data,
		                     struct zwlr_output_head_v1 *wlr_head,
		                     struct zwlr_output_mode_v1 *wlr_mode)
{
    struct wlay_head *head = data;
    struct wlay_mode *mode;
    wl_list_for_each(mode, &head->modes, link) {
        if (mode->wlr == wlr_mode) {
            head->current_mode = mode;
            return;
        }
    }
    // WTF?
    head->current_mode = NULL;
    log_info("Unknown mode");
}

static void handle_head_position(void *data,
		                 struct zwlr_output_head_v1 *wlr_head,
                                 int32_t x, int32_t y)
{
    struct wlay_head *head = data;
    head->x = x;
    head->y = y;
}

static void handle_head_transform(void *data,
		                  struct zwlr_output_head_v1 *wlr_head,
                                  int32_t transform)
{
    struct wlay_head *head = data;
    head->transform = transform;
}

static void handle_head_scale(void *data,
		              struct zwlr_output_head_v1 *wlr_head, wl_fixed_t scale)
{
    struct wlay_head *head = data;
    head->scale = scale;
}

static void handle_head_finished(void *data,
		                 struct zwlr_output_head_v1 *wlr_head)
{
    struct wlay_head *head = data;
    wl_list_remove(&head->link);
    zwlr_output_head_v1_destroy(head->wlr);
    free(head->name);
    free(head->description);
    free(head);
}

static const struct zwlr_output_head_v1_listener wlr_head_listener = {
	.name = handle_head_name,
	.description = handle_head_description,
	.physical_size = handle_head_physical_size,
	.mode = handle_head_mode,
	.enabled = handle_head_enabled,
	.current_mode = handle_head_current_mode,
	.position = handle_head_position,
	.transform = handle_head_transform,
	.scale = handle_head_scale,
	.finished = handle_head_finished,
};


static void handle_wlr_output_manager_head(void *data,
                                           struct zwlr_output_manager_v1 *manager,
                                           struct zwlr_output_head_v1 *wlr_head)
{
    struct wlay_state *wlay = data;
    struct wlay_head *head = xmalloc(sizeof(struct wlay_head));
    head->wlay = wlay;
    head->wlr = wlr_head;
    wl_list_init(&head->modes);
    wl_list_insert(&wlay->wl.heads, &head->link);
    zwlr_output_head_v1_add_listener(wlr_head, &wlr_head_listener, head);
}


static void handle_wlr_output_manager_done(void *data,
                                           struct zwlr_output_manager_v1 *manager,
                                           uint32_t serial)
{
    struct wlay_state *wlay = data;
    wlay->serial = serial;
}


static void handle_wlr_output_manager_finished(void *data,
                                               struct zwlr_output_manager_v1 *manager)
{
}


static const struct zwlr_output_manager_v1_listener wlr_output_manager_listener = {
    .head = handle_wlr_output_manager_head,
    .done = handle_wlr_output_manager_done,
    .finished = handle_wlr_output_manager_finished,
};


static void handle_wl_event(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
    struct wlay_state *wlay = data;
    if (!strcmp(interface, zwlr_output_manager_v1_interface.name)) {
        wlay->wl.output_manager = wl_registry_bind(
            registry, name, &zwlr_output_manager_v1_interface, 1
        );
        zwlr_output_manager_v1_add_listener(
            wlay->wl.output_manager, &wlr_output_manager_listener, wlay
        );
    }
}


static void handle_wl_event_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    // TODO: At this point we should handle output removal
    log_info("Removing!");
}


static const struct wl_registry_listener registry_listener = {
    .global = handle_wl_event,
    .global_remove = handle_wl_event_remove,
};

static void wlay_gui_init(struct wlay_state *wlay)
{
    /* Platform */
    int width = 0, height = 0;

    /* GLFW */
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        fprintf(stdout, "[GFLW] failed to init!\n");
        exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GL_FALSE);
    wlay->gl.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Demo", NULL, NULL);
    glfwMakeContextCurrent(wlay->gl.window);
    glfwGetWindowSize(wlay->gl.window, &width, &height);

    /* OpenGL */
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    glewExperimental = 1;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Failed to setup GLEW\n");
        exit(1);
    }

    wlay->nk = nk_glfw3_init(wlay->gl.window, NK_GLFW3_INSTALL_CALLBACKS);
    /* Load Fonts: if none of these are loaded a default font will be used  */
    /* Load Cursor: if you uncomment cursor loading please hide the cursor */
    struct nk_font_atlas *atlas;
    nk_glfw3_font_stash_begin(&atlas);
    nk_glfw3_font_stash_end();
}


static void wlay_gui_editor(struct wlay_state *wlay)
{

}


const float editor_scale = 1./10;


static void wlay_gui_editor_head(struct wlay_head *head)
{
    struct wlay_state *wlay = head->wlay;
    struct nk_context *ctx = wlay->nk;
    struct nk_rect layout_bounds = nk_layout_space_bounds(ctx);
    struct nk_rect bounds = nk_rect(
        head->x*editor_scale +
            layout_bounds.w/2 - wlay->gui.screen_size.x*editor_scale/2,
        head->y*editor_scale +
            layout_bounds.h/2 - wlay->gui.screen_size.y*editor_scale/2,
        head->w*editor_scale,
        head->h*editor_scale
    );
    nk_layout_space_push(ctx, bounds);
    if (nk_group_begin(ctx, head->name, NK_WINDOW_MOVABLE | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
        nk_layout_row_static(ctx, bounds.h, bounds.w, 1);
        nk_label_colored(
            ctx, head->name, NK_TEXT_CENTERED,
            head->focused ? nk_rgb(200, 100, 100) : ctx->style.text.color
        );
    
        struct nk_input *in = &ctx->input;
        bool left_mouse_down = in->mouse.buttons[NK_BUTTON_LEFT].down;
        bool left_mouse_clicked = in->mouse.buttons[NK_BUTTON_LEFT].clicked;
        bool click_in_group = nk_input_has_mouse_click_down_in_rect(
            in, NK_BUTTON_LEFT, nk_layout_space_bounds(ctx), nk_true
        );
        if (left_mouse_clicked && click_in_group) {
            struct wlay_head *head_other;
            wl_list_for_each(head_other, &wlay->wl.heads, link) {
                head_other->focused = head_other == head;
            }
        }
        if (left_mouse_down && click_in_group && head->focused) {
            head->x = head->x + in->mouse.delta.x/editor_scale;
            head->y = head->y + in->mouse.delta.y/editor_scale;
            wlay->gui.dragging = true;
            in->mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x += in->mouse.delta.x;
            in->mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y += in->mouse.delta.y;
        }
        nk_group_end(ctx);
    }
}


static const char *wl_output_transform_names[] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = "normal",
	[WL_OUTPUT_TRANSFORM_90] = "90",
	[WL_OUTPUT_TRANSFORM_180] = "180",
	[WL_OUTPUT_TRANSFORM_270] = "270",
	[WL_OUTPUT_TRANSFORM_FLIPPED] = "flip",
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = "flip-90",
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = "flip-180",
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = "flip-270",
};


static void wlay_gui_details(struct wlay_head *head)
{
    struct nk_context *ctx = head->wlay->nk;
    nk_layout_row_dynamic(ctx, 30, 1);
    nk_labelf(ctx, NK_TEXT_CENTERED, "Output %s \"%s\"", head->name, head->description);

    nk_layout_row_dynamic(ctx, 30, 3);
    // Transform selector
    head->transform = nk_combo(
            ctx, wl_output_transform_names, ARRAY_SIZE(wl_output_transform_names),
            head->transform, 25, nk_vec2(200, 200)
    );

    // Mode selector
    int mode_count = wl_list_length(&head->modes);
    char *mode_strs[mode_count];
    struct wlay_mode *modes[mode_count];
    int i = 0;
    int selected_mode = 0;
    struct wlay_mode *mode;
    wl_list_for_each(mode, &head->modes, link) {
        asprintf(&mode_strs[i], "%dx%d@%dHz",
                 mode->width, mode->height, mode->refresh_rate / 1000);
        modes[i] = mode;
        if (mode == head->current_mode) {
            selected_mode = i;
        }
        i++;
    }
    selected_mode = nk_combo(ctx, (const char **)mode_strs, mode_count, selected_mode, 25, nk_vec2(200, 200));
    head->current_mode = modes[selected_mode];
}


static void wlay_calculate_screen_space(struct wlay_state *wlay)
{
    // We do this before rendering the GUI to allow stuff like edge
    // snapping/editor autoscaling

    // First, we calculate individual head rectangles
    struct wlay_head *head;
    wl_list_for_each(head, &wlay->wl.heads, link) {
        int32_t w, h;
        switch(head->transform) {
        case WL_OUTPUT_TRANSFORM_NORMAL:
        case WL_OUTPUT_TRANSFORM_180:
        case WL_OUTPUT_TRANSFORM_FLIPPED:
        case WL_OUTPUT_TRANSFORM_FLIPPED_180:
            w = head->current_mode->width;
            h = head->current_mode->height;
            break;
        case WL_OUTPUT_TRANSFORM_90:
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
            w = head->current_mode->height;
            h = head->current_mode->width;
            break;
        case WL_OUTPUT_TRANSFORM_270:
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            w = head->current_mode->height;
            h = head->current_mode->width;
            break;
        default:
            w = head->current_mode->width;
            h = head->current_mode->height;
            log_info("Transform %d not implemented", head->transform);
            break;
        }
        head->h = h;
        head->w = w;
    }

    // Now we find the screen space bounds
    // TODO: This will be fucked if no head is enabled...
    if (!wlay->nk->input.mouse.buttons[NK_BUTTON_LEFT].down) {
        int32_t min_x = INT32_MAX;
        int32_t max_x = INT32_MIN;
        int32_t min_y = INT32_MAX;
        int32_t max_y = INT32_MIN;

        wl_list_for_each(head, &wlay->wl.heads, link) {
            min_x = min(min_x, head->x);
            max_x = max(max_x, head->x + head->w);
            min_y = min(min_y, head->y);
            max_y = max(max_y, head->y + head->h);
        }
        // Now we shift everything to be based on 0,0
        wl_list_for_each(head, &wlay->wl.heads, link) {
            head->x -= min_x;
            head->y -= min_y;
        }
        wlay->gui.screen_size.x = max_x - min_x;
        wlay->gui.screen_size.y = max_y - min_y;
    }
}


static void wlay_snap(struct wlay_state *wlay)
{
    struct wlay_head *focused;
    wl_list_for_each(focused, &wlay->wl.heads, link) {
        if (focused->focused) {
            break;
        }
    }
    if (!focused->focused) {
        return;
    }

    // Compute snap points
    struct wlay_head *other;
    int32_t best_delta = INT32_MAX;
    int32_t best_x;
    int32_t best_y;
    wl_list_for_each(other, &wlay->wl.heads, link) {
        if (other == focused) {
            continue;
        }
        int32_t snaps[4][2] = {
            // Left border to right border
            {other->x + other->w, focused->y},
            // Right border to left border
            {other->x - focused->w, focused->y},
            // Top border to bottom border
            {focused->x, other->y + other->h},
            // Bottom border to top border
            {focused->x, other->y - focused->h},
        };
        for (unsigned int i = 0; i < ARRAY_SIZE(snaps); i++) {
            int32_t want_x = snaps[i][0];
            int32_t want_y = snaps[i][1];
            float delta_x = abs(focused->x - want_x);
            float delta_y = abs(focused->y - want_y);
            float delta = max(delta_x, delta_y);
            if (delta < best_delta) {
                best_x = want_x;
                best_y = want_y;
                best_delta = delta;
            }
        }
    }

    if (best_delta < 200) {
        focused->x = best_x;
        focused->y = best_y;
    }
}


static void wlay_gui(struct wlay_state *wlay)
{
    int window_width, window_height;
    glfwGetWindowSize(wlay->gl.window, &window_width, &window_height);
    struct nk_context *ctx = wlay->nk;

    wlay_calculate_screen_space(wlay);

    wlay->gui.dragging = false;

    /* GUI */
    ctx->style.window.padding = nk_vec2(20, 20);
    if (nk_begin(ctx, "", nk_rect(0, 0, window_width, window_height), 0))
    {
        struct wlay_head *head;
        struct wlay_head *focused_head = NULL;
        wl_list_for_each(head, &wlay->wl.heads, link) {
            if (head->focused) {
                focused_head = head;
            }
        }
        nk_layout_space_begin(ctx, NK_STATIC, 300, wl_list_length(&wlay->wl.heads));
        {
            wl_list_for_each(head, &wlay->wl.heads, link) {
                if (head->current_mode == NULL || head == focused_head) {
                    continue;
                }
                wlay_gui_editor_head(head);
            }
            // Render focused head on top
            if (focused_head != NULL) {
                wlay_gui_editor_head(focused_head);
            }
        }
        nk_layout_row_dynamic(ctx, 200, 1);
        if (focused_head != NULL) {
            wlay_gui_details(focused_head);
        }
        nk_layout_row_static(ctx, 30, 100, 1);
        nk_layout_row_static(ctx, 30, 100, 3);
        if (nk_button_label(ctx, "Apply")) {
            log_info("Apply");
            wlay->should_apply = true;
        }
    }
    if (nk_input_is_key_down(&ctx->input, NK_KEY_TAB)) {
        wlay_snap(wlay);
    }
    nk_end(ctx);
}


void wlay_push_settings(struct wlay_state *wlay)
{
    log_info("Pushing config");
    struct zwlr_output_configuration_v1 *config =
        zwlr_output_manager_v1_create_configuration(wlay->wl.output_manager, wlay->serial);
    // TODO: Handle failures
    struct wlay_head *head;
    wl_list_for_each(head, &wlay->wl.heads, link) {
        struct zwlr_output_configuration_head_v1 *cfg_head =
            zwlr_output_configuration_v1_enable_head(config, head->wlr);
        zwlr_output_configuration_head_v1_set_mode(cfg_head, head->current_mode->wlr);
        zwlr_output_configuration_head_v1_set_position(
            cfg_head, head->x, head->y
        );
        zwlr_output_configuration_head_v1_set_transform(
            cfg_head, head->transform
        );
        zwlr_output_configuration_head_v1_set_scale(
            cfg_head, head->scale
        );
    }
    zwlr_output_configuration_v1_apply(config);
}


int main(void)
{
    struct wlay_state wlay;
    memset(&wlay, 0, sizeof(wlay));
    wlay_gui_init(&wlay);

    wl_list_init(&wlay.wl.heads);
    wlay.wl.display = wl_display_connect(NULL);
    if (wlay.wl.display == NULL) {
        fail("wl_display_connect failed");
    }
    wlay.wl.registry = wl_display_get_registry(wlay.wl.display);
    wl_registry_add_listener(wlay.wl.registry, &registry_listener, &wlay);
    wl_display_dispatch(wlay.wl.display);
    wl_display_roundtrip(wlay.wl.display);

    if (wlay.wl.output_manager == NULL) {
        fail("compositor does not support wlr-output-management-unstable-v1");
    }

    while (!glfwWindowShouldClose(wlay.gl.window))
    {
        /* Input */
        glfwPollEvents();
        nk_glfw3_new_frame();

        wlay_gui(&wlay);
        if (wlay.should_apply) {
            wlay.should_apply = false;
            wlay_push_settings(&wlay);
            wl_display_dispatch(wlay.wl.display);
        }

        /* Draw */
        int width, height;
        glfwGetWindowSize(wlay.gl.window, &width, &height);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0., 0., 0., 1.);
        nk_glfw3_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glfwSwapBuffers(wlay.gl.window);
    }
    nk_glfw3_shutdown();
    glfwTerminate();
    return 0;
}

