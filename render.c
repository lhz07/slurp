#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>

#include "pool-buffer.h"
#include "render.h"
#include "slurp.h"

static void set_source_u32(cairo_t *cairo, uint32_t color) {
	cairo_set_source_rgba(cairo, (color >> (3 * 8) & 0xFF) / 255.0,
		(color >> (2 * 8) & 0xFF) / 255.0,
		(color >> (1 * 8) & 0xFF) / 255.0,
		(color >> (0 * 8) & 0xFF) / 255.0);
}

static void draw_rect(cairo_t *cairo, struct slurp_box *box, uint32_t color) {
	set_source_u32(cairo, color);
	cairo_rectangle(cairo, box->x, box->y,
			box->width, box->height);
}

static void box_layout_to_output(struct slurp_box *box, struct slurp_output *output) {
	box->x -= output->logical_geometry.x;
	box->y -= output->logical_geometry.y;
}

void render(struct slurp_output *output) {
	struct slurp_state *state = output->state;
	struct pool_buffer *buffer = output->current_buffer;
	cairo_t *cairo = buffer->cairo;

	// Clear
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	set_source_u32(cairo, state->colors.background);
	cairo_paint(cairo);

	// Draw option boxes from input
	struct slurp_box *choice_box;
	wl_list_for_each(choice_box, &state->boxes, link) {
		if (box_intersect(&output->logical_geometry,
					choice_box)) {
			struct slurp_box b = *choice_box;
			box_layout_to_output(&b, output);
			draw_rect(cairo, &b, state->colors.choice);
			cairo_fill(cairo);
		}
	}

	struct slurp_seat *seat;
	wl_list_for_each(seat, &state->seats, link) {
		struct slurp_selection *current_selection =
			slurp_seat_current_selection(seat);

		if (!current_selection->has_selection) {
			continue;
		}

		if (!box_intersect(&output->logical_geometry,
			&current_selection->selection)) {
			continue;
		}
		struct slurp_box b = current_selection->selection;
		box_layout_to_output(&b, output);

		draw_rect(cairo, &b, state->colors.selection);
		cairo_fill(cairo);

		// Draw border
		cairo_set_line_width(cairo, state->border_weight);
		draw_rect(cairo, &b, state->colors.border);
		cairo_stroke(cairo);

		if (state->display_dimensions) {
			cairo_select_font_face(cairo, state->font_family,
					       CAIRO_FONT_SLANT_NORMAL,
					       CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cairo, 14);
			set_source_u32(cairo, state->colors.border);
			// buffer of 12 can hold selections up to 99999x99999
			char dimensions[12];
			snprintf(dimensions, sizeof(dimensions), "%ix%i",
				 b.width, b.height);
			cairo_move_to(cairo, b.x + b.width + 10,
				      b.y + b.height + 20);
			cairo_show_text(cairo, dimensions);
		}
	}
}
void render_selected(struct slurp_output *output, struct slurp_box fixed_box, char *status)
{
    struct slurp_state *state = output->state;
    struct pool_buffer *buffer = output->current_buffer;
    cairo_t *cairo = buffer->cairo;
    if (!output->configured) {
        return;
    }

    // Clear
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    set_source_u32(cairo, state->colors.background);
    cairo_paint(cairo);

    // 直接绘制固定大小的矩形，不依赖 seat 状态
    // struct slurp_box fixed_box = {.x = 1000, .y = 237, .width = 800, .height = 600};

    // 调整到输出坐标系
    box_layout_to_output(&fixed_box, output);

    // 绘制选择区域
    draw_rect(cairo, &fixed_box, state->colors.selection);
    cairo_fill(cairo);

    // 绘制边框
    cairo_set_line_width(cairo, 2);           // 设置固定的边框宽度
    draw_rect(cairo, &fixed_box, 0xFF0000FF); // 使用红色边框
    cairo_stroke(cairo);

    // 显示尺寸标签
    cairo_select_font_face(cairo, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cairo, 14);
    set_source_u32(cairo, 0xFF0000FF); // 红色文字
    char dimensions[32];
    snprintf(dimensions,
             sizeof(dimensions),
             "%dx%d  %s",
             fixed_box.width - 4,
             fixed_box.height - 4,
             status);
    cairo_move_to(cairo, fixed_box.x, fixed_box.y - 10);
    cairo_show_text(cairo, dimensions);

    // 确保渲染生效
    wl_surface_commit(output->surface);
}
