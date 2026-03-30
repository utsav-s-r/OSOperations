/*
 * ZenithOS - A simple, cross-platform terminal-based operating system monitor.
 * mkdir build && cd build
 * cmake ..
 * make || cmake --build . || cmake --build . --target clean
 * ./zenith_os
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <curses.h> // requires pdcurses
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <windows.h>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <curses.h>
#include <libproc.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

// Color pairs
#define C_HEALTHY 1
#define C_STRESS 2
#define C_HEADER 3
#define C_NORMAL 4
#define C_GHOST 5

// Global UI Layout variables
WINDOW *hdr_win, *main_win, *ftr_win;
int max_y, max_x;
int current_mode = 'p';
bool is_running = true;
int scroll_offset = 0;

int cpu_history[50] = {0};
int mem_history[50] = {0};

// Mock or calculated system stats for the header
double get_cpu_load() {
#ifdef _WIN32
  // Windows simplified mock
  return 15.0;
#else
  // Dummy random for now as a real calculation requires multi-tick sampling
  return (double)(rand() % 100);
#endif
}

void get_mem_stats(double *totalMB, double *availMB) {
#ifdef _WIN32
  MEMORYSTATUSEX memInfo;
  memInfo.dwLength = sizeof(MEMORYSTATUSEX);
  if (GlobalMemoryStatusEx(&memInfo)) {
    *totalMB = memInfo.ullTotalPhys / (1024.0 * 1024.0);
    *availMB = memInfo.ullAvailPhys / (1024.0 * 1024.0);
  } else {
    *totalMB = 1000.0;
    *availMB = 500.0;
  }
#else
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vmstat;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
    uint64_t total_pages = vmstat.free_count + vmstat.active_count +
                           vmstat.inactive_count + vmstat.wire_count;
    *totalMB = (total_pages * getpagesize()) / (1024.0 * 1024.0);
    *availMB = ((vmstat.free_count + vmstat.inactive_count) * getpagesize()) /
               (1024.0 * 1024.0);
  } else {
    *totalMB = 1000.0;
    *availMB = 500.0;
  }
#endif
}

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

void draw_header() {
  werase(hdr_win);
  wattron(hdr_win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(hdr_win, 0, 2, "=== Zenith-OS Diagnostics ===");
  wattroff(hdr_win, COLOR_PAIR(C_HEADER) | A_BOLD);

  double cpu = get_cpu_load();
  draw_bar(hdr_win, 1, 2, "CPU", cpu, 20);

  double totMem = 0, availMem = 0;
  get_mem_stats(&totMem, &availMem);
  double memPct = (totMem > 0) ? ((totMem - availMem) / totMem) * 100.0 : 0.0;
  draw_bar(hdr_win, 2, 2, "MEM", memPct, 20);

  box(hdr_win, 0, 0);
  wnoutrefresh(hdr_win);
}

void draw_footer() {
  werase(ftr_win);
  wattron(ftr_win, COLOR_PAIR(C_HEADER));
  mvwprintw(ftr_win, 1, 2,
            " Shortcuts: (P)roc | (M)em | (D)isk | (I)PC | (C)PU | (G)host "
            "Hunter | (S)ync Demo | (Q)uit ");
  wattroff(ftr_win, COLOR_PAIR(C_HEADER));
  box(ftr_win, 0, 0);
  wnoutrefresh(ftr_win);
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

// ------ MODULES ------

void do_proc(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "%-10s %-10s %s", "PID", "PPID", "NAME");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

#ifdef _WIN32
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    int line = 2;
    int current_index = 0;
    if (Process32First(hSnap, &pe)) {
      do {
        if (current_index >= scroll_offset) {
          mvwprintw(win, line++, 2, "%-10lu %-10lu %s", pe.th32ProcessID,
                    pe.th32ParentProcessID, pe.szExeFile);
        }
        current_index++;
        if (line >= max_y - 8)
          break; // scroll limit
      } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
  }
#else
  pid_t pids[1024];
  int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
  int num_pids = count / sizeof(pid_t);
  int line = 2;
  int current_index = 0;
  for (int i = 0; i < num_pids && line < max_y - 8; i++) {
    if (pids[i] == 0)
      continue;
    struct proc_bsdinfo proc_info;
    if (proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &proc_info,
                     sizeof(proc_info)) == sizeof(proc_info)) {
      if (current_index >= scroll_offset) {
        mvwprintw(win, line++, 2, "%-10d %-10d %s", pids[i], proc_info.pbi_ppid,
                  proc_info.pbi_name);
      }
      current_index++;
    }
  }
#endif
}

void do_mem(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "Memory Information:");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

  double tMB = 0, aMB = 0;
  get_mem_stats(&tMB, &aMB);
  mvwprintw(win, 3, 2, "%-20s : %.2f MB", "Total Physical RAM", tMB);
  mvwprintw(win, 4, 2, "%-20s : %.2f MB", "Available RAM", aMB);

  mvwprintw(win, 6, 2, "Memory Usage History (50s):");
  draw_graph(win, 7, 2, mem_history, 50);
}

void do_disk(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "%-20s %s", "Mount Point", "Free Space (%)");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

#ifdef _WIN32
  DWORD drives = GetLogicalDrives();
  int line = 2;
  for (int i = 0; i < 26; i++) {
    if (drives & (1 << i)) {
      char path[] = {(char)('A' + i), ':', '\\', '\0'};
      ULARGE_INTEGER free_b, tot_b, tot_f;
      if (GetDiskFreeSpaceExA(path, &free_b, &tot_b, &tot_f) &&
          tot_b.QuadPart > 0) {
        double freePct = (double)free_b.QuadPart / tot_b.QuadPart * 100.0;
        wattron(win, (freePct > 20.0) ? COLOR_PAIR(C_HEALTHY)
                                      : COLOR_PAIR(C_STRESS));
        mvwprintw(win, line++, 2, "%-20s %.2f%%", path, freePct);
        wattroff(win, (freePct > 20.0) ? COLOR_PAIR(C_HEALTHY)
                                       : COLOR_PAIR(C_STRESS));
      }
    }
  }
#else
  struct statfs *mntbufp;
  int num_mounts = getmntinfo(&mntbufp, MNT_NOWAIT);
  int line = 2;
  for (int i = 0; i < num_mounts && line < max_y - 8; i++) {
    uint64_t total = mntbufp[i].f_blocks;
    uint64_t mfree = mntbufp[i].f_bavail;
    if (total > 0) {
      double freePct = (double)mfree / total * 100.0;
      wattron(win,
              (freePct > 20.0) ? COLOR_PAIR(C_HEALTHY) : COLOR_PAIR(C_STRESS));
      mvwprintw(win, line++, 2, "%-20s %.2f%%", mntbufp[i].f_mntonname,
                freePct);
      wattroff(win,
               (freePct > 20.0) ? COLOR_PAIR(C_HEALTHY) : COLOR_PAIR(C_STRESS));
    }
  }
#endif
}

void do_ipc(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "--- IPC Scanner ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

#ifdef _WIN32
  DWORD dwSize = 0;
  GetExtendedTcpTable(NULL, &dwSize, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
  PMIB_TCPTABLE_OWNER_PID pTcp = (PMIB_TCPTABLE_OWNER_PID)malloc(dwSize);
  if (GetExtendedTcpTable(pTcp, &dwSize, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL,
                          0) == NO_ERROR) {
    int line = 3;
    int current_index = 0;
    for (int i = 0; i < (int)pTcp->dwNumEntries && line < max_y - 8; i++) {
      if (current_index >= scroll_offset) {
        mvwprintw(win, line++, 2, "TCP PID %lu State: %lu",
                  pTcp->table[i].dwOwningPid, pTcp->table[i].dwState);
      }
      current_index++;
    }
  }
  free(pTcp);
#else
  mvwprintw(win, 2, 2, "%-10s %-20s %s", "PID", "P-Name", "Sockets Found");
  pid_t pids[1024];
  int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
  int line = 3;
  int num_pids = count / sizeof(pid_t);
  int current_index = 0;
  for (int i = 0; i < num_pids && line < max_y - 8; i++) {
    if (pids[i] == 0)
      continue;
    int bufSz = proc_pidinfo(pids[i], PROC_PIDLISTFDS, 0, NULL, 0);
    if (bufSz > 0) {
      struct proc_fdinfo *fds = malloc(bufSz);
      if (proc_pidinfo(pids[i], PROC_PIDLISTFDS, 0, fds, bufSz) == bufSz) {
        int scount = 0;
        int num_fds = bufSz / sizeof(struct proc_fdinfo);
        for (int j = 0; j < num_fds; j++) {
          if (fds[j].proc_fdtype == PROX_FDTYPE_SOCKET)
            scount++;
        }
        if (scount > 0) {
          if (current_index >= scroll_offset) {
            struct proc_bsdinfo bi;
            proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &bi, sizeof(bi));
            mvwprintw(win, line++, 2, "%-10d %-20s %d", pids[i], bi.pbi_name,
                      scount);
          }
          current_index++;
        }
      }
      free(fds);
    }
  }
#endif
}

void do_cpu(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "--- CPU Topology & HW Info ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

#ifdef _WIN32
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  mvwprintw(win, 3, 2, "Logical Processors: %lu", sysInfo.dwNumberOfProcessors);
  mvwprintw(win, 4, 2, "Architecture      : %u",
            sysInfo.wProcessorArchitecture);
  mvwprintw(win, 5, 2, "Page Size         : %lu bytes", sysInfo.dwPageSize);
#else
  int pcore = 0, ecore = 0;
  size_t sz = sizeof(pcore);
  sysctlbyname("hw.perflevel0.physicalcpu", &pcore, &sz, NULL, 0);
  sysctlbyname("hw.perflevel1.physicalcpu", &ecore, &sz, NULL, 0);
  uint64_t l1i = 0, l1d = 0, l2 = 0;
  sz = sizeof(uint64_t);
  sysctlbyname("hw.l1icachesize", &l1i, &sz, NULL, 0);
  sysctlbyname("hw.l1dcachesize", &l1d, &sz, NULL, 0);
  sysctlbyname("hw.l2cachesize", &l2, &sz, NULL, 0);

  mvwprintw(win, 3, 2, "Performance Cores: %d", pcore);
  mvwprintw(win, 4, 2, "Efficiency Cores : %d", ecore);
  mvwprintw(win, 5, 2, "L1 I-Cache       : %llu bytes", l1i);
  mvwprintw(win, 6, 2, "L1 D-Cache       : %llu bytes", l1d);
  mvwprintw(win, 7, 2, "L2 Cache         : %llu bytes", l2);
#endif

  mvwprintw(win, 9, 2, "CPU Usage History (50s):");
  draw_graph(win, 10, 2, cpu_history, 50);
}

void do_orphan(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "--- The Ghost Hunter (Orphans) ---");
  mvwprintw(win, 2, 2, "%-10s %-10s %s", "PID", "PPID", "NAME");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

#ifdef _WIN32
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    int line = 3;
    int current_index = 0;
    if (Process32First(hSnap, &pe)) {
      do {
        if (pe.th32ParentProcessID <= 1) {
          if (current_index >= scroll_offset) {
            wattron(win, COLOR_PAIR(C_GHOST) | A_BOLD);
            mvwprintw(win, line++, 2, "%-10lu %-10lu %s", pe.th32ProcessID,
                      pe.th32ParentProcessID, pe.szExeFile);
            wattroff(win, COLOR_PAIR(C_GHOST) | A_BOLD);
          }
          current_index++;
        }
      } while (Process32Next(hSnap, &pe) && line < max_y - 8);
    }
    CloseHandle(hSnap);
  }
#else
  pid_t pids[1024];
  int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
  int line = 3;
  int num_pids = count / sizeof(pid_t);
  int current_index = 0;
  for (int i = 0; i < num_pids && line < max_y - 8; i++) {
    if (pids[i] == 0)
      continue;
    struct proc_bsdinfo proc_info;
    if (proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &proc_info,
                     sizeof(proc_info)) == sizeof(proc_info)) {
      if (proc_info.pbi_ppid == 1 &&
          strcmp(proc_info.pbi_name, "launchd") != 0 &&
          strcmp(proc_info.pbi_name, "kernel_task") != 0 &&
          strcmp(proc_info.pbi_name, "idle") != 0) {
        if (current_index >= scroll_offset) {
          wattron(win, COLOR_PAIR(C_GHOST) | A_BOLD);
          mvwprintw(win, line++, 2, "%-10d %-10d %s", pids[i],
                    proc_info.pbi_ppid, proc_info.pbi_name);
          wattroff(win, COLOR_PAIR(C_GHOST) | A_BOLD);
        }
        current_index++;
      }
    }
  }
#endif
}

// Global live demo counters
int unsafe_counter = 0;
int safe_counter = 0;
#ifdef _WIN32
CRITICAL_SECTION cs;
HANDLE ts[2];
DWORD WINAPI race_t(LPVOID x) {
  while (is_running) {
    int t = unsafe_counter;
    Sleep(1);
    unsafe_counter = t + 1;
  }
  return 0;
}
DWORD WINAPI safe_t(LPVOID x) {
  while (is_running) {
    EnterCriticalSection(&cs);
    int t = safe_counter;
    Sleep(1);
    safe_counter = t + 1;
    LeaveCriticalSection(&cs);
  }
  return 0;
}
#else
pthread_mutex_t mtx;
pthread_t ts[2];
void *race_t(void *x) {
  (void)x;
  while (is_running) {
    int t = unsafe_counter;
    usleep(1000);
    unsafe_counter = t + 1;
  }
  return NULL;
}
void *safe_t(void *x) {
  (void)x;
  while (is_running) {
    pthread_mutex_lock(&mtx);
    int t = safe_counter;
    usleep(1000);
    safe_counter = t + 1;
    pthread_mutex_unlock(&mtx);
  }
  return NULL;
}
#endif

bool sync_initialized = false;

void init_sync_demo() {
  if (sync_initialized)
    return;
  unsafe_counter = 0;
  safe_counter = 0;
#ifdef _WIN32
  InitializeCriticalSection(&cs);
  ts[0] = CreateThread(NULL, 0, race_t, NULL, 0, NULL);
  ts[1] = CreateThread(NULL, 0, safe_t, NULL, 0, NULL);
#else
  pthread_mutex_init(&mtx, NULL);
  pthread_create(&ts[0], NULL, race_t, NULL);
  pthread_create(&ts[1], NULL, safe_t, NULL);
#endif
  sync_initialized = true;
}

void do_sync(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "--- Live Sync Demo ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  init_sync_demo();

  mvwprintw(win, 3, 2, "Unsafe Counter (Race Cond) : ");
  wattron(win, COLOR_PAIR(C_STRESS) | A_BOLD);
  wprintw(win, "%d", unsafe_counter);
  wattroff(win, COLOR_PAIR(C_STRESS) | A_BOLD);

  mvwprintw(win, 4, 2, "Mutex Safe Counter         : ");
  wattron(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);
  wprintw(win, "%d", safe_counter);
  wattroff(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);
}

void cleanup_sync() {
  if (!sync_initialized)
    return;
  is_running = false;
#ifdef _WIN32
  WaitForMultipleObjects(2, ts, TRUE, INFINITE);
  DeleteCriticalSection(&cs);
#else
  pthread_join(ts[0], NULL);
  pthread_join(ts[1], NULL);
  pthread_mutex_destroy(&mtx);
#endif
}

void main_event_loop() {
  while (is_running) {
    for (int i = 0; i < 49; i++) {
      cpu_history[i] = cpu_history[i + 1];
      mem_history[i] = mem_history[i + 1];
    }
    double cpu_now = get_cpu_load();
    double totMem = 0, availMem = 0;
    get_mem_stats(&totMem, &availMem);
    double memPct = (totMem > 0) ? ((totMem - availMem) / totMem) * 100.0 : 0.0;
    cpu_history[49] = (int)cpu_now;
    mem_history[49] = (int)memPct;

    draw_header();
    draw_footer();

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
    }

    box(main_win, 0, 0);
    wnoutrefresh(main_win);
    doupdate();

    int ch = getch();
    if (ch != ERR) {
      if (ch == KEY_UP) {
        if (scroll_offset > 0)
          scroll_offset--;
      } else if (ch == KEY_DOWN) {
        scroll_offset++;
      } else {
        ch = tolower(ch);
        if (ch == 'q')
          is_running = false;
        else if (strchr("pmdicgs", ch)) {
          if (current_mode != ch) {
            current_mode = ch;
            scroll_offset = 0;
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
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  timeout(1000); // refresh every 1s implicitly if no input

  if (has_colors()) {
    start_color();
    init_pair(C_HEALTHY, COLOR_GREEN, COLOR_BLACK);
    init_pair(C_STRESS, COLOR_RED, COLOR_BLACK);
    init_pair(C_HEADER, COLOR_CYAN, COLOR_BLACK);
    init_pair(C_NORMAL, COLOR_WHITE, COLOR_BLACK);
    init_pair(C_GHOST, COLOR_RED, COLOR_BLACK);
  }

  getmaxyx(stdscr, max_y, max_x);
  hdr_win = newwin(4, max_x, 0, 0);
  main_win = newwin(max_y - 7, max_x, 4, 0);
  ftr_win = newwin(3, max_x, max_y - 3, 0);

  main_event_loop();

  cleanup_sync();

  delwin(hdr_win);
  delwin(main_win);
  delwin(ftr_win);
  endwin();

  return 0;
}
