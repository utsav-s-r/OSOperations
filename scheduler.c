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
  double avg_wt;
  double avg_tat;
} AlgorithmResult;

static int base_num_processes = 0;
static int base_time_quantum = 2;
static Process base_processes[MAX_PROCESSES];
static AlgorithmResult results[4];
static bool simulation_complete = false;

typedef enum {
  INPUT_IDLE = 0,
  INPUT_NUM_PROCS = 1,
  INPUT_TIME_QUANTUM = 2,
  INPUT_PROCESS_AT = 3,
  INPUT_PROCESS_BT = 4,
  INPUT_PROCESS_PRIORITY = 5,
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

static void compute_metrics(int algo_idx) {
  AlgorithmResult *res = &results[algo_idx];
  double total_wt = 0, total_tat = 0;
  int count = base_num_processes;
  for (int i = 0; i < count; i++) {
    res->processes[i].turnaround_time =
        res->processes[i].completion_time - res->processes[i].arrival_time;
    res->processes[i].waiting_time =
        res->processes[i].turnaround_time - res->processes[i].burst_time;
    total_wt += res->processes[i].waiting_time;
    total_tat += res->processes[i].turnaround_time;
  }
  res->avg_wt = total_wt / count;
  res->avg_tat = total_tat / count;
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
    procs[i].response_time = current_time - procs[i].arrival_time;
    results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[i].pid;
    results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
    current_time += procs[i].burst_time;
    results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;
    procs[i].completion_time = current_time;
    results[algo_idx].gantt_count++;
  }
  results[algo_idx].total_time = current_time;
  compute_metrics(algo_idx);
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
      int min_at = 1000000;
      bool any_left = false;
      for (int i = 0; i < count; i++) {
        if (!completed[i]) {
          any_left = true;
          if (procs[i].arrival_time < min_at) min_at = procs[i].arrival_time;
        }
      }
      if (!any_left) break;
      current_time = min_at;
      continue;
    }

    results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[next_proc].pid;
    results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
    
    // Set response time
    procs[next_proc].response_time = current_time - procs[next_proc].arrival_time;

    current_time += procs[next_proc].burst_time;
    results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;
    procs[next_proc].completion_time = current_time;
    completed[next_proc] = true;
    results[algo_idx].gantt_count++;
  }

  results[algo_idx].total_time = current_time;
  compute_metrics(algo_idx);
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
      int min_at = 1000000;
      bool any_left = false;
      for (int i = 0; i < count; i++) {
        if (!completed[i]) {
          any_left = true;
          if (procs[i].arrival_time < min_at) min_at = procs[i].arrival_time;
        }
      }
      if (!any_left) break;
      current_time = min_at;
      continue;
    }

    results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[next_proc].pid;
    results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
    
    // Set response time
    procs[next_proc].response_time = current_time - procs[next_proc].arrival_time;

    current_time += procs[next_proc].burst_time;
    results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;
    procs[next_proc].completion_time = current_time;
    completed[next_proc] = true;
    results[algo_idx].gantt_count++;
  }

  results[algo_idx].total_time = current_time;
  compute_metrics(algo_idx);
}

