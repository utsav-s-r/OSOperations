#include "fs_sim.h"
#include "ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_FS_NODES 100
#define MAX_DISK_BLOCKS 64
#define MAX_TERM_LINES 100

typedef enum {
    NODE_FILE,
    NODE_DIR
} NodeType;

typedef struct {
    char name[32];
    NodeType type;
    int parent;
    int first_child;
    int next_sibling;
    char perms[4]; // "rwx", "r--", etc.
    
    int size; // in blocks
    
    int start_block;
    
    int linked_blocks[MAX_DISK_BLOCKS];
    int num_linked_blocks;
    
    int index_block;
    int index_pointers[MAX_DISK_BLOCKS];
} FSNode;

static FSNode fs_nodes[MAX_FS_NODES];
static int fs_node_count = 0;
static int current_dir = 0; // Root is 0
static bool fs_initialized = false;

static int disk_bitmap[MAX_DISK_BLOCKS];

bool fs_input_active = false;

static char term_lines[MAX_TERM_LINES][128];
static int term_line_count = 0;
static char input_buffer[64] = "";

static void print_term(const char* msg) {
    if (term_line_count < MAX_TERM_LINES) {
        strncpy(term_lines[term_line_count++], msg, 127);
    } else {
        for (int i=0; i<MAX_TERM_LINES-1; i++) {
            strcpy(term_lines[i], term_lines[i+1]);
        }
        strncpy(term_lines[MAX_TERM_LINES-1], msg, 127);
    }
}

static void init_fs() {
    if (fs_initialized) return;
    memset(fs_nodes, 0, sizeof(fs_nodes));
    memset(disk_bitmap, 0, sizeof(disk_bitmap));
    
    strcpy(fs_nodes[0].name, "/");
    fs_nodes[0].type = NODE_DIR;
    fs_nodes[0].parent = -1;
    fs_nodes[0].first_child = -1;
    fs_nodes[0].next_sibling = -1;
    strcpy(fs_nodes[0].perms, "rwx");
    
    fs_node_count = 1;
    fs_initialized = true;
    print_term("Zenith-OS File System Simulator started.");
    print_term("Commands: create <file> [size], delete <file>, mkdir <dir>, ls, chmod <perms> <file>");
}

static int alloc_blocks(int count, FSNode *node) {
    // Need to allocate 'count' blocks for contiguous, linked, and indexed for demonstration
    // We will just allocate 'count' free blocks in bitmap
    int allocated[MAX_DISK_BLOCKS];
    int found = 0;
    
    // For contiguous
    int contig_start = -1;
    int current_streak = 0;
    for (int i=0; i<MAX_DISK_BLOCKS; i++) {
        if (disk_bitmap[i] == 0) {
            current_streak++;
            if (current_streak == count) {
                contig_start = i - count + 1;
                break;
            }
        } else {
            current_streak = 0;
        }
    }
    
    if (contig_start != -1) {
        node->start_block = contig_start;
    } else {
        node->start_block = -1; // fragmented, cannot do contiguous
    }
    
    // Gather random free blocks for linked and indexed
    for (int i=0; i<MAX_DISK_BLOCKS && found < count; i++) {
        if (disk_bitmap[i] == 0) {
            allocated[found++] = i;
        }
    }
    
    if (found < count) return 0; // Disk full
    
    // Mark allocated
    for (int i=0; i<count; i++) {
        disk_bitmap[allocated[i]] = 1;
        node->linked_blocks[i] = allocated[i];
    }
    node->num_linked_blocks = count;
    
    // Indexed: use an extra block for index
    int idx_blk = -1;
    for (int i=0; i<MAX_DISK_BLOCKS; i++) {
        if (disk_bitmap[i] == 0) {
            idx_blk = i;
            break;
        }
    }
    if (idx_blk != -1) {
        disk_bitmap[idx_blk] = 1;
        node->index_block = idx_blk;
        for (int i=0; i<count; i++) {
            node->index_pointers[i] = allocated[i];
        }
    } else {
        node->index_block = -1;
    }
    
    return 1;
}

static void free_blocks(FSNode *node) {
    if (node->type == NODE_DIR) return;
    for (int i=0; i<node->num_linked_blocks; i++) {
        disk_bitmap[node->linked_blocks[i]] = 0;
    }
    if (node->index_block != -1) {
        disk_bitmap[node->index_block] = 0;
    }
}

