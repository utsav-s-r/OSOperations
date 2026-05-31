#include "web_server.h"
#include "os_stats.h"
#include "modules.h"
#include "fs_sim.h"
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static struct MHD_Daemon *httpd_daemon = NULL;
static int server_port = 8080;

// Web UI HTML content
static const char *web_ui_html = 
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>Zenith-OS Diagnostics</title>\n"
"    <style>\n"
"        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
"        body {\n"
"            font-family: 'Courier New', monospace;\n"
"            background: linear-gradient(135deg, #0a0a0a 0%, #1a1a2e 100%);\n"
"            color: #00ff00;\n"
"            padding: 20px;\n"
"            min-height: 100vh;\n"
"        }\n"
"        .container {\n"
"            max-width: 1200px;\n"
"            margin: 0 auto;\n"
"            background: rgba(20, 20, 40, 0.8);\n"
"            border: 2px solid #00ff00;\n"
"            border-radius: 8px;\n"
"            padding: 20px;\n"
"            box-shadow: 0 0 20px rgba(0, 255, 0, 0.3);\n"
"        }\n"
"        .header {\n"
"            text-align: center;\n"
"            border-bottom: 2px solid #00ff00;\n"
"            padding-bottom: 15px;\n"
"            margin-bottom: 20px;\n"
"        }\n"
"        .header h1 {\n"
"            font-size: 32px;\n"
"            text-shadow: 0 0 10px #00ff00;\n"
"            margin-bottom: 5px;\n"
"        }\n"
"        .header p {\n"
"            color: #00aa00;\n"
"            font-size: 12px;\n"
"        }\n"
"        .nav {\n"
"            display: flex;\n"
"            gap: 10px;\n"
"            margin-bottom: 20px;\n"
"            flex-wrap: wrap;\n"
"        }\n"
"        .nav button {\n"
"            padding: 10px 15px;\n"
"            background: #1a1a2e;\n"
"            border: 1px solid #00ff00;\n"
"            color: #00ff00;\n"
"            cursor: pointer;\n"
"            font-family: inherit;\n"
"            border-radius: 4px;\n"
"            transition: all 0.3s;\n"
"        }\n"
"        .nav button:hover, .nav button.active {\n"
"            background: #00ff00;\n"
"            color: #0a0a0a;\n"
"            font-weight: bold;\n"
"        }\n"
"        .content {\n"
"            display: grid;\n"
"            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));\n"
"            gap: 15px;\n"
"        }\n"
"        .card {\n"
"            background: rgba(30, 30, 50, 0.9);\n"
"            border: 1px solid #00ff00;\n"
"            border-radius: 4px;\n"
"            padding: 15px;\n"
"        }\n"
"        .card-title {\n"
"            font-size: 14px;\n"
"            font-weight: bold;\n"
"            margin-bottom: 10px;\n"
"            color: #00ff00;\n"
"            border-bottom: 1px solid #00aa00;\n"
"            padding-bottom: 5px;\n"
"        }\n"
"        .bar-container {\n"
"            background: #0a0a0a;\n"
"            border: 1px solid #00aa00;\n"
"            border-radius: 3px;\n"
"            height: 30px;\n"
"            margin: 8px 0;\n"
"            position: relative;\n"
"            overflow: hidden;\n"
"        }\n"
"        .bar {\n"
"            height: 100%;\n"
"            background: linear-gradient(90deg, #00ff00, #00aa00);\n"
"            transition: width 0.3s;\n"
"            display: flex;\n"
"            align-items: center;\n"
"            justify-content: flex-end;\n"
"            padding-right: 5px;\n"
"            font-size: 12px;\n"
"            color: #0a0a0a;\n"
"            font-weight: bold;\n"
"        }\n"
"        .full-width {\n"
"            grid-column: 1 / -1;\n"
"        }\n"
"        table {\n"
"            width: 100%;\n"
"            border-collapse: collapse;\n"
"            font-size: 12px;\n"
"        }\n"
"        table th {\n"
"            background: #1a1a2e;\n"
"            color: #00ff00;\n"
"            padding: 8px;\n"
"            text-align: left;\n"
"            border-bottom: 1px solid #00ff00;\n"
"        }\n"
"        table td {\n"
"            padding: 6px 8px;\n"
"            border-bottom: 1px solid #00aa00;\n"
"        }\n"
"        table tr:hover {\n"
"            background: rgba(0, 255, 0, 0.05);\n"
"        }\n"
"        .status { color: #00ff00; }\n"
"        .warning { color: #ffaa00; }\n"
"        .error { color: #ff0000; }\n"
"        .section { display: none; }\n"
"        .section.active { display: block; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <div class=\"header\">\n"
"            <h1>=== Zenith-OS Diagnostics ===</h1>\n"
"            <p>System Performance Monitor</p>\n"
"        </div>\n"
"\n"
"        <div class=\"nav\">\n"
"            <button class=\"nav-btn active\" onclick=\"showSection('overview')\">Overview</button>\n"
"            <button class=\"nav-btn\" onclick=\"showSection('processes')\">Processes</button>\n"
"            <button class=\"nav-btn\" onclick=\"showSection('memory')\">Memory</button>\n"
"            <button class=\"nav-btn\" onclick=\"showSection('disk')\">Disk</button>\n"
"        </div>\n"
"\n"
"        <div class=\"content\">\n"
"            <!-- Overview Section -->\n"
"            <div id=\"overview\" class=\"section active full-width\">\n"
"                <div class=\"card full-width\">\n"
"                    <div class=\"card-title\">System Overview</div>\n"
"                    <div>\n"
"                        <label>CPU Load</label>\n"
"                        <div class=\"bar-container\">\n"
"                            <div class=\"bar\" id=\"cpu-bar\" style=\"width: 0%\">0%</div>\n"
"                        </div>\n"
"                    </div>\n"
"                    <div>\n"
"                        <label>Memory Usage</label>\n"
"                        <div class=\"bar-container\">\n"
"                            <div class=\"bar\" id=\"mem-bar\" style=\"width: 0%\">0%</div>\n"
"                        </div>\n"
"                    </div>\n"
"                </div>\n"
"            </div>\n"
"\n"
"            <!-- Processes Section -->\n"
"            <div id=\"processes\" class=\"section full-width\">\n"
"                <div class=\"card full-width\">\n"
"                    <div class=\"card-title\">Active Processes</div>\n"
"                    <table id=\"processes-table\">\n"
"                        <thead>\n"
"                            <tr>\n"
"                                <th>PID</th>\n"
"                                <th>PPID</th>\n"
"                                <th>Name</th>\n"
"                            </tr>\n"
"                        </thead>\n"
"                        <tbody id=\"processes-body\">\n"
"                        </tbody>\n"
"                    </table>\n"
"                </div>\n"
"            </div>\n"
"\n"
"            <!-- Memory Section -->\n"
"            <div id=\"memory\" class=\"section full-width\">\n"
"                <div class=\"card full-width\">\n"
"                    <div class=\"card-title\">Memory Information</div>\n"
"                    <div id=\"memory-info\"></div>\n"
"                </div>\n"
"            </div>\n"
"\n"
"            <!-- Disk Section -->\n"
"            <div id=\"disk\" class=\"section full-width\">\n"
"                <div class=\"card full-width\">\n"
"                    <div class=\"card-title\">Disk Mounts</div>\n"
"                    <table id=\"disk-table\">\n"
"                        <thead>\n"
"                            <tr>\n"
"                                <th>Mount Point</th>\n"
"                                <th>Free Space</th>\n"
"                            </tr>\n"
"                        </thead>\n"
"                        <tbody id=\"disk-body\">\n"
"                        </tbody>\n"
"                    </table>\n"
"                </div>\n"
"            </div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        const API_BASE = 'http://localhost:8080/api';\n"
"\n"
"        function showSection(sectionId) {\n"
"            document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));\n"
"            document.getElementById(sectionId).classList.add('active');\n"
"            document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));\n"
"            event.target.classList.add('active');\n"
"            \n"
"            if (sectionId === 'processes') loadProcesses();\n"
"            else if (sectionId === 'memory') loadMemory();\n"
"            else if (sectionId === 'disk') loadDisk();\n"
"        }\n"
"\n"
"        async function loadOverview() {\n"
"            try {\n"
"                const cpuRes = await fetch(API_BASE + '/cpu');\n"
"                const cpuData = await cpuRes.json();\n"
"                document.getElementById('cpu-bar').style.width = cpuData.load + '%';\n"
"                document.getElementById('cpu-bar').textContent = cpuData.load.toFixed(1) + '%';\n"
"\n"
"                const memRes = await fetch(API_BASE + '/memory');\n"
"                const memData = await memRes.json();\n"
"                const memPct = (memData.used / memData.total) * 100;\n"
"                document.getElementById('mem-bar').style.width = memPct + '%';\n"
"                document.getElementById('mem-bar').textContent = memPct.toFixed(1) + '%';\n"
"            } catch (e) {\n"
"                console.error('Error loading overview:', e);\n"
"            }\n"
"        }\n"
"\n"
"        async function loadProcesses() {\n"
"            try {\n"
"                const res = await fetch(API_BASE + '/processes');\n"
"                const data = await res.json();\n"
"                const tbody = document.getElementById('processes-body');\n"
"                tbody.innerHTML = '';\n"
"                data.processes.forEach(p => {\n"
"                    const row = tbody.insertRow();\n"
"                    row.innerHTML = `<td>${p.pid}</td><td>${p.ppid}</td><td>${p.name}</td>`;\n"
"                });\n"
"            } catch (e) {\n"
"                console.error('Error loading processes:', e);\n"
"            }\n"
"        }\n"
"\n"
"        async function loadMemory() {\n"
"            try {\n"
"                const res = await fetch(API_BASE + '/memory');\n"
"                const data = await res.json();\n"
"                const info = document.getElementById('memory-info');\n"
"                info.innerHTML = `\n"
"                    <p>Total: ${(data.total / 1024 / 1024).toFixed(2)} GB</p>\n"
"                    <p>Available: ${(data.available / 1024 / 1024).toFixed(2)} GB</p>\n"
"                    <p>Used: ${(data.used / 1024 / 1024).toFixed(2)} GB</p>\n"
"                    <div class=\"bar-container\" style=\"margin-top: 10px;\">\n"
"                        <div class=\"bar\" style=\"width: ${(data.used / data.total) * 100}%\">\n"
"                            ${((data.used / data.total) * 100).toFixed(1)}%\n"
"                        </div>\n"
"                    </div>\n"
"                `;\n"
"            } catch (e) {\n"
"                console.error('Error loading memory:', e);\n"
"            }\n"
"        }\n"
"\n"
"        async function loadDisk() {\n"
"            try {\n"
"                const res = await fetch(API_BASE + '/disk');\n"
"                const data = await res.json();\n"
"                const tbody = document.getElementById('disk-body');\n"
"                tbody.innerHTML = '';\n"
"                data.mounts.forEach(m => {\n"
"                    const row = tbody.insertRow();\n"
"                    row.innerHTML = `<td>${m.mount}</td><td>${(m.free_space / 1024 / 1024 / 1024).toFixed(2)} GB</td>`;\n"
"                });\n"
"            } catch (e) {\n"
"                console.error('Error loading disk:', e);\n"
"            }\n"
"        }\n"
"\n"
"        // Load data on page load\n"
"        window.addEventListener('load', () => {\n"
"            loadOverview();\n"
"            setInterval(loadOverview, 2000);\n"
"        });\n"
"    </script>\n"
"</body>\n"
"</html>\n";

