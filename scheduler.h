#ifndef ZENITH_SCHEDULER_H
#define ZENITH_SCHEDULER_H

#include <curses.h>
#include <stdbool.h>

#define MAX_PROCESSES 20
#define MAX_GANTT_BLOCKS 100

typedef enum {
  SCHED_FCFS = 0,
  SCHED_SJF = 1,
  SCHED_PRIORITY = 2,
  SCHED_RR = 3
} SchedulingAlgorithm;

typedef struct {
  int pid;
  int arrival_time;
  int burst_time;
  int priority;
  int completion_time;
  int turnaround_time;
  int waiting_time;
  int response_time;
  int remaining_time;
} Process;

typedef struct {
  int pid;
  int start_time;
  int end_time;
} GanttBlock;

typedef struct {
  SchedulingAlgorithm algorithm;
  Process processes[MAX_PROCESSES];
  int num_processes;
  int time_quantum;
  GanttBlock gantt[MAX_GANTT_BLOCKS];
  int gantt_count;
  bool simulation_complete;
  int current_time;
  int total_time;
} SchedulerContext;

void do_scheduler(WINDOW *win);
void scheduler_handle_input(char ch);
void scheduler_cleanup(void);

#endif // ZENITH_SCHEDULER_H
