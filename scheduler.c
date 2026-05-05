#include "scheduler.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
  Process processes[MAX_PROCESSES];
  GanttBlock gantt[MAX_GANTT_BLOCKS];
  int gantt_count;
  int total_time;
} AlgorithmResult;

static int base_num_processes = 0;
static Process base_processes[MAX_PROCESSES];
static AlgorithmResult results[4];
static bool simulation_complete = false;

typedef enum {
  INPUT_IDLE = 0,
  INPUT_NUM_PROCS = 1,
  INPUT_PROCESS_AT = 2,
  INPUT_PROCESS_BT = 3,
  INPUT_PROCESS_PRIORITY = 4,
} InputState;

static InputState input_state = INPUT_IDLE;
static char input_buffer[32] = "";
static int processes_entered = 0;
static bool input_active = false;

static int compare_arrival(const void *a, const void *b) {
  Process *p1 = (Process *)a;
  Process *p2 = (Process *)b;
  return p1->arrival_time - p2->arrival_time;
}

static void compute_metrics(Process *processes, int count) {
  for (int i = 0; i < count; i++) {
    processes[i].turnaround_time =
        processes[i].completion_time - processes[i].arrival_time;
    processes[i].waiting_time =
        processes[i].turnaround_time - processes[i].burst_time;
  }
}

static void schedule_fcfs(int algo_idx) {
  results[algo_idx].gantt_count = 0;
  int current_time = 0;
  Process *procs = results[algo_idx].processes;
  int count = base_num_processes;

  memcpy(procs, base_processes, count * sizeof(Process));
  qsort(procs, count, sizeof(Process), compare_arrival);

  for (int i = 0; i < count; i++) {
    if (current_time < procs[i].arrival_time) {
      current_time = procs[i].arrival_time;
    }
    results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[i].pid;
    results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
    current_time += procs[i].burst_time;
    results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;
    procs[i].completion_time = current_time;
    results[algo_idx].gantt_count++;
  }
  results[algo_idx].total_time = current_time;
  compute_metrics(procs, count);
}

static void schedule_sjf(int algo_idx) {
  results[algo_idx].gantt_count = 0;
  int current_time = 0;
  Process *procs = results[algo_idx].processes;
  int count = base_num_processes;

  memcpy(procs, base_processes, count * sizeof(Process));
  bool completed[MAX_PROCESSES] = {false};

  while (1) {
    int next_proc = -1;
    int min_burst = 1000000;

    for (int i = 0; i < count; i++) {
      if (!completed[i] && procs[i].arrival_time <= current_time &&
          procs[i].burst_time < min_burst) {
        next_proc = i;
        min_burst = procs[i].burst_time;
      }
    }

    if (next_proc == -1) {
      bool all_done = true;
      for (int i = 0; i < count; i++) {
        if (!completed[i]) {
          all_done = false;
          current_time = procs[i].arrival_time;
          break;
        }
      }
      if (all_done)
        break;
      continue;
    }

    results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[next_proc].pid;
    results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
    current_time += procs[next_proc].burst_time;
    results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;
    procs[next_proc].completion_time = current_time;
    completed[next_proc] = true;
    results[algo_idx].gantt_count++;
  }

  results[algo_idx].total_time = current_time;
  compute_metrics(procs, count);
}

static void schedule_priority(int algo_idx) {
  results[algo_idx].gantt_count = 0;
  int current_time = 0;
  Process *procs = results[algo_idx].processes;
  int count = base_num_processes;

  memcpy(procs, base_processes, count * sizeof(Process));
  bool completed[MAX_PROCESSES] = {false};

  while (1) {
    int next_proc = -1;
    int max_priority = 1000000;

    for (int i = 0; i < count; i++) {
      if (!completed[i] && procs[i].arrival_time <= current_time &&
          procs[i].priority < max_priority) {
        next_proc = i;
        max_priority = procs[i].priority;
      }
    }

    if (next_proc == -1) {
      bool all_done = true;
      for (int i = 0; i < count; i++) {
        if (!completed[i]) {
          all_done = false;
          current_time = procs[i].arrival_time;
          break;
        }
      }
      if (all_done)
        break;
      continue;
    }

    results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[next_proc].pid;
    results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
    current_time += procs[next_proc].burst_time;
    results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;
    procs[next_proc].completion_time = current_time;
    completed[next_proc] = true;
    results[algo_idx].gantt_count++;
  }

  results[algo_idx].total_time = current_time;
  compute_metrics(procs, count);
}

