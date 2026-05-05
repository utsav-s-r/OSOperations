#ifndef ZENITH_UI_H
#define ZENITH_UI_H

#include <curses.h>
#include <stdbool.h>

#define C_HEALTHY 1
#define C_STRESS 2
#define C_HEADER 3
#define C_NORMAL 4
#define C_GHOST 5
#define C_WARNING 6

extern WINDOW *hdr_win;
extern WINDOW *main_win;
extern WINDOW *ftr_win;

extern int max_y;
extern int max_x;
extern int current_mode;
extern bool is_running;
extern int scroll_offset;

extern char ftr_status_msg[128];
extern int ftr_status_color;

extern int cpu_history[50];
extern int mem_history[50];
extern int temp_history[50];

void draw_bar(WINDOW *win, int y, int x, const char *label, double percentage,
              int width);
void draw_header(void);
void draw_footer(void);
void draw_graph(WINDOW *win, int y, int x, int *history, int size);

#endif // ZENITH_UI_H
