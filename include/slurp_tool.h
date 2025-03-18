#ifndef SLURP_H
#define SLURP_H
#ifdef __cplusplus
extern "C" {
#endif
struct screen_box {
    int x, y;
    int width, height;
};
struct screen_output {
    struct screen_box geometry;
    struct screen_box logical_geometry;
    char *label;
    struct screen_output* next;
};

struct seletion_box {
    struct screen_box geometry;
    char* label;
};

int get_region(int argc, char *argv[], struct seletion_box *select_region, struct screen_output* screens);
int show_selected_area(int argc, char *argv[], struct screen_box area);
void change_running();
void change_text(char *text);

#ifdef __cplusplus
}
#endif

#endif
