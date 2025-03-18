#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "slurp.h"
#include "slurp_tool.h"
#include "render.h"

#define BG_COLOR 0xFFFFFF40
#define BORDER_COLOR 0x000000FF
#define SELECTION_COLOR 0x00000000
#define FONT_FAMILY "sans-serif"
atomic_bool running = true;
struct wl_display *global_display = 0;
struct slurp_box global_area;
struct wl_list *global_outputs;

static void noop()
{
    // This space intentionally left blank
}

static void set_output_dirty(struct slurp_output *output);
void change_running()
{
    atomic_store(&running, false);
    wl_display_roundtrip(global_display);
}
void change_text(char *text)
{
    struct slurp_output *output;
    wl_list_for_each(output, global_outputs, link)
    {
        render_selected(output, global_area, text);
    }
    wl_display_roundtrip(global_display);
}
bool box_intersect(const struct slurp_box *a, const struct slurp_box *b);
// {
//     return a->x < b->x + b->width &&
//            a->x + a->width > b->x &&
//            a->y < b->y + b->height &&
//            a->height + a->y > b->y;
// }

static bool in_box(const struct slurp_box *box, int32_t x, int32_t y)
{
    return box->x <= x && box->x + box->width > x && box->y <= y && box->y + box->height > y;
}

static int32_t box_size(const struct slurp_box *box)
{
    return box->width * box->height;
}

static int max(int a, int b)
{
    return (a > b) ? a : b;
}

static int min(int a, int b)
{
    return (a < b) ? a : b;
}

static struct slurp_output *output_from_surface(struct slurp_state *state,
                                                struct wl_surface *surface);

// static void move_seat(struct slurp_seat *seat, wl_fixed_t surface_x,
//                       wl_fixed_t surface_y,
//                       struct slurp_selection *current_selection)
// {
//     int x = wl_fixed_to_int(surface_x) +
//             current_selection->current_output->logical_geometry.x;
//     int y = wl_fixed_to_int(surface_y) + current_selection->current_output->logical_geometry.y;

//     if (seat->state->edit_anchor)
//     {
//         current_selection->anchor_x += x - current_selection->x;
//         current_selection->anchor_y += y - current_selection->y;
//     }

//     current_selection->x = x;
//     current_selection->y = y;
// }

// static void seat_update_selection(struct slurp_seat *seat)
// {
//     seat->pointer_selection.has_selection = false;

//     // find smallest box intersecting the cursor
//     struct slurp_box *box;
//     wl_list_for_each(box, &seat->state->boxes, link)
//     {
//         if (in_box(box, seat->pointer_selection.x,
//                    seat->pointer_selection.y))
//         {
//             if (seat->pointer_selection.has_selection &&
//                 box_size(
//                     &seat->pointer_selection.selection) <
//                     box_size(box))
//             {
//                 continue;
//             }
//             seat->pointer_selection.selection = *box;
//             seat->pointer_selection.has_selection = true;
//         }
//     }
// }

static void seat_set_outputs_dirty(struct slurp_seat *seat)
{
    struct slurp_output *output;
    wl_list_for_each(output, &seat->state->outputs, link)
    {
        if (box_intersect(&output->logical_geometry, &seat->pointer_selection.selection)
            || box_intersect(&output->logical_geometry, &seat->touch_selection.selection)) {
            set_output_dirty(output);
        }
    }
}

// static void create_seat(struct slurp_state *state, struct wl_seat *wl_seat)
// {
//     struct slurp_seat *seat = calloc(1, sizeof(struct slurp_seat));
//     if (seat == NULL)
//     {
//         fprintf(stderr, "allocation failed\n");
//         return;
//     }
//     seat->state = state;
//     seat->wl_seat = wl_seat;
//     seat->touch_id = TOUCH_ID_EMPTY;
//     wl_list_insert(&state->seats, &seat->link);
// }

// static void destroy_seat(struct slurp_seat *seat)
// {
//     wl_list_remove(&seat->link);
//     wl_surface_destroy(seat->cursor_surface);
//     if (seat->wl_pointer)
//     {
//         wl_pointer_destroy(seat->wl_pointer);
//     }
//     if (seat->wl_keyboard)
//     {
//         wl_keyboard_destroy(seat->wl_keyboard);
//     }
//     xkb_state_unref(seat->xkb_state);
//     xkb_keymap_unref(seat->xkb_keymap);
//     wl_seat_destroy(seat->wl_seat);
//     free(seat);
// }

