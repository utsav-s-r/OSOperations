#ifndef ZENITH_FS_SIM_H
#define ZENITH_FS_SIM_H

#include <curses.h>
#include <stdbool.h>

extern bool fs_input_active;

void do_fs(WINDOW *win);
void fs_handle_input(int ch);
void fs_cleanup(void);

#endif // ZENITH_FS_SIM_H
