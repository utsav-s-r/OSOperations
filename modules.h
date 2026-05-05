#ifndef ZENITH_MODULES_H
#define ZENITH_MODULES_H

#include <curses.h>

void do_proc(WINDOW *win);
void do_mem(WINDOW *win);
void do_disk(WINDOW *win);
void do_ipc(WINDOW *win);
void do_temp(WINDOW *win);
void do_cpu(WINDOW *win);
void do_orphan(WINDOW *win);
void do_signals(WINDOW *win);

#endif // ZENITH_MODULES_H