static void output_handle_geometry(void *data, struct wl_output *wl_output,
                                   int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
                                   int32_t subpixel, const char *make, const char *model,
                                   int32_t transform)
{
    struct slurp_output *output = data;

    output->geometry.x = x;
    output->geometry.y = y;
}

static void output_handle_mode(void *data, struct wl_output *wl_output,
                               uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
    struct slurp_output *output = data;
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0)
    {
        return;
    }
    output->geometry.width = width;
    output->geometry.height = height;
}

static void output_handle_scale(void *data, struct wl_output *wl_output,
                                int32_t scale)
{
    struct slurp_output *output = data;

    output->scale = scale;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = noop,
    .scale = output_handle_scale,
};

static void xdg_output_handle_logical_position(void *data,
                                               struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y)
{
    struct slurp_output *output = data;
    output->logical_geometry.x = x;
    output->logical_geometry.y = y;
}

static void xdg_output_handle_logical_size(void *data,
                                           struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height)
{
    struct slurp_output *output = data;
    output->logical_geometry.width = width;
    output->logical_geometry.height = height;
}

static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name)
{
    struct slurp_output *output = data;
    output->logical_geometry.label = strdup(name);
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = noop,
    .name = xdg_output_handle_name,
    .description = noop,
};

static void create_output(struct slurp_state *state,
                          struct wl_output *wl_output)
{
    struct slurp_output *output = calloc(1, sizeof(struct slurp_output));
    if (output == NULL)
    {
        fprintf(stderr, "allocation failed\n");
        return;
    }
    output->wl_output = wl_output;
    output->state = state;
    output->scale = 1;
    wl_list_insert(&state->outputs, &output->link);

    wl_output_add_listener(wl_output, &output_listener, output);
}

static void destroy_output(struct slurp_output *output)
{
    if (output == NULL)
    {
        return;
    }
    wl_list_remove(&output->link);
    finish_buffer(&output->buffers[0]);
    finish_buffer(&output->buffers[1]);
    if (output->cursor_theme)
    {
        wl_cursor_theme_destroy(output->cursor_theme);
    }
    zwlr_layer_surface_v1_destroy(output->layer_surface);
    if (output->xdg_output)
    {
        zxdg_output_v1_destroy(output->xdg_output);
    }
    wl_surface_destroy(output->surface);
    if (output->frame_callback)
    {
        wl_callback_destroy(output->frame_callback);
    }
    wl_output_destroy(output->wl_output);
    free(output->logical_geometry.label);
    free(output);
}

static const struct wl_callback_listener output_frame_listener;

static void send_frame(struct slurp_output *output)
{
    struct slurp_state *state = output->state;

    if (!output->configured)
    {
        return;
    }

    int32_t buffer_width = output->width * output->scale;
    int32_t buffer_height = output->height * output->scale;

    output->current_buffer = get_next_buffer(state->shm, output->buffers,
                                             buffer_width, buffer_height);
    if (output->current_buffer == NULL)
    {
        return;
    }
    output->current_buffer->busy = true;

    cairo_identity_matrix(output->current_buffer->cairo);
    cairo_scale(output->current_buffer->cairo, output->scale, output->scale);

    render_selected(output, global_area, "Ready to record");

    // Schedule a frame in case the output becomes dirty again
    output->frame_callback = wl_surface_frame(output->surface);
    wl_callback_add_listener(output->frame_callback,
                             &output_frame_listener, output);

    wl_surface_attach(output->surface, output->current_buffer->buffer, 0, 0);
    wl_surface_damage(output->surface, 0, 0, output->width, output->height);
    wl_surface_set_buffer_scale(output->surface, output->scale);
    wl_surface_commit(output->surface);
    output->dirty = false;
}

static void output_frame_handle_done(void *data, struct wl_callback *callback,
                                     uint32_t time)
{
    struct slurp_output *output = data;

    wl_callback_destroy(callback);
    output->frame_callback = NULL;

    if (output->dirty)
    {
        send_frame(output);
    }
}

static const struct wl_callback_listener output_frame_listener = {
    .done = output_frame_handle_done,
};