// JSON buffer helpers
static char *json_malloc(size_t size) {
    return malloc(size);
}

// API endpoint handlers
static enum MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version,
                          const char *upload_data, size_t *upload_data_size,
                          void **con_cls) {
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;
    
    struct MHD_Response *response = NULL;
    enum MHD_Result ret = MHD_NO;

    if (strcmp(method, "GET") != 0) {
        return MHD_NO;
    }

    // Serve main UI
    if (strcmp(url, "/") == 0) {
        response = MHD_create_response_from_buffer(strlen(web_ui_html),
                                                   (void *)web_ui_html,
                                                   MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    }
    // CPU endpoint
    else if (strcmp(url, "/api/cpu") == 0) {
        double cpu_load = get_cpu_load();
        char *buffer = json_malloc(256);
        snprintf(buffer, 256, "{\"load\": %.2f}", cpu_load);
        response = MHD_create_response_from_buffer(strlen(buffer), buffer,
                                                   MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    }
    // Memory endpoint
    else if (strcmp(url, "/api/memory") == 0) {
        double total = 0, available = 0;
        get_mem_stats(&total, &available);
        double used = total - available;
        char *buffer = json_malloc(512);
        snprintf(buffer, 512, "{\"total\": %.0f, \"available\": %.0f, \"used\": %.0f}",
                 total, available, used);
        response = MHD_create_response_from_buffer(strlen(buffer), buffer,
                                                   MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    }
    // Processes endpoint (placeholder)
    else if (strcmp(url, "/api/processes") == 0) {
        char *buffer = json_malloc(8192);
        strcpy(buffer, "{\"processes\": []}");
        response = MHD_create_response_from_buffer(strlen(buffer), buffer,
                                                   MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    }
    // Disk endpoint (placeholder)
    else if (strcmp(url, "/api/disk") == 0) {
        char *buffer = json_malloc(2048);
        strcpy(buffer, "{\"mounts\": []}");
        response = MHD_create_response_from_buffer(strlen(buffer), buffer,
                                                   MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    }
    else {
        const char *not_found = "404 - Not Found";
        response = MHD_create_response_from_buffer(strlen(not_found),
                                                   (void *)not_found,
                                                   MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    }

    if (response) {
        MHD_destroy_response(response);
    }
    return ret;
}

void start_web_server(int port) {
    server_port = port;
    printf("Starting web server on port %d...\n", port);
    
    // Try to start the daemon
    httpd_daemon = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD,
                              port, NULL, NULL,
                              &handle_request, NULL,
                              MHD_OPTION_END);
    
    if (httpd_daemon != NULL) {
        printf("✓ Web server started successfully on http://localhost:%d\n", port);
        printf("Open your browser and navigate to http://localhost:%d\n", port);
    } else {
        printf("✗ Failed to start web server on port %d\n", port);
        printf("Possible reasons:\n");
        printf("  - Port %d is already in use\n", port);
        printf("  - Insufficient permissions\n");
        printf("  - Library linking issues\n");
        printf("Try a different port with --port option\n");
    }
}

void stop_web_server(void) {
    if (httpd_daemon != NULL) {
        MHD_stop_daemon(httpd_daemon);
        httpd_daemon = NULL;
        printf("✓ Web server stopped\n");
    }
}

bool is_web_server_running(void) {
    return httpd_daemon != NULL;
}
