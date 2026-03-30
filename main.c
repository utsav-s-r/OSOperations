#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// Windows includes
#include <tlhelp32.h>
#include <windows.h>
#else
// macOS includes
#include <libproc.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <unistd.h>
#endif

void print_help(const char *progname) {
  printf("Usage: %s [options]\n", progname);
  printf("Options:\n");
  printf("  --proc    List processes (PID, PPID, Name)\n");
  printf("  --mem     Show memory info (Total vs Available Physical RAM)\n");
  printf("  --disk    List disk drives/mount points and free space %%\n");
  printf("  --sync    Demonstrate concurrency race condition and fix\n");
}

void do_proc() {
  printf("%-10s %-10s %s\n", "PID", "PPID", "NAME");
  printf("----------------------------------------\n");
#ifdef _WIN32
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE) {
    printf("Failed to create process snapshot.\n");
    return;
  }

  PROCESSENTRY32 pe32;
  pe32.dwSize = sizeof(PROCESSENTRY32);

  if (Process32First(hSnapshot, &pe32)) {
    do {
      printf("%-10lu %-10lu %s\n", pe32.th32ProcessID, pe32.th32ParentProcessID,
             pe32.szExeFile);
    } while (Process32Next(hSnapshot, &pe32));
  }
  CloseHandle(hSnapshot);
#else
  pid_t pids[1024];
  int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
  if (count <= 0) {
    printf("Failed to list processes.\n");
    return;
  }
  int num_pids = count / sizeof(pid_t);
  for (int i = 0; i < num_pids; i++) {
    if (pids[i] == 0)
      continue;
    struct proc_bsdinfo proc_info;
    int st = proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &proc_info,
                          sizeof(proc_info));
    if (st == sizeof(proc_info)) {
      printf("%-10d %-10d %s\n", pids[i], proc_info.pbi_ppid,
             proc_info.pbi_name);
    }
  }
#endif
}

void do_mem() {
#ifdef _WIN32
  MEMORYSTATUSEX memInfo;
  memInfo.dwLength = sizeof(MEMORYSTATUSEX);
  if (GlobalMemoryStatusEx(&memInfo)) {
    double totalMB = memInfo.ullTotalPhys / (1024.0 * 1024.0);
    double availMB = memInfo.ullAvailPhys / (1024.0 * 1024.0);
    printf("%-20s : %.2f MB\n", "Total Physical RAM", totalMB);
    printf("%-20s : %.2f MB\n", "Available RAM", availMB);
  } else {
    printf("Failed to get memory status.\n");
  }
#else
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vmstat;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
    uint64_t total_pages = vmstat.free_count + vmstat.active_count +
                           vmstat.inactive_count + vmstat.wire_count;
    uint64_t mem_size = total_pages * getpagesize();
    uint64_t avail_size =
        (vmstat.free_count + vmstat.inactive_count) * getpagesize();

    printf("%-20s : %.2f MB\n", "Total Physical RAM",
           mem_size / (1024.0 * 1024.0));
    printf("%-20s : %.2f MB\n", "Available RAM",
           avail_size / (1024.0 * 1024.0));
  } else {
    printf("Failed to get memory statistics.\n");
  }
#endif
}