static void schedule_round_robin(int algo_idx) {
  results[algo_idx].gantt_count = 0;
  int current_time = 0;
  Process *procs = results[algo_idx].processes;
  int count = base_num_processes;
  int time_quantum = 2;

  memcpy(procs, base_processes, count * sizeof(Process));
  int remaining[MAX_PROCESSES];
  bool arrived[MAX_PROCESSES] = {false};
  int completed = 0;

  for (int i = 0; i < count; i++) {
    remaining[i] = procs[i].burst_time;
  }

  while (completed < count) {
    for (int i = 0; i < count; i++) {
      if (procs[i].arrival_time <= current_time && remaining[i] > 0) {
        if (!arrived[i]) {
          procs[i].response_time = current_time - procs[i].arrival_time;
          arrived[i] = true;
        }

        int exec_time =
            (remaining[i] > time_quantum) ? time_quantum : remaining[i];

        results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[i].pid;
        results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
        current_time += exec_time;
        results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;
        remaining[i] -= exec_time;

        if (remaining[i] == 0) {
          procs[i].completion_time = current_time;
          completed++;
        }

        results[algo_idx].gantt_count++;
      }
    }
    if (results[algo_idx].gantt_count >= MAX_GANTT_BLOCKS)
      break;
  }

  results[algo_idx].total_time = current_time;
  compute_metrics(procs, count);
}

static void run_all_schedules(void) {
  schedule_fcfs(0);
  schedule_sjf(1);
  schedule_priority(2);
  schedule_round_robin(3);
  simulation_complete = true;
  input_active = false;
}

void do_scheduler(WINDOW *win) {
  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 1, 2, "--- Process Scheduler Simulator ---");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

  if (!input_active && base_num_processes == 0) {
    mvwprintw(win, 3, 2, "Welcome to the Process Scheduler Simulator!");
    mvwprintw(win, 5, 2, "Displays all 4 algorithms: FCFS, SJF, Priority, RR");
    mvwprintw(win, 7, 2, "Press [ e ] to start interactive setup.");
    return;
  }

  if (input_active && !simulation_complete) {
    if (input_state == INPUT_NUM_PROCS) {
      wattron(win, COLOR_PAIR(C_WARNING) | A_BOLD);
      mvwprintw(win, 3, 2, "Enter number of processes (1-20): ");
      wattroff(win, COLOR_PAIR(C_WARNING) | A_BOLD);
      mvwprintw(win, 4, 2, "Current input: %s_", input_buffer);
    } else if (input_state == INPUT_PROCESS_AT) {
      wattron(win, COLOR_PAIR(C_NORMAL));
      mvwprintw(win, 3, 2, "Process %d / %d", processes_entered + 1,
                base_num_processes);
      mvwprintw(win, 4, 2, "Enter Arrival Time: ");
      wattroff(win, COLOR_PAIR(C_NORMAL));
      mvwprintw(win, 5, 2, "Current input: %s_", input_buffer);
    } else if (input_state == INPUT_PROCESS_BT) {
      wattron(win, COLOR_PAIR(C_NORMAL));
      mvwprintw(win, 3, 2, "Process %d / %d", processes_entered + 1,
                base_num_processes);
      mvwprintw(win, 4, 2, "Enter Burst Time: ");
      wattroff(win, COLOR_PAIR(C_NORMAL));
      mvwprintw(win, 5, 2, "Current input: %s_", input_buffer);
    } else if (input_state == INPUT_PROCESS_PRIORITY) {
      wattron(win, COLOR_PAIR(C_NORMAL));
      mvwprintw(win, 3, 2, "Process %d / %d", processes_entered + 1,
                base_num_processes);
      mvwprintw(win, 4, 2, "Enter Priority (1=high, 10=low): ");
      wattroff(win, COLOR_PAIR(C_NORMAL));
      mvwprintw(win, 5, 2, "Current input: %s_", input_buffer);
    }
    return;
  }

  wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
  mvwprintw(win, 3, 2, "Scheduling Results - All Algorithms");
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

  const char *algo_names[] = {"FCFS", "SJF", "Priority", "Round Robin"};
  int base_y = 5;

  for (int algo = 0; algo < 4; algo++) {
    int y = base_y + algo * 6;
    if (y >= max_y - 5)
      break;

    wattron(win, COLOR_PAIR(C_HEADER));
    mvwprintw(win, y, 2, "%s (Total Time: %d)", algo_names[algo],
              results[algo].total_time);
    wattroff(win, COLOR_PAIR(C_HEADER));

    int gantt_y = y + 1;
    for (int i = 0; i < results[algo].gantt_count && gantt_y < y + 4; i++) {
      int pid = results[algo].gantt[i].pid;
      int start = results[algo].gantt[i].start_time;
      int end = results[algo].gantt[i].end_time;
      mvwprintw(win, gantt_y, 2, "  P%d [%d-%d]", pid, start, end);
      if (i > 0 && i % 2 == 1)
        gantt_y++;
    }

    int metrics_y = y + 4;
    wattron(win, COLOR_PAIR(C_NORMAL));
    mvwprintw(win, metrics_y, 2, "PID AT BT CT WT TAT RT");
    for (int i = 0; i < base_num_processes && metrics_y + i + 1 < y + 6; i++) {
      Process *p = &results[algo].processes[i];
      int color = (p->waiting_time < 5) ? C_HEALTHY : C_WARNING;
      wattron(win, COLOR_PAIR(color));
      mvwprintw(win, metrics_y + i + 1, 2, "P%d %d %d %d %d %d %d", p->pid,
                p->arrival_time, p->burst_time, p->completion_time,
                p->waiting_time, p->turnaround_time, p->response_time);
      wattroff(win, COLOR_PAIR(color));
    }
  }

  wattron(win, COLOR_PAIR(C_NORMAL) | A_BOLD);
  mvwprintw(win, max_y - 2, 2, "Press [ n ] for new schedule, [ p/m/d/i/c/g/s/t ] to switch");
  wattroff(win, COLOR_PAIR(C_NORMAL) | A_BOLD);
}

