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
#include <time.h>

#ifdef _WIN32
#include <curses.h> // requires pdcurses
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <wbemidl.h>
#include <windows.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#else
#include <CoreFoundation/CoreFoundation.h>
#include <curses.h>
#include <errno.h>
#include <libproc.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <signal.h>
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
#define C_WARNING 6

// Global UI Layout variables
WINDOW *hdr_win, *main_win, *ftr_win;
int max_y, max_x;
int current_mode = 'p';
bool is_running = true;
int scroll_offset = 0;

// Footer status message (shown after signal send)
char ftr_status_msg[128] = "";
int ftr_status_color = 0; // C_HEALTHY or C_STRESS

int cpu_history[50] = {0};
int mem_history[50] = {0};
int temp_history[50] = {0};

/* SMA Buffer for Smoothing CPU Load */
double cpu_buffer[5] = {0.0};
int cpu_idx = 0;

#ifdef _WIN32
static double get_temperature() {
  double tempC = -1.0;
  HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
  if (FAILED(hres))
    return -1.0;

  hres =
      CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                           RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);

  IWbemLocator *pLoc = NULL;
  hres = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          &IID_IWbemLocator, (LPVOID *)&pLoc);
  if (FAILED(hres)) {
    CoUninitialize();
    return -1.0;
  }

  IWbemServices *pSvc = NULL;
  BSTR resource = SysAllocString(L"ROOT\\WMI");
  hres = pLoc->lpVtbl->ConnectServer(pLoc, resource, NULL, NULL, 0, 0, 0, 0,
                                     &pSvc);
  SysFreeString(resource);

  if (SUCCEEDED(hres)) {
    hres = CoSetProxyBlanket((IUnknown *)pSvc, RPC_C_AUTHN_WINNT,
                             RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                             RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (SUCCEEDED(hres)) {
      IEnumWbemClassObject *pEnumerator = NULL;
      BSTR lang = SysAllocString(L"WQL");
      BSTR query = SysAllocString(
          L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
      hres = pSvc->lpVtbl->ExecQuery(pSvc, lang, query,
                                     WBEM_FLAG_FORWARD_ONLY |
                                         WBEM_FLAG_RETURN_IMMEDIATELY,
                                     NULL, &pEnumerator);
      SysFreeString(lang);
      SysFreeString(query);

      if (SUCCEEDED(hres)) {
        IWbemClassObject *pclsObj = NULL;
        ULONG uReturn = 0;
        while (pEnumerator) {
          hres = pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1,
                                           &pclsObj, &uReturn);
          if (0 == uReturn || FAILED(hres))
            break;

          VARIANT vtProp;
          VariantInit(&vtProp);
          hres = pclsObj->lpVtbl->Get(pclsObj, L"CurrentTemperature", 0,
                                      &vtProp, 0, 0);
          if (SUCCEEDED(hres)) {
            long kelvinDeci = vtProp.lVal;
            tempC = (kelvinDeci - 2732.0) / 10.0;
            VariantClear(&vtProp);
            pclsObj->lpVtbl->Release(pclsObj);
            break;
          }
          pclsObj->lpVtbl->Release(pclsObj);
        }
        pEnumerator->lpVtbl->Release(pEnumerator);
      }
    }
    pSvc->lpVtbl->Release(pSvc);
  }
  pLoc->lpVtbl->Release(pLoc);
  CoUninitialize();
  return tempC;
}
#else
/*
 * NSProcessInfoThermalState constants (from Foundation):
 *   NSProcessInfoThermalStateNominal  = 0
 *   NSProcessInfoThermalStateFair     = 1
 *   NSProcessInfoThermalStateSerious  = 2
 *   NSProcessInfoThermalStateCritical = 3
 *
 * We call into NSProcessInfo via the Obj-C runtime so that main.c
 * remains a plain .c file (no .m extension required).
 */
typedef long NSInteger;
typedef NSInteger NSProcessInfoThermalState;

#define THERMAL_NOMINAL 0
#define THERMAL_FAIR 1
#define THERMAL_SERIOUS 2
#define THERMAL_CRITICAL 3

