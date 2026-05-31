#ifndef ZENITH_WEB_SERVER_H
#define ZENITH_WEB_SERVER_H

#include <stdbool.h>

typedef struct {
    int port;
    bool running;
} WebServerConfig;

void start_web_server(int port);
void stop_web_server(void);
bool is_web_server_running(void);

#endif // ZENITH_WEB_SERVER_H
