#include "ui.h"
#include "modules.h"
#include "sync.h"
#include "scheduler.h"
#include "os_stats.h"
//cd /Users/utsavsr/Developer/OS_Demo && rm -rf build && cmake -S . -B build && cmake --build build && ./build/zenith_os
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void main_event_loop(void) {
  while (is_running) {
    for (int i = 0; i < 49; i++) {
      cpu_history[i] = cpu_history[i + 1];
      mem_history[i] = mem_history[i + 1];
      temp_history[i] = temp_history[i + 1];
    }

    double cpu_now = get_cpu_load();
    double totMem = 0, availMem = 0;
    get_mem_stats(&totMem, &availMem);
    double memPct = (totMem > 0) ? ((totMem - availMem) / totMem) * 100.0 : 0.0;
    cpu_history[49] = (int)cpu_now;
    mem_history[49] = (int)memPct;
    temp_history[49] = (int)get_temperature();

    draw_header();

    werase(main_win);
    switch (current_mode) {
    case 'p':
      do_proc(main_win);
      break;
    case 'm':
      do_mem(main_win);
      break;
    case 'd':
      do_disk(main_win);
      break;
    case 'i':
      do_ipc(main_win);
      break;
    case 'c':
      do_cpu(main_win);
      break;
    case 'g':
      do_orphan(main_win);
      break;
    case 's':
      do_sync(main_win);
      break;
    case 't':
      do_temp(main_win);
      break;
    case 'x':
      do_scheduler(main_win);
      break;
    }
    box(main_win, 0, 0);

    draw_footer();
    box(ftr_win, 0, 0);

    wnoutrefresh(hdr_win);
    wnoutrefresh(main_win);
    wnoutrefresh(ftr_win);
    doupdate();

    int ch = getch();
    if (ch != ERR) {
      if (ch == KEY_UP) {
        if (current_mode == 'x') {
          if (sched_scroll_offset > 0)
            sched_scroll_offset--;
        } else {
          if (scroll_offset > 0)
            scroll_offset--;
        }
      } else if (ch == KEY_DOWN) {
        if (current_mode == 'x') {
          sched_scroll_offset++;
        } else {
          scroll_offset++;
        }
      } else {
        ch = tolower(ch);
        if (ch == 'q') {
          is_running = false;
        } else if (ch == 'k') {
          do_signals(main_win);
        } else if (strchr("pmdicgstx", ch) && (ch != 'x' || current_mode != 'x')) {
          /* Mode switching always works */
          if (ch == 's') {
            if (current_mode != 's') {
              current_mode = 's';
              scroll_offset = 0;
            } else if (sync_can_start()) {
              start_sync_test();
            }
          } else {
            if (current_mode != ch) {
              current_mode = ch;
              if (ch != 'x') scroll_offset = 0;
            }
          }
        } else {
          /* Scheduler-specific input (e, r, n, digits, enter, backspace, ESC) */
          if (current_mode == 'x') {
            scheduler_handle_input(ch);
          }
        }
      }
    }
  }
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  initscr();
  srand((unsigned)time(NULL));

  for (int i = 0; i < 5; i++) {
    get_cpu_load();
  }

  set_escdelay(25);
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  timeout(1000);

  if (has_colors()) {
    start_color();
    init_pair(C_HEALTHY, COLOR_GREEN, COLOR_BLACK);
    init_pair(C_STRESS, COLOR_RED, COLOR_BLACK);
    init_pair(C_HEADER, COLOR_CYAN, COLOR_BLACK);
    init_pair(C_NORMAL, COLOR_WHITE, COLOR_BLACK);
    init_pair(C_GHOST, COLOR_RED, COLOR_BLACK);
    init_pair(C_WARNING, COLOR_YELLOW, COLOR_BLACK);
  }

  clearok(stdscr, TRUE);

  getmaxyx(stdscr, max_y, max_x);
  const int header_h = 5;
  const int footer_h = 7;
  const int dead_zone = 2;
  const int main_h = max_y - header_h - footer_h - dead_zone;

  hdr_win = newwin(header_h, max_x, 0, 0);
  main_win = newwin(main_h > 1 ? main_h : 1, max_x, header_h, 0);
  ftr_win = newwin(footer_h, max_x, max_y - footer_h, 0);

  main_event_loop();

  cleanup_sync();

  delwin(hdr_win);
  delwin(main_win);
  delwin(ftr_win);
  endwin();

  return 0;
}