static void set_output_dirty(struct slurp_output *output)
{
    output->dirty = true;
    if (output->frame_callback) {
        return;
    }

    // 清理旧的帧回调（如果存在）
    if (output->frame_callback) {
        wl_callback_destroy(output->frame_callback);
        output->frame_callback = NULL;
    }

    output->frame_callback = wl_surface_frame(output->surface);
    wl_callback_add_listener(output->frame_callback, &output_frame_listener, output);
    wl_surface_commit(output->surface);
}

static struct slurp_output *output_from_surface(struct slurp_state *state,
                                                struct wl_surface *surface)
{
    struct slurp_output *output;
    wl_list_for_each(output, &state->outputs, link)
    {
        if (output->surface == surface)
        {
            return output;
        }
    }
    return NULL;
}

static void layer_surface_handle_configure(void *data,
                                           struct zwlr_layer_surface_v1 *surface,
                                           uint32_t serial, uint32_t width, uint32_t height)
{
    struct slurp_output *output = data;

    output->configured = true;
    output->width = width;
    output->height = height;

    zwlr_layer_surface_v1_ack_configure(surface, serial);
    send_frame(output);
}

static void layer_surface_handle_closed(void *data,
                                        struct zwlr_layer_surface_v1 *surface)
{
    struct slurp_output *output = data;
    destroy_output(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface, uint32_t version)
{
    struct slurp_state *state = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        state->compositor = wl_registry_bind(registry, name,
                                             &wl_compositor_interface, 4);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        state->shm = wl_registry_bind(registry, name,
                                      &wl_shm_interface, 1);
    }
    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
    {
        state->layer_shell = wl_registry_bind(registry, name,
                                              &zwlr_layer_shell_v1_interface, 1);
    }
    // else if (strcmp(interface, wl_seat_interface.name) == 0)
    // {
    //     struct wl_seat *wl_seat =
    //         wl_registry_bind(registry, name, &wl_seat_interface, 1);
    //     create_seat(state, wl_seat);
    // }
    else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *wl_output =
            wl_registry_bind(registry, name, &wl_output_interface, 3);
        create_output(state, wl_output);
    } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        state->xdg_output_manager = wl_registry_bind(registry, name,
                                                     &zxdg_output_manager_v1_interface, 2);
    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        state->cursor_shape_manager = wl_registry_bind(registry, name,
                                                       &wp_cursor_shape_manager_v1_interface, 1);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = noop,
};

static const char usage[] =
    "Usage: slurp [options...]\n"
    "\n"
    "  -h           Show help message and quit.\n"
    "  -d           Display dimensions of selection.\n"
    "  -b #rrggbbaa Set background color.\n"
    "  -c #rrggbbaa Set border color.\n"
    "  -s #rrggbbaa Set selection color.\n"
    "  -B #rrggbbaa Set option box color.\n"
    "  -F s         Set the font family for the dimensions.\n"
    "  -w n         Set border weight.\n"
    "  -f s         Set output format.\n"
    "  -o           Select a display output.\n"
    "  -p           Select a single point.\n"
    "  -r           Restrict selection to predefined boxes.\n"
    "  -a w:h       Force aspect ratio.\n";

static struct slurp_output *output_from_box(const struct slurp_box *box, struct wl_list *outputs)
{
    struct slurp_output *output;
    wl_list_for_each(output, outputs, link)
    {
        struct slurp_box *geometry = &output->logical_geometry;
        // For now just use the top-left corner
        if (in_box(geometry, box->x, box->y))
        {
            return output;
        }
    }
    return NULL;
}

static void print_output_name(FILE *stream, const struct slurp_box *result, struct wl_list *outputs)
{
    struct slurp_output *output = output_from_box(result, outputs);
    if (output)
    {
        struct slurp_box *geometry = &output->logical_geometry;
        if (geometry->label)
        {
            fprintf(stream, "%s", geometry->label);
            return;
        }
    }
    fprintf(stream, "<unknown>");
}

// static void initialize_fixed_selection(struct slurp_state *state)
// {
//     struct slurp_seat *seat = wl_container_of(state->seats.next, seat, link);
//     if (seat) {
//         // 设置固定的选择区域
//         seat->pointer_selection.has_selection = true;
//         seat->pointer_selection.selection.x = 100; // 可以调整起始位置
//         seat->pointer_selection.selection.y = 100;
//         seat->pointer_selection.selection.width = 800;
//         seat->pointer_selection.selection.height = 600;
//     }
// }

