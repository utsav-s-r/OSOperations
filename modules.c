#include "modules.h"
#include "ui.h"
#include "os_stats.h"

#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <libproc.h>
#else
#include <process.h>
#endif

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
          break;
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
    const char *mnt = mntbufp[i].f_mntonname;
    const char *fst = mntbufp[i].f_fstypename;
    if (total == 0 || strcmp(fst, "devfs") == 0 || strcmp(mnt, "/dev") == 0) {
      continue;
    }
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

void do_temp(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "--- Live Temperature Monitor ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

#ifdef _WIN32
  double temp = get_temperature();
  if (temp < 0.0) {
    mvwprintw(win, 3, 2, "Current Temperature : ");
    wattron(win, COLOR_PAIR(C_STRESS) | A_BOLD);
    wprintw(win, "N/A (Sensor Blocked)");
    wattroff(win, COLOR_PAIR(C_STRESS) | A_BOLD);
  } else {
    int color = COLOR_PAIR(C_HEALTHY);
    if (temp >= 80.0)
      color = COLOR_PAIR(C_STRESS) | A_BOLD;
    else if (temp >= 60.0)
      color = COLOR_PAIR(C_WARNING) | A_BOLD;
    mvwprintw(win, 3, 2, "Current Temperature : ");
    wattron(win, color);
    wprintw(win, "%.1f C", temp);
    wattroff(win, color);
  }
#else
  NSProcessInfoThermalState state = get_thermal_state();

  const char *state_name;
  const char *temp_range;
  int color;

  switch (state) {
  case THERMAL_NOMINAL:
    state_name = "NOMINAL";
    temp_range = "35-45°C";
    color = COLOR_PAIR(C_HEALTHY) | A_BOLD;
    break;
  case THERMAL_FAIR:
    state_name = "FAIR";
    temp_range = "55-65°C";
    color = COLOR_PAIR(C_WARNING) | A_BOLD;
    break;
  case THERMAL_SERIOUS:
    state_name = "SERIOUS";
    temp_range = "75-85°C";
    color = COLOR_PAIR(C_STRESS) | A_BOLD;
    break;
  case THERMAL_CRITICAL:
    state_name = "CRITICAL";
    temp_range = "95°C+ (Throttling)";
    color = COLOR_PAIR(C_STRESS) | A_BOLD;
    break;
  default:
    state_name = "UNKNOWN";
    temp_range = "N/A";
    color = COLOR_PAIR(C_STRESS) | A_BOLD;
    break;
  }

  mvwprintw(win, 3, 2, "Status              : ");
  wattron(win, color);
  wprintw(win, "%-10s", state_name);
  wattroff(win, color);
  wclrtoeol(win);

  mvwprintw(win, 4, 2, "Approx Temperature  : ");
  if (state == THERMAL_CRITICAL) {
    wattron(win, COLOR_PAIR(C_STRESS) | A_BOLD);
    wprintw(win, "%s", temp_range);
    wattroff(win, COLOR_PAIR(C_STRESS) | A_BOLD);
  } else {
    wattron(win, color);
    wprintw(win, "%s", temp_range);
    wattroff(win, color);
  }
  wclrtoeol(win);

  mvwprintw(win, 5, 2,
            "Source              : macOS NSProcessInfo Thermal State API");
#endif

  mvwprintw(win, 7, 2, "Thermal History (50s):");
  draw_graph(win, 8, 2, temp_history, 50);
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

void do_signals(WINDOW *win) {
  (void)win;

  bool is_canceled = false;
  char buf[32] = "";
  pid_t pid = 0;
  int sig_num = 0;

  werase(ftr_win);
  box(ftr_win, 0, 0);
  wattron(ftr_win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(ftr_win, 1, 2, " Kill Process  -  Enter PID (ESC to cancel): ");
  wattroff(ftr_win, COLOR_PAIR(C_HEADER) | A_BOLD);
  wnoutrefresh(ftr_win);
  doupdate();

  nodelay(stdscr, FALSE);
  echo();
  curs_set(1);

  wgetnstr(ftr_win, buf, (int)(sizeof(buf) - 1));

  if (buf[0] == 27 || buf[0] == '\0') {
    is_canceled = true;
    goto cleanup;
  }

  pid = (pid_t)atoi(buf);

  if (pid <= 1 || pid == getpid()) {
    snprintf(ftr_status_msg, sizeof(ftr_status_msg),
             "[SIGNAL] Blocked: PID %d is protected.", (int)pid);
    ftr_status_color = C_STRESS;
    goto cleanup;
  }

  werase(ftr_win);
  box(ftr_win, 0, 0);
  wattron(ftr_win, COLOR_PAIR(C_WARNING) | A_BOLD);
  mvwprintw(
      ftr_win, 1, 2,
      " Signals: k/K=SIGTERM(15)  9=SIGKILL  or enter number  (ESC=cancel)");
  wattroff(ftr_win, COLOR_PAIR(C_WARNING) | A_BOLD);
  mvwprintw(ftr_win, 2, 2, " Signal for PID %d: ", (int)pid);
  wnoutrefresh(ftr_win);
  doupdate();

  buf[0] = '\0';
  wgetnstr(ftr_win, buf, (int)(sizeof(buf) - 1));

  if (buf[0] == 27 || buf[0] == '\0') {
    is_canceled = true;
    goto cleanup;
  }

  if (buf[0] == 'k' || buf[0] == 'K') {
    sig_num = SIGTERM;
  } else if (strcmp(buf, "9") == 0) {
    sig_num = SIGKILL;
  } else {
    sig_num = atoi(buf);
    if (sig_num <= 0) {
      snprintf(ftr_status_msg, sizeof(ftr_status_msg),
               "[SIGNAL] Invalid signal: '%s'", buf);
      ftr_status_color = C_STRESS;
      goto cleanup;
    }
  }

#ifdef _WIN32
  {
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (hProc == NULL) {
      snprintf(ftr_status_msg, sizeof(ftr_status_msg),
               "[SIGNAL] OpenProcess failed for PID %d (err %lu)", (int)pid,
               GetLastError());
      ftr_status_color = C_STRESS;
      goto cleanup;
    }
    BOOL ok = TerminateProcess(hProc, (UINT)sig_num);
    CloseHandle(hProc);
    if (ok) {
      snprintf(ftr_status_msg, sizeof(ftr_status_msg),
               "[SIGNAL] OK – Terminated PID %d (signal %d)", (int)pid,
               sig_num);
      ftr_status_color = C_HEALTHY;
    } else {
      snprintf(ftr_status_msg, sizeof(ftr_status_msg),
               "[SIGNAL] FAIL – TerminateProcess PID %d (err %lu)", (int)pid,
               GetLastError());
      ftr_status_color = C_STRESS;
    }
  }
#else
  {
    int ret = kill(pid, sig_num);
    if (ret == 0) {
      snprintf(ftr_status_msg, sizeof(ftr_status_msg),
               "[SIGNAL] OK – Sent signal %d to PID %d", sig_num, (int)pid);
      ftr_status_color = C_HEALTHY;
      current_mode = 'p';
      scroll_offset = 0;
    } else {
      snprintf(ftr_status_msg, sizeof(ftr_status_msg),
               "[SIGNAL] FAIL – kill(%d, %d): %s", (int)pid, sig_num,
               strerror(errno));
      ftr_status_color = C_STRESS;
    }
  }
#endif

cleanup:
  if (is_canceled) {
    flushinp();
  }

  curs_set(0);
  noecho();
  nodelay(stdscr, TRUE);
  timeout(1000);

  werase(ftr_win);
  draw_footer();
  box(ftr_win, 0, 0);
  wnoutrefresh(ftr_win);
  doupdate();
}