void scheduler_handle_input(char ch) {
  if (ch == 'e' && !input_active) {
    input_active = true;
    input_state = INPUT_NUM_PROCS;
    memset(input_buffer, 0, sizeof(input_buffer));
    processes_entered = 0;
    base_num_processes = 0;
    simulation_complete = false;
    return;
  }

  if (!input_active) {
    if (ch == 'n' && simulation_complete) {
      simulation_complete = false;
      base_num_processes = 0;
      processes_entered = 0;
      memset(base_processes, 0, sizeof(base_processes));
      memset(results, 0, sizeof(results));
    }
    return;
  }

  if (ch == 127 || ch == 8) {
    int len = strlen(input_buffer);
    if (len > 0) {
      input_buffer[len - 1] = '\0';
    }
    return;
  }

  if (ch == 27) {
    input_active = false;
    input_state = INPUT_IDLE;
    memset(input_buffer, 0, sizeof(input_buffer));
    base_num_processes = 0;
    processes_entered = 0;
    return;
  }

  if (ch == '\n' || ch == '\r') {
    if (input_buffer[0] == '\0')
      return;

    if (input_state == INPUT_NUM_PROCS) {
      int num = atoi(input_buffer);
      if (num > 0 && num <= MAX_PROCESSES) {
        base_num_processes = num;
        processes_entered = 0;
        input_state = INPUT_PROCESS_AT;
        memset(input_buffer, 0, sizeof(input_buffer));
      }
    } else if (input_state == INPUT_PROCESS_AT) {
      int at = atoi(input_buffer);
      base_processes[processes_entered].arrival_time = at;
      input_state = INPUT_PROCESS_BT;
      memset(input_buffer, 0, sizeof(input_buffer));
    } else if (input_state == INPUT_PROCESS_BT) {
      int bt = atoi(input_buffer);
      base_processes[processes_entered].burst_time = bt;
      base_processes[processes_entered].pid = processes_entered + 1;
      input_state = INPUT_PROCESS_PRIORITY;
      memset(input_buffer, 0, sizeof(input_buffer));
    } else if (input_state == INPUT_PROCESS_PRIORITY) {
      int pri = atoi(input_buffer);
      base_processes[processes_entered].priority = pri;
      base_processes[processes_entered].remaining_time =
          base_processes[processes_entered].burst_time;
      processes_entered++;

      if (processes_entered >= base_num_processes) {
        run_all_schedules();
      } else {
        input_state = INPUT_PROCESS_AT;
      }
      memset(input_buffer, 0, sizeof(input_buffer));
    }
    return;
  }

  if (isdigit(ch) && strlen(input_buffer) < sizeof(input_buffer) - 1) {
    int len = strlen(input_buffer);
    input_buffer[len] = ch;
    input_buffer[len + 1] = '\0';
  }
}

void scheduler_cleanup(void) {
}
