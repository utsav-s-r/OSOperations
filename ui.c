#include "ui.h"
#include "os_stats.h"

#include <stdlib.h>

WINDOW *hdr_win = NULL;
WINDOW *main_win = NULL;
WINDOW *ftr_win = NULL;

int max_y = 0;
int max_x = 0;
int current_mode = 'p';
bool is_running = true;
int scroll_offset = 0;
int sched_scroll_offset = 0;

char ftr_status_msg[128] = "";
int ftr_status_color = 0;

int cpu_history[50] = {0};
int mem_history[50] = {0};
int temp_history[50] = {0};

void draw_bar(WINDOW *win, int y, int x, const char *label, double percentage,
              int width) {
  wattron(win, COLOR_PAIR(C_HEADER));
  mvwprintw(win, y, x, "%-5s [", label);
  wattroff(win, COLOR_PAIR(C_HEADER));

  int bars = (int)((percentage / 100.0) * width);
  int color = (percentage < 50.0) ? C_HEALTHY
                                  : ((percentage < 85.0) ? C_NORMAL : C_STRESS);

  wattron(win, COLOR_PAIR(color) | A_BOLD);
  for (int i = 0; i < width; i++) {
    if (i < bars) {
      waddch(win, '|');
    } else {
      waddch(win, ' ');
    }
  }
  wattroff(win, COLOR_PAIR(color) | A_BOLD);

  wattron(win, COLOR_PAIR(C_HEADER));
  waddch(win, ']');
  wprintw(win, " %.1f%%", percentage);
  wattroff(win, COLOR_PAIR(C_HEADER));
}

void draw_header(void) {
  werase(hdr_win);
  wattron(hdr_win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(hdr_win, 1, 2, "=== Zenith-OS Diagnostics ===");
  wattroff(hdr_win, COLOR_PAIR(C_HEADER) | A_BOLD);

  double cpu = get_cpu_load();
  draw_bar(hdr_win, 2, 2, "CPU", cpu, 20);

  double totMem = 0, availMem = 0;
  get_mem_stats(&totMem, &availMem);
  double memPct = (totMem > 0) ? ((totMem - availMem) / totMem) * 100.0 : 0.0;
  draw_bar(hdr_win, 3, 2, "MEM", memPct, 20);

  box(hdr_win, 0, 0);
}

void draw_footer(void) {
  werase(ftr_win);
  wattron(ftr_win, COLOR_PAIR(C_HEADER));
  mvwprintw(
      ftr_win, 2, 2,
      " (P)roc | (M)em | (D)isk | (I)PC | (C)PU | (T)emp | (G)host | (S)ync | (X)Sched | (K)ill | (Q)uit ");
  wattroff(ftr_win, COLOR_PAIR(C_HEADER));

  if (ftr_status_msg[0] != '\0') {
    wattron(ftr_win, COLOR_PAIR(ftr_status_color) | A_BOLD);
    mvwprintw(ftr_win, 3, 2, " %s", ftr_status_msg);
    wattroff(ftr_win, COLOR_PAIR(ftr_status_color) | A_BOLD);
  }
}

void draw_graph(WINDOW *win, int y, int x, int *history, int size) {
  for (int i = 0; i < size; i++) {
    int val = history[i];
    int color = (val < 50) ? C_HEALTHY : C_STRESS;

    char bar_char = ' ';
    if (val > 80)
      bar_char = '|';
    else if (val > 60)
      bar_char = '!';
    else if (val > 40)
      bar_char = ':';
    else if (val > 20)
      bar_char = '.';
    else if (val > 0)
      bar_char = ',';

    wattron(win, COLOR_PAIR(color) | A_BOLD);
    mvwaddch(win, y, x + i, bar_char);
    wattroff(win, COLOR_PAIR(color) | A_BOLD);
  }
}