static void schedule_round_robin(int algo_idx) {
  results[algo_idx].gantt_count = 0;
  int current_time = 0;
  Process *procs = results[algo_idx].processes;
  int count = base_num_processes;
  int time_quantum = base_time_quantum;

  memcpy(procs, base_processes, count * sizeof(Process));
  int remaining[MAX_PROCESSES];
  bool arrived[MAX_PROCESSES] = {false};
  int completed = 0;
  
  int queue[MAX_PROCESSES * 100]; 
  int head = 0, tail = 0;

  for (int i = 0; i < count; i++) {
    remaining[i] = procs[i].burst_time;
  }

  // Find the first arrival
  int first_arrival = 1000000;
  for(int i=0; i<count; i++) if(procs[i].arrival_time < first_arrival) first_arrival = procs[i].arrival_time;
  if(current_time < first_arrival) current_time = first_arrival;

  for (int i = 0; i < count; i++) {
    if (procs[i].arrival_time <= current_time) {
      queue[tail++] = i;
      arrived[i] = true;
    }
  }

  while (completed < count) {
    if (head < tail) {
      int idx = queue[head++];
      
      if (remaining[idx] == procs[idx].burst_time) {
          procs[idx].response_time = current_time - procs[idx].arrival_time;
      }

      int exec_time = (remaining[idx] > time_quantum) ? time_quantum : remaining[idx];

      results[algo_idx].gantt[results[algo_idx].gantt_count].pid = procs[idx].pid;
      results[algo_idx].gantt[results[algo_idx].gantt_count].start_time = current_time;
      
      // Crucial: check for new arrivals during execution
      for (int t = 1; t <= exec_time; t++) {
        current_time++;
        for (int i = 0; i < count; i++) {
          if (!arrived[i] && procs[i].arrival_time <= current_time) {
            queue[tail++] = i;
            arrived[i] = true;
          }
        }
      }

      remaining[idx] -= exec_time;
      results[algo_idx].gantt[results[algo_idx].gantt_count].end_time = current_time;

      if (remaining[idx] == 0) {
        procs[idx].completion_time = current_time;
        completed++;
      } else {
        queue[tail++] = idx; 
      }
      
      results[algo_idx].gantt_count++;
    } else {
      int next_at = 1000000;
      bool found = false;
      for (int i = 0; i < count; i++) {
        if (!arrived[i] && procs[i].arrival_time < next_at) {
          next_at = procs[i].arrival_time;
          found = true;
        }
      }
      if (found) {
        current_time = next_at;
        for (int i = 0; i < count; i++) {
          if (!arrived[i] && procs[i].arrival_time <= current_time) {
            queue[tail++] = i;
            arrived[i] = true;
          }
        }
      } else break;
    }
    if (results[algo_idx].gantt_count >= MAX_GANTT_BLOCKS) break;
  }

  results[algo_idx].total_time = current_time;
  compute_metrics(algo_idx);
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
    mvwprintw(win, 7, 2, "Press [ x ] to start interactive setup.");
    return;
  }

  if (input_active && !simulation_complete) {
    if (input_state == INPUT_NUM_PROCS) {
      wattron(win, COLOR_PAIR(C_WARNING) | A_BOLD);
      mvwprintw(win, 3, 2, "Enter number of processes (1-20): ");
      wattroff(win, COLOR_PAIR(C_WARNING) | A_BOLD);
      mvwprintw(win, 4, 2, "Current input: %s_", input_buffer);
    } else if (input_state == INPUT_TIME_QUANTUM) {
      wattron(win, COLOR_PAIR(C_WARNING) | A_BOLD);
      mvwprintw(win, 3, 2, "Enter Time Quantum for Round Robin: ");
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
  mvwprintw(win, 3, 2, "                                          "); // Clear line
  mvwprintw(win, 3, 2, "--- Scheduling Results ---"); 
  wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

  const char *algo_names[] = {"FCFS", "SJF", "Priority", "Round Robin"};
  int current_line = 0;
  int display_row = 5; 

  for (int algo = 0; algo < 4; algo++) {
    // Each algorithm block has: Title (1), Gantt (1), Header (1), Processes (N), Spacer (1)
    
    // Title
    if (current_line >= sched_scroll_offset && display_row < max_y - 8) {
      wattron(win, COLOR_PAIR(C_HEADER));
      mvwprintw(win, display_row++, 2, "%s (Total Time: %d) Avg WT: %.2f Avg TAT: %.2f", 
                algo_names[algo], results[algo].total_time, 
                results[algo].avg_wt, results[algo].avg_tat);
      wattroff(win, COLOR_PAIR(C_HEADER));
    }
    current_line++;

    // Gantt
    if (current_line >= sched_scroll_offset && display_row < max_y - 8) {
      int gantt_x = 2;
      mvwprintw(win, display_row, gantt_x, "Gantt: ");
      gantt_x += 7;
      for (int i = 0; i < results[algo].gantt_count && gantt_x < max_x - 10; i++) {
        mvwprintw(win, display_row, gantt_x, "P%d|", results[algo].gantt[i].pid);
        gantt_x += 4;
      }
      display_row++;
    }
    current_line++;

    // Metrics Header
    if (current_line >= sched_scroll_offset && display_row < max_y - 8) {
      wattron(win, COLOR_PAIR(C_NORMAL) | A_UNDERLINE);
      mvwprintw(win, display_row++, 2, "PID  AT  BT  PRI  CT  TAT  WT  RT");
      wattroff(win, COLOR_PAIR(C_NORMAL) | A_UNDERLINE);
    }
    current_line++;

    // Processes
    for (int i = 0; i < base_num_processes; i++) {
      if (current_line >= sched_scroll_offset && display_row < max_y - 8) {
        Process *p = &results[algo].processes[i];
        int color = (p->waiting_time < 5) ? C_HEALTHY : C_WARNING;
        wattron(win, COLOR_PAIR(color));
        mvwprintw(win, display_row++, 2, "P%-2d  %-2d  %-2d  %-3d  %-2d  %-3d  %-2d  %-2d", 
                  p->pid, p->arrival_time, p->burst_time, p->priority, p->completion_time,
                  p->turnaround_time, p->waiting_time, p->response_time);
        wattroff(win, COLOR_PAIR(color));
      }
      current_line++;
    }

    // Spacer
    if (current_line >= sched_scroll_offset && display_row < max_y - 8) {
      display_row++;
    }
    current_line++;
    
    if (display_row >= max_y - 8) break;
  }

  wattron(win, COLOR_PAIR(C_NORMAL) | A_BOLD);
  mvwprintw(win, max_y - 2, 2, "Press [ x ] for new schedule, [ p/m/d/i/c/g/s/t ] to switch");
  wattroff(win, COLOR_PAIR(C_NORMAL) | A_BOLD);
}

void scheduler_handle_input(char ch) {
  if ((ch == 'x' || ch == 'X') && !input_active) {
    if (simulation_complete) {
      simulation_complete = false;
      base_num_processes = 0;
      processes_entered = 0;
      memset(base_processes, 0, sizeof(base_processes));
      memset(results, 0, sizeof(results));
      sched_scroll_offset = 0;
    }
    input_active = true;
    input_state = INPUT_NUM_PROCS;
    memset(input_buffer, 0, sizeof(input_buffer));
    processes_entered = 0;
    base_num_processes = 0;
    simulation_complete = false;
    return;
  }

  if (!input_active) {
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
        input_state = INPUT_TIME_QUANTUM;
        memset(input_buffer, 0, sizeof(input_buffer));
      }
    } else if (input_state == INPUT_TIME_QUANTUM) {
      int tq = atoi(input_buffer);
      if (tq > 0) {
        base_time_quantum = tq;
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
