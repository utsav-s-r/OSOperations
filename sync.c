#include "sync.h"
#include "ui.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define SYNC_ITERS 10000

static int unsafe_counter = 0;
static int safe_counter = 0;

typedef enum {
  SYNC_IDLE = 0,
  SYNC_RUNNING = 1,
  SYNC_DONE = 2
} SyncState;

static SyncState sync_state = SYNC_IDLE;
static bool sync_initialized = false;
static volatile int sync_done_count = 0;

#ifdef _WIN32
static CRITICAL_SECTION cs;
static HANDLE ts[4];

DWORD WINAPI race_t(LPVOID x) {
  (void)x;
  for (int i = 0; i < SYNC_ITERS; i++) {
    int t = unsafe_counter;
    Sleep(0);
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
static pthread_mutex_t mtx;
pthread_t ts[4];

void *race_t(void *x) {
  (void)x;
  for (int i = 0; i < SYNC_ITERS; i++) {
    int t = unsafe_counter;
    usleep(1);
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

bool sync_can_start(void) {
  return sync_state == SYNC_IDLE || sync_state == SYNC_DONE;
}

void start_sync_test(void) {
  if (!sync_can_start()) {
    return;
  }

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
    mvwprintw(win, 3, 2,
              "Each counter will be incremented 10,000 times by 2 threads.");
    mvwprintw(win, 4, 2,
              "Unsafe threads use a read-delay-write to provoke a race condition.");
    mvwprintw(win, 5, 2, "Safe   threads use a mutex to serialize access.");
    mvwprintw(win, 7, 2, "Press [ s ] to start the test.");
    return;
  }

  if (sync_state == SYNC_RUNNING) {
    if (sync_done_count >= 4) {
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
      sync_initialized = false;
    }
  }

  if (sync_state == SYNC_RUNNING) {
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

void cleanup_sync(void) {
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
