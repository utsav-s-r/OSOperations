#include "ui.h"
#include "modules.h"
#include "sync.h"
#include "scheduler.h"
#include "os_stats.h"
#include "fs_sim.h"
#include "web_server.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
//make clean && make && ./bin/zenith_os --web --port 8080
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
    case 'f':
      do_fs(main_win);
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
        if (ch == 'q' && !(current_mode == 'f' && fs_input_active)) {
          is_running = false;
        } else if (ch == 'k' && !(current_mode == 'f' && fs_input_active)) {
          do_signals(main_win);
        } else if (strchr("pmdicgstxf", ch) && 
                   !(current_mode == 'x' && ch == 'x') && 
                   !(current_mode == 'f' && (ch == 'f' || fs_input_active))) {
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
              if (ch != 'x' && ch != 'f') scroll_offset = 0;
            }
          }
        } else {
          /* Scheduler and FS specific input */
          if (current_mode == 'x') {
            scheduler_handle_input(ch);
          } else if (current_mode == 'f') {
            fs_handle_input(ch);
          }
        }
      }
    }
  }
}

int main(int argc, char *argv[]) {
  int port = 8080;

  // Parse command-line arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--help") == 0) {
      printf("Zenith-OS - System Diagnostics Tool\n");
      printf("Usage: zenith_os [OPTIONS]\n");
      printf("Options:\n");
      printf("  --port PORT        Specify port for web server (default: 8080)\n");
      printf("  --help             Show this help message\n");
      return 0;
    }
  }

  // Start the web server by default
  start_web_server(port);
  printf("Press Ctrl+C to stop the server...\n");
  while (is_web_server_running()) {
    sleep(1);
  }

  cleanup_sync();

  return 0;
}