static void process_cmd(char *cmd) {
    char buf[128];
    snprintf(buf, sizeof(buf), "> %s", cmd);
    print_term(buf);
    
    char op[32], arg1[32], arg2[32];
    int parts = sscanf(cmd, "%s %s %s", op, arg1, arg2);
    
    if (parts < 1) return;
    
    if (strcmp(op, "ls") == 0) {
        int child = fs_nodes[current_dir].first_child;
        char ls_buf[128] = "";
        while (child != -1) {
            strcat(ls_buf, fs_nodes[child].name);
            if (fs_nodes[child].type == NODE_DIR) strcat(ls_buf, "/");
            strcat(ls_buf, "  ");
            child = fs_nodes[child].next_sibling;
        }
        if (strlen(ls_buf) > 0) print_term(ls_buf);
        else print_term("(empty)");
    } 
    else if (strcmp(op, "mkdir") == 0 && parts >= 2) {
        if (fs_node_count >= MAX_FS_NODES) {
            print_term("Error: FS full");
            return;
        }
        int new_node = fs_node_count++;
        strcpy(fs_nodes[new_node].name, arg1);
        fs_nodes[new_node].type = NODE_DIR;
        fs_nodes[new_node].parent = current_dir;
        fs_nodes[new_node].first_child = -1;
        fs_nodes[new_node].next_sibling = fs_nodes[current_dir].first_child;
        strcpy(fs_nodes[new_node].perms, "rwx");
        fs_nodes[current_dir].first_child = new_node;
        print_term("Directory created.");
    }
    else if (strcmp(op, "create") == 0 && parts >= 2) {
        if (fs_node_count >= MAX_FS_NODES) {
            print_term("Error: FS full");
            return;
        }
        int size = 3; // default
        if (parts >= 3) size = atoi(arg2);
        if (size <= 0 || size > 10) size = 3;
        
        int new_node = fs_node_count++;
        strcpy(fs_nodes[new_node].name, arg1);
        fs_nodes[new_node].type = NODE_FILE;
        fs_nodes[new_node].parent = current_dir;
        fs_nodes[new_node].first_child = -1;
        fs_nodes[new_node].next_sibling = fs_nodes[current_dir].first_child;
        strcpy(fs_nodes[new_node].perms, "rw-");
        fs_nodes[new_node].size = size;
        
        if (!alloc_blocks(size, &fs_nodes[new_node])) {
            print_term("Error: Disk full");
            fs_node_count--;
            return;
        }
        
        fs_nodes[current_dir].first_child = new_node;
        print_term("File created.");
    }
    else if (strcmp(op, "delete") == 0 && parts >= 2) {
        int prev = -1;
        int curr = fs_nodes[current_dir].first_child;
        while (curr != -1) {
            if (strcmp(fs_nodes[curr].name, arg1) == 0) {
                if (fs_nodes[curr].type == NODE_DIR && fs_nodes[curr].first_child != -1) {
                    print_term("Error: Dir not empty");
                    return;
                }
                if (prev == -1) fs_nodes[current_dir].first_child = fs_nodes[curr].next_sibling;
                else fs_nodes[prev].next_sibling = fs_nodes[curr].next_sibling;
                
                free_blocks(&fs_nodes[curr]);
                print_term("Deleted.");
                return;
            }
            prev = curr;
            curr = fs_nodes[curr].next_sibling;
        }
        print_term("Error: Not found");
    }
    else if (strcmp(op, "chmod") == 0 && parts >= 3) {
        int curr = fs_nodes[current_dir].first_child;
        while (curr != -1) {
            if (strcmp(fs_nodes[curr].name, arg2) == 0) {
                strncpy(fs_nodes[curr].perms, arg1, 3);
                fs_nodes[curr].perms[3] = '\0';
                print_term("Permissions updated.");
                return;
            }
            curr = fs_nodes[curr].next_sibling;
        }
        print_term("Error: Not found");
    }
    else if (strcmp(op, "cd") == 0 && parts >= 2) {
        if (strcmp(arg1, "..") == 0) {
            if (fs_nodes[current_dir].parent != -1) {
                current_dir = fs_nodes[current_dir].parent;
            } else {
                print_term("Already at root.");
            }
        } else {
            int curr = fs_nodes[current_dir].first_child;
            bool found = false;
            while (curr != -1) {
                if (strcmp(fs_nodes[curr].name, arg1) == 0) {
                    if (fs_nodes[curr].type == NODE_DIR) {
                        current_dir = curr;
                        found = true;
                    } else {
                        print_term("Error: Not a directory");
                        found = true;
                    }
                    break;
                }
                curr = fs_nodes[curr].next_sibling;
            }
            if (!found) print_term("Error: Not found");
        }
    }
    else {
        print_term("Unknown command.");
    }
}

static void get_current_path(char *buf, int size) {
    if (current_dir == 0) {
        strncpy(buf, "/", size);
        return;
    }
    
    int path_nodes[15];
    int count = 0;
    int curr = current_dir;
    while (curr != 0 && count < 15) {
        path_nodes[count++] = curr;
        curr = fs_nodes[curr].parent;
    }
    
    buf[0] = '\0';
    for (int i = count - 1; i >= 0; i--) {
        if (strlen(buf) + strlen(fs_nodes[path_nodes[i]].name) + 2 < (size_t)size) {
            strcat(buf, "/");
            strcat(buf, fs_nodes[path_nodes[i]].name);
        }
    }
}

