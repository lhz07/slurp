#ifndef _RENDER_H
#define _RENDER_H

struct slurp_output;
struct slurp_box;

void render(struct slurp_output *output);
void render_selected(struct slurp_output *output, struct slurp_box fixed_box, char *status);

#endif