int show_selected_area(int argc, char *argv[], struct screen_box area)
{
    int status = EXIT_SUCCESS;
    global_area.height = area.height;
    global_area.width = area.width;
    global_area.x = area.x;
    global_area.y = area.y;
    struct slurp_state state = {
        .colors = {
            .background = BG_COLOR,
            .border = BORDER_COLOR,
            .selection = SELECTION_COLOR,
            .choice = BG_COLOR,
        },
        .border_weight = 2,
        .display_dimensions = false,
        .restrict_selection = false,
        .fixed_aspect_ratio = false,
        .aspect_ratio = 0,
        .font_family = FONT_FAMILY};

    int opt;
    char *format = "%x,%y %wx%h\n";
    bool output_boxes = false;
    int w, h;
    state.colors.background = 0x00000000;
    state.colors.border = 0xFFFFFFFF;
    while ((opt = getopt(argc, argv, "hdb:c:s:B:w:proa:f:F:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            printf("%s", usage);
            return EXIT_SUCCESS;
        case 'd':
            state.display_dimensions = true;
            break;
        case 'b':
            state.colors.background = 0x00000000;
            break;
        case 'c':
            // state.colors.border = parse_color(optarg);
            break;
        case 's':
            // state.colors.selection = parse_color(optarg);
            break;
        case 'B':
            // state.colors.choice = parse_color(optarg);
            break;
        case 'f':
            format = optarg;
            break;
        case 'F':
            state.font_family = optarg;
            break;
        case 'w':
        {
            errno = 0;
            char *endptr;
            state.border_weight = strtol(optarg, &endptr, 10);
            if (*endptr || errno)
            {
                fprintf(stderr, "Error: expected numeric argument for -w\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case 'p':
            state.single_point = true;
            break;
        case 'o':
            output_boxes = true;
            break;
        case 'r':
            state.restrict_selection = true;
            break;
        case 'a':
            if (sscanf(optarg, "%d:%d", &w, &h) != 2)
            {
                fprintf(stderr, "invalid aspect ratio\n");
                return EXIT_FAILURE;
            }
            if (w <= 0 || h <= 0)
            {
                fprintf(stderr, "width and height of aspect ratio must be greater than zero\n");
                return EXIT_FAILURE;
            }
            state.fixed_aspect_ratio = true;
            state.aspect_ratio = (double)h / w;
            break;
        default:
            printf("%s", usage);
            return EXIT_FAILURE;
        }
    }

    if (state.single_point && state.restrict_selection)
    {
        fprintf(stderr, "-p and -r cannot be used together\n");
        return EXIT_FAILURE;
    }

    wl_list_init(&state.boxes);
    if (!isatty(STDIN_FILENO) && !state.single_point)
    {
        char *line = NULL;
        size_t line_size = 0;
        while (getline(&line, &line_size, stdin) >= 0)
        {
            struct slurp_box in_box = {0};
            if (sscanf(line, "%d,%d %dx%d %m[^\n]", &in_box.x, &in_box.y,
                       &in_box.width, &in_box.height, &in_box.label) < 4)
            {
                fprintf(stderr, "invalid box format: %s\n", line);
                return EXIT_FAILURE;
            }
            // add_choice_box(&state, &in_box);
            free(in_box.label);
        }
        free(line);
    }
    wl_list_init(&state.outputs);
    // wl_list_init(&state.seats);

    state.display = wl_display_connect(NULL);
    if (state.display == NULL)
    {
        fprintf(stderr, "failed to create display\n");
        return EXIT_FAILURE;
    }

    if ((state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS)) == NULL)
    {
        fprintf(stderr, "xkb_context_new failed\n");
        return EXIT_FAILURE;
    }

    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    if (state.compositor == NULL)
    {
        fprintf(stderr, "compositor doesn't support wl_compositor\n");
        return EXIT_FAILURE;
    }
    if (state.shm == NULL)
    {
        fprintf(stderr, "compositor doesn't support wl_shm\n");
        return EXIT_FAILURE;
    }
    if (state.layer_shell == NULL)
    {
        fprintf(stderr, "compositor doesn't support zwlr_layer_shell_v1\n");
        return EXIT_FAILURE;
    }
    if (state.xdg_output_manager == NULL)
    {
        fprintf(stderr, "compositor doesn't support xdg-output. "
                        "Guessing geometry from physical output size.\n");
    }
    if (wl_list_empty(&state.outputs))
    {
        fprintf(stderr, "no wl_output\n");
        return EXIT_FAILURE;
    }

    struct slurp_output *output;
    wl_list_for_each(output, &state.outputs, link)
    {
        output->surface = wl_compositor_create_surface(state.compositor);
        struct wl_region *input_region = wl_compositor_create_region(state.compositor);
        wl_surface_set_input_region(output->surface, input_region);
        // TODO: wl_surface_add_listener(output->surface, &surface_listener, output);

        output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            state.layer_shell, output->surface, output->wl_output,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "selection");
        zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                           &layer_surface_listener, output);

        if (state.xdg_output_manager)
        {
            output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
                state.xdg_output_manager, output->wl_output);
            zxdg_output_v1_add_listener(output->xdg_output,
                                        &xdg_output_listener, output);
        }
        else
        {
            // guess
            output->logical_geometry = output->geometry;
            output->logical_geometry.width /= output->scale;
            output->logical_geometry.height /= output->scale;
        }
        zwlr_layer_surface_v1_set_anchor(output->layer_surface,
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                                             | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                                             | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
                                             | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
        zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, false);
        zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
        wl_surface_commit(output->surface);
    }
    // second roundtrip for xdg-output

    wl_display_roundtrip(state.display);
    global_display = state.display;
    // bool create_seat = false;

    // struct slurp_seat *seat;
    // wl_list_for_each(seat, &state.seats, link)
    // {
    //     seat->cursor_surface =
    //         wl_compositor_create_surface(state.compositor);
    // }
    // create_seat = true;

    // 添加初始化固定选择
    // initialize_fixed_selection(&state);
    {
        struct slurp_output *output;
        wl_list_for_each(output, &state.outputs, link)
        {
            set_output_dirty(output);
            // 使用 frame callback 机制触发渲染
            output->frame_callback = wl_surface_frame(output->surface);
            wl_callback_add_listener(output->frame_callback, &output_frame_listener, output);
            wl_surface_commit(output->surface);
        }
    }
    global_outputs = &state.outputs;
    atomic_store(&running, true);
    state.running = true;
    // printf("%d", state.running);

    while (atomic_load(&running) && wl_display_dispatch(state.display) != -1) {
        // Wayland event loop
        // printf("%d", atomic_load(&running));
    }
    // printf("test%d", running);
    // char *result_str = 0;
    // size_t length;
    // if (state.result.width == 0 && state.result.height == 0) {
    //     fprintf(stderr, "selection cancelled\n");
    //     status = EXIT_FAILURE;
    // } else {
    //     FILE *stream = open_memstream(&result_str, &length);
    //     print_formatted_result(stream, &state, format);
    //     fclose(stream);
    // }
    {
        struct slurp_output *output_tmp;

        wl_list_for_each_safe(output, output_tmp, &state.outputs, link)
        {
            destroy_output(output);
        }
    }
    // if (create_seat)
    // {
    //     struct slurp_seat *seat_tmp;
    //     wl_list_for_each_safe(seat, seat_tmp, &state.seats, link)
    //     {
    //         destroy_seat(seat);
    //     }
    // }

    // Make sure the compositor has unmapped our surfaces by the time we exit
    wl_display_roundtrip(state.display);

    zwlr_layer_shell_v1_destroy(state.layer_shell);
    if (state.xdg_output_manager != NULL)
    {
        zxdg_output_manager_v1_destroy(state.xdg_output_manager);
    }
    if (state.cursor_shape_manager != NULL)
    {
        wp_cursor_shape_manager_v1_destroy(state.cursor_shape_manager);
    }
    wl_compositor_destroy(state.compositor);
    wl_shm_destroy(state.shm);
    wl_registry_destroy(state.registry);
    xkb_context_unref(state.xkb_context);
    wl_display_disconnect(state.display);

    struct slurp_box *box, *box_tmp;
    wl_list_for_each_safe(box, box_tmp, &state.boxes, link)
    {
        wl_list_remove(&box->link);
        free(box->label);
        free(box);
    }
    // if (result_str) {
    //     free(result_str);
    // }
    return status;
}