void do_disk() {
  printf("%-20s %s\n", "Mount Point", "Free Space (%)");
  printf("----------------------------------------\n");
#ifdef _WIN32
  DWORD drives = GetLogicalDrives();
  for (int i = 0; i < 26; i++) {
    if (drives & (1 << i)) {
      char drivePath[] = {(char)('A' + i), ':', '\\', '\0'};
      ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes,
          totalNumberOfFreeBytes;
      if (GetDiskFreeSpaceExA(drivePath, &freeBytesAvailable,
                              &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        if (totalNumberOfBytes.QuadPart > 0) {
          double freePct = (double)freeBytesAvailable.QuadPart /
                           totalNumberOfBytes.QuadPart * 100.0;
          printf("%-20s %.2f%%\n", drivePath, freePct);
        }
      } else {
        printf("%-20s Access Denied\n", drivePath);
      }
    }
  }
#else
  struct statfs *mntbufp;
  int num_mounts = getmntinfo(&mntbufp, MNT_NOWAIT);
  if (num_mounts == 0) {
    printf("Failed to get mount info.\n");
    return;
  }
  for (int i = 0; i < num_mounts; i++) {
    uint64_t total = mntbufp[i].f_blocks;
    uint64_t free = mntbufp[i].f_bavail;
    if (total > 0) {
      double freePct = (double)free / total * 100.0;
      printf("%-20s %.2f%%\n", mntbufp[i].f_mntonname, freePct);
    }
  }
#endif
}

// Concurrency setup
#define NUM_THREADS 5
#define ITERATIONS 100000

int global_counter = 0;

#ifdef _WIN32
CRITICAL_SECTION cs;

DWORD WINAPI race_thread(LPVOID arg) {
  (void)arg;
  for (int i = 0; i < ITERATIONS; i++) {
    int temp = global_counter;
    Sleep(0); // force context switch
    temp = temp + 1;
    global_counter = temp;
  }
  return 0;
}

DWORD WINAPI safe_thread(LPVOID arg) {
  (void)arg;
  for (int i = 0; i < ITERATIONS; i++) {
    EnterCriticalSection(&cs);

    int temp = global_counter;
    Sleep(0);
    temp = temp + 1;
    global_counter = temp;

    LeaveCriticalSection(&cs);
  }
  return 0;
}
#else
#include <unistd.h>
pthread_mutex_t mutex;

void *race_thread(void *arg) {
  (void)arg; // suppress warning
  for (int i = 0; i < ITERATIONS; i++) {
    int temp = global_counter;
    usleep(1); // force context switch
    temp = temp + 1;
    global_counter = temp;
  }
  return NULL;
}

void *safe_thread(void *arg) {
  (void)arg; // suppress warning
  for (int i = 0; i < ITERATIONS; i++) {
    pthread_mutex_lock(&mutex);

    int temp = global_counter;
    usleep(1);
    temp = temp + 1;
    global_counter = temp;

    pthread_mutex_unlock(&mutex);
  }
  return NULL;
}
#endif

void do_sync_demo() {
  printf("Starting concurrency demo (%d threads, %d iterations each)...\n",
         NUM_THREADS, ITERATIONS);
  printf("Expected count: %d\n", NUM_THREADS * ITERATIONS);

  // 1. Race condition
  global_counter = 0;
#ifdef _WIN32
  HANDLE threads[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    threads[i] = CreateThread(NULL, 0, race_thread, NULL, 0, NULL);
  }
  WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
  for (int i = 0; i < NUM_THREADS; i++) {
    CloseHandle(threads[i]);
  }
#else
  pthread_t threads[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_create(&threads[i], NULL, race_thread, NULL);
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }
#endif
  printf("Without synchronization (Race Condition): %d\n", global_counter);

  // 2. Safe execution
  global_counter = 0;
#ifdef _WIN32
  InitializeCriticalSection(&cs);
  for (int i = 0; i < NUM_THREADS; i++) {
    threads[i] = CreateThread(NULL, 0, safe_thread, NULL, 0, NULL);
  }
  WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
  for (int i = 0; i < NUM_THREADS; i++) {
    CloseHandle(threads[i]);
  }
  DeleteCriticalSection(&cs);
#else
  pthread_mutex_init(&mutex, NULL);
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_create(&threads[i], NULL, safe_thread, NULL);
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }
  pthread_mutex_destroy(&mutex);
#endif
  printf("With synchronization (Mutex/CriticalSection): %d\n", global_counter);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_help(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "--proc") == 0) {
    do_proc();
  } else if (strcmp(argv[1], "--mem") == 0) {
    do_mem();
  } else if (strcmp(argv[1], "--disk") == 0) {
    do_disk();
  } else if (strcmp(argv[1], "--sync") == 0) {
    do_sync_demo();
  } else {
    printf("Unknown option: %s\n", argv[1]);
    print_help(argv[0]);
    return 1;
  }

  return 0;
}