/* Returns the current thermal state (0-3), or -1 on failure. */
static NSProcessInfoThermalState get_thermal_state(void) {
  /* id pi = [NSProcessInfo processInfo]; */
  Class NSProcessInfoClass = objc_getClass("NSProcessInfo");
  if (!NSProcessInfoClass)
    return -1;

  SEL procesInfoSel = sel_registerName("processInfo");
  id pi = ((id (*)(Class, SEL))objc_msgSend)(NSProcessInfoClass, procesInfoSel);
  if (!pi)
    return -1;

  /* NSProcessInfoThermalState state = [pi thermalState]; */
  SEL thermalStateSel = sel_registerName("thermalState");
  NSProcessInfoThermalState state =
      ((NSProcessInfoThermalState (*)(id, SEL))objc_msgSend)(pi,
                                                             thermalStateSel);
  return state;
}

/* Maps thermal state to an integer percentage for the sparkline history.
 * Nominal=30, Fair=60, Serious=80, Critical=97 */
static double get_temperature(void) {
  NSProcessInfoThermalState s = get_thermal_state();
  switch (s) {
  case THERMAL_NOMINAL:
    return 30.0;
  case THERMAL_FAIR:
    return 60.0;
  case THERMAL_SERIOUS:
    return 80.0;
  case THERMAL_CRITICAL:
    return 97.0;
  default:
    return -1.0;
  }
}
#endif

// Mock or calculated system stats for the header
double get_cpu_load() {
#ifdef _WIN32
  // Windows simplified mock
  return 15.0;
#else
  // Realistic macOS-style mock: 12% idle load with background spikes
  double raw = (rand() % 12) + (rand() % 8 == 0 ? rand() % 30 : 0);

  /* Circular Buffer logic for SMA */
  cpu_buffer[cpu_idx] = raw;
  cpu_idx = (cpu_idx + 1) % 5;

  /* Return simple moving average of last 5 samples */
  double sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += cpu_buffer[i];
  }
  return sum / 5.0;
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
  mvwprintw(hdr_win, 1, 2, "=== Zenith-OS Diagnostics ===");
  wattroff(hdr_win, COLOR_PAIR(C_HEADER) | A_BOLD);

  double cpu = get_cpu_load();
  draw_bar(hdr_win, 2, 2, "CPU", cpu, 20);

  double totMem = 0, availMem = 0;
  get_mem_stats(&totMem, &availMem);
  double memPct = (totMem > 0) ? ((totMem - availMem) / totMem) * 100.0 : 0.0;
  draw_bar(hdr_win, 3, 2, "MEM", memPct, 20);

  box(hdr_win, 0, 0);
  /* wnoutrefresh deferred to main_event_loop for strict single-pass refresh */
}

