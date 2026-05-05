#ifndef ZENITH_SYNC_H
#define ZENITH_SYNC_H

#include <stdbool.h>
#include <curses.h>

void do_sync(WINDOW *win);
bool sync_can_start(void);
void start_sync_test(void);
void cleanup_sync(void);

#endif // ZENITH_SYNC_H