void fs_handle_input(int ch) {
    if (!fs_input_active) {
        if (ch == 'f' || ch == 'F') {
            fs_input_active = true;
            input_buffer[0] = '\0';
        }
        return;
    }

    if (ch == 27) { // ESC
        fs_input_active = false;
        return;
    }

    if (ch == '\n' || ch == '\r') {
        if (strlen(input_buffer) > 0) {
            process_cmd(input_buffer);
            input_buffer[0] = '\0';
        }
    } else if (ch == 127 || ch == 8 || ch == KEY_BACKSPACE) {
        int len = strlen(input_buffer);
        if (len > 0) input_buffer[len - 1] = '\0';
    } else if (ch >= 32 && ch <= 126 && strlen(input_buffer) < 63) {
        int len = strlen(input_buffer);
        input_buffer[len] = (char)ch;
        input_buffer[len + 1] = '\0';
    }
}

void do_fs(WINDOW *win) {
    init_fs();
    
    wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
    mvwprintw(win, 1, 2, "--- File System Simulation ---");
    wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);

    int max_lines = max_y - 8;
    int display_row = 3 - scroll_offset;
    
    if (display_row > 0 && display_row < max_lines) {
        wattron(win, COLOR_PAIR(C_WARNING) | A_BOLD);
        mvwprintw(win, display_row, 2, "Mini Terminal");
        wattroff(win, COLOR_PAIR(C_WARNING) | A_BOLD);
    }
    display_row++;
    
    if (!fs_input_active) {
        if (display_row > 0 && display_row < max_lines) {
            wattron(win, COLOR_PAIR(C_NORMAL));
            mvwprintw(win, display_row, 2, "Press [ f ] to start typing. Press [ ESC ] to stop.");
            wattroff(win, COLOR_PAIR(C_NORMAL));
        }
        display_row++;
    }

    int start_term = (term_line_count > 6) ? term_line_count - 6 : 0;
    for (int i=start_term; i<term_line_count; i++) {
        if (display_row > 0 && display_row < max_lines) {
            mvwprintw(win, display_row, 2, "%s", term_lines[i]);
        }
        display_row++;
    }
    
    char path_buf[64];
    get_current_path(path_buf, sizeof(path_buf));
    
    if (display_row > 0 && display_row < max_lines) {
        if (fs_input_active) {
            wattron(win, COLOR_PAIR(C_HEALTHY));
            mvwprintw(win, display_row, 2, "%s $ %s_", path_buf, input_buffer);
            wattroff(win, COLOR_PAIR(C_HEALTHY));
        } else {
            wattron(win, COLOR_PAIR(C_WARNING));
            mvwprintw(win, display_row, 2, "%s $ %s", path_buf, input_buffer);
            wattroff(win, COLOR_PAIR(C_WARNING));
        }
    }
    display_row += 2;
    
    if (display_row > 0 && display_row < max_lines) {
        wattron(win, COLOR_PAIR(C_HEADER) | A_BOLD);
        mvwprintw(win, display_row, 2, "File Allocation Visualizations");
        wattroff(win, COLOR_PAIR(C_HEADER) | A_BOLD);
    }
    display_row++;

    // Print tree and allocations for the CURRENT directory
    int curr = fs_nodes[current_dir].first_child;
    while (curr != -1) {
        if (display_row > 0 && display_row < max_lines) {
            mvwprintw(win, display_row, 2, "[%s] %-10s", fs_nodes[curr].perms, fs_nodes[curr].name);
        }
        display_row++;
        
        if (fs_nodes[curr].type == NODE_FILE) {
            if (display_row > 0 && display_row < max_lines) {
                wattron(win, COLOR_PAIR(C_NORMAL));
                mvwprintw(win, display_row, 4, "Contiguous: ");
                if (fs_nodes[curr].start_block != -1) {
                    for(int i=0; i<fs_nodes[curr].size; i++) wprintw(win, "[%d]", fs_nodes[curr].start_block + i);
                } else {
                    wprintw(win, "Failed (No contiguous space)");
                }
                wattroff(win, COLOR_PAIR(C_NORMAL));
            }
            display_row++;
            
            if (display_row > 0 && display_row < max_lines) {
                wattron(win, COLOR_PAIR(C_NORMAL));
                mvwprintw(win, display_row, 4, "Linked:     ");
                for(int i=0; i<fs_nodes[curr].num_linked_blocks; i++) {
                    wprintw(win, "[%d]", fs_nodes[curr].linked_blocks[i]);
                    if (i < fs_nodes[curr].num_linked_blocks - 1) wprintw(win, "->");
                }
                wattroff(win, COLOR_PAIR(C_NORMAL));
            }
            display_row++;
            
            if (display_row > 0 && display_row < max_lines) {
                wattron(win, COLOR_PAIR(C_NORMAL));
                mvwprintw(win, display_row, 4, "Indexed:    Idx[%d] -> ", fs_nodes[curr].index_block);
                for(int i=0; i<fs_nodes[curr].size; i++) {
                    wprintw(win, "%d ", fs_nodes[curr].index_pointers[i]);
                }
                wattroff(win, COLOR_PAIR(C_NORMAL));
            }
            display_row++;
        }
        
        curr = fs_nodes[curr].next_sibling;
    }
}

void fs_cleanup(void) {
}