void draw_footer() {
  werase(ftr_win);
  /* Print shortcuts on line 2 so the top border of the box cannot clip them */
  wattron(ftr_win, COLOR_PAIR(C_HEADER));
  mvwprintw(
      ftr_win, 2, 2,
      " Shortcuts: (P)roc | (M)em | (D)isk | (I)PC | (C)PU | (T)emp | (G)host "
      "Hunter | (S)ync Demo | (K)ill | (Q)uit ");
  wattroff(ftr_win, COLOR_PAIR(C_HEADER));

  // Show last signal result, if any
  if (ftr_status_msg[0] != '\0') {
    wattron(ftr_win, COLOR_PAIR(ftr_status_color) | A_BOLD);
    mvwprintw(ftr_win, 3, 2, " %s", ftr_status_msg);
    wattroff(ftr_win, COLOR_PAIR(ftr_status_color) | A_BOLD);
  }

  /* box and wnoutrefresh deferred to main_event_loop for strict single-pass
   * refresh */
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

void do_temp(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "--- Live Temperature Monitor ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

#ifdef _WIN32
  /* Windows: use the raw WMI temperature value */
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
  /* macOS: use NSProcessInfo Thermal State API (SMC/IOHID are sandboxed) */
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

  /* Line 3: Thermal state name — proves comm with macOS Power Kernel */
  mvwprintw(win, 3, 2, "Status              : ");
  wattron(win, color);
  wprintw(win, "%-10s", state_name);
  wattroff(win, color);

  /* Line 4: Approximate temperature range */
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

/* ====================================================================
 * do_signals – send a signal to a process by PID
 *
 * Input is taken from ftr_win so the main_win stays intact. ESC (27)
 * or an empty string cancels immediately without spinning the CPU.
 * ==================================================================== */
void do_signals(WINDOW *win) {
  (void)win; /* main_win is left untouched; we use ftr_win for prompts */

  bool is_canceled = false;
  char buf[32] = "";
  pid_t pid = 0;
  int sig_num = 0;

  /* ---- Prepare ftr_win for input ---- */
  werase(ftr_win);
  box(ftr_win, 0, 0);
  wattron(ftr_win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(ftr_win, 1, 2, " Kill Process  -  Enter PID (ESC to cancel): ");
  wattroff(ftr_win, COLOR_PAIR(C_HEADER) | A_BOLD);
  wnoutrefresh(ftr_win);
  doupdate();

  /* Switch to blocking, visible input */
  nodelay(stdscr, FALSE);
  echo();
  curs_set(1);

  /* --- Step 1: collect PID via ftr_win --- */
  wgetnstr(ftr_win, buf, (int)(sizeof(buf) - 1));

  /* ESC or empty → cancel */
  if (buf[0] == 27 || buf[0] == '\0') {
    is_canceled = true;
    goto cleanup;
  }

  pid = (pid_t)atoi(buf);

  /* Block killing ourselves or PID ≤ 1 */
  if (pid <= 1 || pid == getpid()) {
    snprintf(ftr_status_msg, sizeof(ftr_status_msg),
             "[SIGNAL] Blocked: PID %d is protected.", (int)pid);
    ftr_status_color = C_STRESS;
    goto cleanup;
  }

  /* --- Step 2: collect signal number --- */
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

  /* Parse signal */
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

  /* --- Step 3: send the signal --- */
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
    flushinp(); /* discard any stale keystrokes */
  }

  /* Restore non-blocking, silent mode so the main loop can 'rest' */
  curs_set(0);
  noecho();
  nodelay(stdscr, TRUE);
  timeout(1000);

  /* Repaint the footer so the box + shortcuts come back immediately */
  werase(ftr_win);
  draw_footer();
  box(ftr_win, 0, 0);
  wnoutrefresh(ftr_win);
  doupdate();
}

// ---- Fixed-Iterative Sync Demo ----
#define SYNC_ITERS 10000

int unsafe_counter = 0;
int safe_counter = 0;

// Sync test state machine
typedef enum {
  SYNC_IDLE = 0,    // waiting for 's' to start
  SYNC_RUNNING = 1, // threads in flight
  SYNC_DONE = 2     // results ready
} SyncState;

SyncState sync_state = SYNC_IDLE;
bool sync_initialized = false;

#ifdef _WIN32
CRITICAL_SECTION cs;
HANDLE ts[4];

DWORD WINAPI race_t(LPVOID x) {
  (void)x;
  for (int i = 0; i < SYNC_ITERS; i++) {
    int t = unsafe_counter;
    Sleep(0); /* yield to provoke scheduling interleave */
    unsafe_counter = t + 1;
  }
  return 0;
}
DWORD WINAPI safe_t(LPVOID x) {
  (void)x;
  for (int i = 0; i < SYNC_ITERS; i++) {
    EnterCriticalSection(&cs);
    safe_counter++;
    LeaveCriticalSection(&cs);
  }
  return 0;
}
#else
pthread_mutex_t mtx;
pthread_t ts[4];

void *race_t(void *x) {
  (void)x;
  for (int i = 0; i < SYNC_ITERS; i++) {
    int t = unsafe_counter;
    usleep(1); /* tiny delay forces the OS to interleave threads */
    unsafe_counter = t + 1;
  }
  return NULL;
}
void *safe_t(void *x) {
  (void)x;
  for (int i = 0; i < SYNC_ITERS; i++) {
    pthread_mutex_lock(&mtx);
    safe_counter++;
    pthread_mutex_unlock(&mtx);
  }
  return NULL;
}
#endif

/* Shared finished-thread counter (incremented by each thread on exit) */
static volatile int sync_done_count = 0;

/* Wrapper thread functions that bump sync_done_count on exit */
#ifdef _WIN32
DWORD WINAPI race_wrapper(LPVOID x) {
  race_t(x);
  InterlockedIncrement((LONG *)&sync_done_count);
  return 0;
}
DWORD WINAPI safe_wrapper(LPVOID x) {
  safe_t(x);
  InterlockedIncrement((LONG *)&sync_done_count);
  return 0;
}
#else
void *race_wrapper(void *x) {
  race_t(x);
  __sync_fetch_and_add(&sync_done_count, 1);
  return NULL;
}
void *safe_wrapper(void *x) {
  safe_t(x);
  __sync_fetch_and_add(&sync_done_count, 1);
  return NULL;
}
#endif

/* Revised launcher that uses the wrapper functions */
static void start_sync_test_real() {
  unsafe_counter = 0;
  safe_counter = 0;
  sync_done_count = 0;
#ifdef _WIN32
  InitializeCriticalSection(&cs);
  ts[0] = CreateThread(NULL, 0, race_wrapper, NULL, 0, NULL);
  ts[1] = CreateThread(NULL, 0, race_wrapper, NULL, 0, NULL);
  ts[2] = CreateThread(NULL, 0, safe_wrapper, NULL, 0, NULL);
  ts[3] = CreateThread(NULL, 0, safe_wrapper, NULL, 0, NULL);
#else
  pthread_mutex_init(&mtx, NULL);
  pthread_create(&ts[0], NULL, race_wrapper, NULL);
  pthread_create(&ts[1], NULL, race_wrapper, NULL);
  pthread_create(&ts[2], NULL, safe_wrapper, NULL);
  pthread_create(&ts[3], NULL, safe_wrapper, NULL);
#endif
  sync_initialized = true;
  sync_state = SYNC_RUNNING;
}

void do_sync(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(
      win, 1, 2,
      "--- Fixed-Iterative Sync Demo (2x10,000 increments per counter) ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

  if (sync_state == SYNC_IDLE) {
    /* ---- Idle: show instructions ---- */
    mvwprintw(win, 3, 2,
              "Each counter will be incremented 10,000 times by 2 threads.");
    mvwprintw(
        win, 4, 2,
        "Unsafe threads use a read-delay-write to provoke a race condition.");
    mvwprintw(win, 5, 2, "Safe   threads use a mutex to serialize access.");
    mvwprintw(win, 7, 2, "Press [ s ] to start the test.");
    return;
  }

  if (sync_state == SYNC_RUNNING) {
    /* ---- Check whether all 4 threads have exited ---- */
    if (sync_done_count >= 4) {
      /* Collect threads (non-blocking at this point) */
#ifdef _WIN32
      WaitForMultipleObjects(4, ts, TRUE, INFINITE);
      for (int i = 0; i < 4; i++)
        CloseHandle(ts[i]);
      DeleteCriticalSection(&cs);
#else
      for (int i = 0; i < 4; i++)
        pthread_join(ts[i], NULL);
      pthread_mutex_destroy(&mtx);
#endif
      sync_state = SYNC_DONE;
      sync_initialized = false; /* allow a fresh run next time */
    }
  }

  if (sync_state == SYNC_RUNNING) {
    /* ---- Live counters ---- */
    mvwprintw(win, 3, 2, "Test in progress ... (%d / 4 threads done)",
              sync_done_count);

    mvwprintw(win, 5, 2, "Unsafe Counter (Race) : ");
    wattron(win, COLOR_PAIR(C_STRESS) | A_BOLD);
    wprintw(win, "%-8d", unsafe_counter);
    wattroff(win, COLOR_PAIR(C_STRESS) | A_BOLD);

    mvwprintw(win, 6, 2, "Safe   Counter (Mutex): ");
    wattron(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);
    wprintw(win, "%-8d", safe_counter);
    wattroff(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);
    return;
  }

  /* ---- Results ---- */
  const int expected = SYNC_ITERS * 2;
  const int lost = expected - unsafe_counter;

  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 3, 2, "--- Test Complete ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

  mvwprintw(win, 5, 2, "Expected               : ");
  wattron(win, COLOR_PAIR(C_NORMAL) | A_BOLD);
  wprintw(win, "%d", expected);
  wattroff(win, COLOR_PAIR(C_NORMAL) | A_BOLD);

  mvwprintw(win, 6, 2, "Actual (Safe  / Mutex) : ");
  wattron(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);
  wprintw(win, "%d", safe_counter);
  wattroff(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);

  mvwprintw(win, 7, 2, "Actual (Unsafe / Race) : ");
  wattron(win, COLOR_PAIR(C_STRESS) | A_BOLD);
  wprintw(win, "%d", unsafe_counter);
  wattroff(win, COLOR_PAIR(C_STRESS) | A_BOLD);

  mvwprintw(win, 9, 2, "Conclusion : ");
  if (lost > 0) {
    wattron(win, COLOR_PAIR(C_STRESS) | A_BOLD);
    wprintw(win, "Race Condition Detected: Lost %d increments.", lost);
    wattroff(win, COLOR_PAIR(C_STRESS) | A_BOLD);
  } else {
    wattron(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);
    wprintw(win, "No lost increments (race not triggered this run).");
    wattroff(win, COLOR_PAIR(C_HEALTHY) | A_BOLD);
  }

  mvwprintw(win, 11, 2, "Press [ s ] to run again.");
}

void cleanup_sync() {
  /* Only needed if the app quits while threads are still running */
  if (!sync_initialized || sync_state != SYNC_RUNNING)
    return;
#ifdef _WIN32
  WaitForMultipleObjects(4, ts, TRUE, INFINITE);
  for (int i = 0; i < 4; i++)
    CloseHandle(ts[i]);
  DeleteCriticalSection(&cs);
#else
  for (int i = 0; i < 4; i++)
    pthread_join(ts[i], NULL);
  pthread_mutex_destroy(&mtx);
#endif
}

void main_event_loop() {
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

    /* --- Draw all panes (no individual wrefresh/wnoutrefresh inside) --- */
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
    }
    box(main_win, 0, 0);

    draw_footer();
    /* draw_footer leaves box/wnoutrefresh to us so the footer is always
       rendered last with a fresh box border */
    box(ftr_win, 0, 0);

    /* --- Strict single-pass refresh: header → main → footer → flush --- */
    wnoutrefresh(hdr_win);
    wnoutrefresh(main_win);
    wnoutrefresh(ftr_win);
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
        if (ch == 'q') {
          is_running = false;
        } else if (ch == 'k') {
          // Kill/signal mode: enter blocking input, then return to proc list
          do_signals(main_win);
        } else if (strchr("pmdicgst", ch)) {
          if (ch == 's') {
            if (current_mode != 's') {
              /* First press: switch to sync demo view (IDLE state) */
              current_mode = 's';
              scroll_offset = 0;
              sync_state = SYNC_IDLE;
            } else if (sync_state == SYNC_IDLE || sync_state == SYNC_DONE) {
              /* Second press (or re-run): launch the test */
              start_sync_test_real();
            }
          } else {
            if (current_mode != ch) {
              current_mode = ch;
              scroll_offset = 0;
            }
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
  srand(time(NULL));

  /* Pre-fill CPU buffer so the UI starts with a steady average */
  for (int i = 0; i < 5; i++) {
    get_cpu_load();
  }
  set_escdelay(25); /* make ESC responsive without the default 1 s hang */
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  timeout(1000); /* refresh every 1 s; do_signals restores this after input */

  if (has_colors()) {
    start_color();
    init_pair(C_HEALTHY, COLOR_GREEN, COLOR_BLACK);
    init_pair(C_STRESS, COLOR_RED, COLOR_BLACK);
    init_pair(C_HEADER, COLOR_CYAN, COLOR_BLACK);
    init_pair(C_NORMAL, COLOR_WHITE, COLOR_BLACK);
    init_pair(C_GHOST, COLOR_RED, COLOR_BLACK);
    init_pair(C_WARNING, COLOR_YELLOW, COLOR_BLACK);
  }

  /* Force a complete repaint of the virtual screen on the first doupdate() */
  clearok(stdscr, TRUE);

  /* --- Window layout constants ---
   *   header_h = 5   (title + 2 bars + box borders)
   *   footer_h = 7   (~2.5x original – large safety buffer for visibility)
   *   dead_zone= 2   extra lines so main_win never bleeds into footer row
   *   main_h   = max_y - header_h - footer_h - dead_zone
   */
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
