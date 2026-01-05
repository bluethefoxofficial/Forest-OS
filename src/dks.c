#include "include/dks.h"
#include "include/shell_loader.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/kb.h"
#include "include/memory.h"
#include "include/panic.h"
#include "include/hardware.h"
#include "include/power.h"
#include "include/ramdisk.h"
#include "include/vfs.h"
#include "include/task.h"
#include "include/string.h"
#include "include/graphics/graphics_manager.h"
#include "include/graphics/graphics_types.h"
#include "include/tty.h"
#include "include/bmp.h"

static int atoi(const char* s) {
    int n = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        n = 10 * n + (*s++ - '0');
    }
    return neg ? -n : n;
}

// Simple trigonometric approximations
static double dks_sin(double angle_rad) {
    // Taylor series approximation: sin(x) ≈ x - x³/6 + x⁵/120
    double x = angle_rad;
    while (x > 3.14159) x -= 2 * 3.14159;
    while (x < -3.14159) x += 2 * 3.14159;
    double x2 = x * x;
    double x3 = x2 * x;
    double x5 = x3 * x2;
    return x - x3/6.0 + x5/120.0;
}

static double dks_cos(double angle_rad) {
    // cos(x) = sin(x + π/2)
    return dks_sin(angle_rad + 1.5708);
}

static char* strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* new_s = malloc(len);
    if (new_s == NULL) return NULL;
    return (char*)memcpy(new_s, s, len);
}

#define putchar printch

#define MAX_CWD_LEN 256
static char dks_cwd[MAX_CWD_LEN] = "";

#define HISTORY_SIZE 16
static char* history_buffer[HISTORY_SIZE];
static int history_index = 0;
static int history_count = 0;
static int history_nav_index = -1; // Current position in history navigation

static void add_to_history(const char* command) {
    if (history_count > 0 && strcmp(command, history_buffer[(history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE]) == 0) {
        return; // Don't add duplicate consecutive commands
    }
    if (history_buffer[history_index]) {
        free(history_buffer[history_index]);
    }
    history_buffer[history_index] = strdup(command);
    history_index = (history_index + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        history_count++;
    }
}

static void dks_print_history(void) {
    int start = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
    for (int i = 0; i < history_count; i++) {
        print_dec(i + 1);
        print("  ");
        print(history_buffer[(start + i) % HISTORY_SIZE]);
        print("\n");
    }
}


static void dks_print_help(void) {
    print("Commands:\n");
    print("  help, cat, cd, clear, cls, cpuid, echo, halt, head, history, kill, ls, mem\n");
    print("  panic, ps, pwd, reboot, run, shell, shutdown, sleep, tail, tui, wc, whoami\n");
    print("  pixel, rect, line, mouse, fill_screen, clear_area, circle, triangle, draw_string, draw_window,\n");
    print("  draw_ellipse, draw_polygon, draw_gradient, draw_image, draw_rounded_rect, draw_arc,\n");
    print("  swap_buffers, enable_double_buffering, wait_vsync, set_resolution\n");
    print("\nArrow Keys:\n");
    print("  Up/Down: Navigate command history\n");
    print("  Left/Right: Move cursor in input line\n");
}

static void dks_dump_memory(void) {
    memory_stats_t stats = memory_get_stats();
    print("Total memory: ");
    print_dec(stats.total_memory_kb);
    print(" KB\n");
}

static void dks_tui_demo(void) {
    clearScreen();
    tui_draw_status_bar(0, "Forest OS - Enhanced TUI Demonstration", "Press any key to continue", 0x0F, 0x01);
    tui_draw_window(2, 2, 35, 8, "System Information", 0x0F, 0x02);
    tui_print_at(4, 4, "Forest OS v1.0", 0x0F, 0x02);
    readStr();
    clearScreen();
}

static void dks_show_cpuid(void) {
    const cpuid_info_t* info = hardware_get_cpuid_info();
    print("[CPUID] Hardware detection:\n");
    print("  Vendor: "); print(info->vendor_id); print("\n");
    print("  Brand:  "); print(info->brand_string); print("\n");
}



static const char* task_state_to_str(task_state_t state) {
    switch(state) {
        case TASK_STATE_RUNNING: return "RUNNING";
        case TASK_STATE_READY: return "READY";
        case TASK_STATE_WAITING: return "WAITING";
        case TASK_STATE_TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

static void dks_ps(void) {
    print("PID  State       Name\n");
    if (ready_queue_head) {
        task_t* current = ready_queue_head;
        do {
            print_dec(current->id);
            print("    ");
            print(task_state_to_str(current->state));
            print("     ");
            print(current->name);
            if (current == current_task) {
                print(" (current)");
            }
            print("\n");
            current = current->next;
        } while (current != ready_queue_head);
    }
}

static void dks_ls(char* path) {
    char target_path[MAX_CWD_LEN];
    if (path) {
        if (path[0] == '/') {
            strcpy(target_path, path + 1);
        } else {
            strcpy(target_path, dks_cwd);
            strcat(target_path, path);
        }
    } else {
        strcpy(target_path, dks_cwd);
    }

    print("Listing for /"); print(target_path); print(":\n");

    uint32 count = ramdisk_file_count();
    for (uint32 i = 0; i < count; i++) {
        const ramdisk_file_t* file = ramdisk_get(i);
        if (file) {
            const char* name = file->name;
            if (strlen(target_path) > 0 && strncmp(name, target_path, strlen(target_path)) != 0) {
                continue;
            }
            
            const char* subpath = name + strlen(target_path);
            
            char* next_slash = strchr(subpath, '/');
            if (!next_slash || (next_slash == subpath + strlen(subpath) - 1)) {
                print(subpath);
                 if (file->is_dir) {
                    print(" [DIR]");
                } else {
                    print(" (");
                    print_dec(file->size);
                    print(" bytes)");
                }
                print("\n");
            }
        }
    }
}

static void dks_cd(char* dir) {
    if (!dir || strlen(dir) == 0 || strcmp(dir, "/") == 0) {
        dks_cwd[0] = '\0';
        return;
    }

    char new_cwd[MAX_CWD_LEN];

    if (strcmp(dir, "..") == 0) {
        if (strlen(dks_cwd) > 0) {
            int i = strlen(dks_cwd) - 2;
            for (; i >= 0; i--) {
                if (dks_cwd[i] == '/') {
                    dks_cwd[i+1] = '\0';
                    return;
                }
            }
            dks_cwd[0] = '\0';
        }
        return;
    } else if (strcmp(dir, ".") == 0) {
        return;
    } else if (dir[0] == '/') {
        strcpy(new_cwd, dir + 1);
    } else {
        strcpy(new_cwd, dks_cwd);
        strcat(new_cwd, dir);
    }
    
    if (new_cwd[strlen(new_cwd)-1] != '/') {
        strcat(new_cwd, "/");
    }

    if (ramdisk_find(new_cwd)) {
        strcpy(dks_cwd, new_cwd);
        return;
    }

    print("Directory not found: "); print(dir); print("\n");
}


char* resolve_path(const char* path) {
    static char fullpath[MAX_CWD_LEN];
    if (path[0] == '/') {
        strcpy(fullpath, path + 1);
    } else {
        strcpy(fullpath, dks_cwd);
        strcat(fullpath, path);
    }
    return fullpath;
}


static void dks_cat(char* args) {
    if (!args) { print("Usage: cat <filename>\n"); return; }
    const uint8* data;
    uint32 size;
    if (vfs_read_file(resolve_path(args), &data, &size)) {
        for (uint32 i = 0; i < size; i++) {
            putchar(data[i]);
        }
        print("\n");
    } else {
        print("File not found: "); print(args); print("\n");
    }
}

static void dks_wait(uint32 pid) {
    print("Waiting for PID "); print_dec(pid); print("...\n");
    while(task_exists(pid)) {
        // Yield CPU
        sleep_interruptible(10);
    }
    print("PID "); print_dec(pid); print(" terminated.\n");
}


static void dks_run_program(char* args) {
    if (!args) { print("Usage: run <path_to_executable> [&]\n"); return; }

    bool background = false;
    char* ampersand = strchr(args, '&');
    if (ampersand) {
        background = true;
        *ampersand = '\0'; // Remove the '&' from the path
        // Trim trailing spaces
        int len = strlen(args);
        while (len > 0 && args[len-1] == ' ') {
            args[--len] = '\0';
        }
    }

    const uint8* elf_data;
    uint32 elf_size;
    if (!vfs_read_file(resolve_path(args), &elf_data, &elf_size)) {
        print_colored("ERROR: ELF not found.\n", 0x0C, 0x00);
        return;
    }
    task_t* task = task_create_elf(elf_data, elf_size, args);
    if (!task) {
        print_colored("ERROR: Failed to create task.\n", 0x0C, 0x00);
        return;
    }
    print("Task created with ID: "); print(int_to_string(task->id)); print("\n");

    if (!background) {
        dks_wait(task->id);
    }
}

static void dks_pwd(void) {
    print(dks_cwd);
    print("\n");
}



static void dks_kill(char* arg) {
    if (!arg) { print("Usage: kill <pid>\n"); return; }
    uint32 pid = atoi(arg);
    if (pid == 0) { print("Invalid PID.\n"); return; }
    task_kill(pid);
    print("Sent SIGKILL to PID "); print_dec(pid); print("\n");
}

static void dks_head(char* arg) {
    if (!arg) { print("Usage: head <file>\n"); return; }
    const uint8* data;
    uint32 size;
    if (!vfs_read_file(resolve_path(arg), &data, &size)) {
        print("File not found\n");
        return;
    }
    int lines = 0;
    for (uint32 i = 0; i < size && lines < 10; i++) {
        putchar(data[i]);
        if (data[i] == '\n') {
            lines++;
        }
    }
}

static void dks_tail(char* arg) {
    if (!arg) { print("Usage: tail <file>\n"); return; }
     const uint8* data;
    uint32 size;
    if (!vfs_read_file(resolve_path(arg), &data, &size)) {
        print("File not found\n");
        return;
    }

    int lines = 0;
    int i = size -1;
    for (; i >= 0 && lines < 11; i--) {
        if (data[i] == '\n') {
            lines++;
        }
    }
    i++; 
    if(data[i] == '\n') i++;

    for(; (uint32)i < size; i++) {
        putchar(data[i]);
    }
}

static void dks_wc(char* arg) {
    if (!arg) { print("Usage: wc <file>\n"); return; }
    const uint8* data;
    uint32 size;
    if (!vfs_read_file(resolve_path(arg), &data, &size)) {
        print("File not found\n");
        return;
    }
    uint32 lines = 0, words = 0, chars = size;
    bool in_word = false;
    for (uint32 i = 0; i < size; i++) {
        if (data[i] == '\n') lines++;
        if (data[i] == ' ' || data[i] == '\n' || data[i] == '\t') {
            in_word = false;
        } else if (!in_word) {
            words++;
            in_word = true;
        }
    }
    print("Lines: "); print_dec(lines);
    print(" Words: "); print_dec(words);
    print(" Chars: "); print_dec(chars);
    print("\n");
}

static void dks_pixel(char* args) {
    if (!args) { print("Usage: pixel <x> <y> <r> <g> <b>\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!x_str || !y_str || !r_str || !g_str || !b_str) {
        print("Usage: pixel <x> <y> <r> <g> <b>\n"); return;
    }
    int x = atoi(x_str);
    int y = atoi(y_str);
    uint8 r = atoi(r_str);
    uint8 g = atoi(g_str);
    uint8 b = atoi(b_str);
    graphics_draw_pixel(x, y, graphics_make_color(r, g, b, 255));
}

static void dks_rect(char* args) {
    if (!args) { print("Usage: rect <x1> <y1> <x2> <y2> <r> <g> <b> [filled]\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    if (!x1_str || !y1_str || !x2_str || !y2_str || !r_str || !g_str || !b_str) {
        print("Usage: rect <x1> <y1> <x2> <y2> <r> <g> <b> [filled]\n"); return;
    }
    graphics_rect_t rect = {atoi(x1_str), atoi(y1_str), atoi(x2_str) - atoi(x1_str), atoi(y2_str) - atoi(y1_str)};
    graphics_color_t color = graphics_make_color(atoi(r_str), atoi(g_str), atoi(b_str), 255);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    graphics_draw_rect(&rect, color, filled);
}

static void dks_line(char* args) {
    if (!args) { print("Usage: line <x1> <y1> <x2> <y2> <r> <g> <b>\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!x1_str || !y1_str || !x2_str || !y2_str || !r_str || !g_str || !b_str) {
        print("Usage: line <x1> <y1> <x2> <y2> <r> <g> <b>\n"); return;
    }
    graphics_draw_line(atoi(x1_str), atoi(y1_str), atoi(x2_str), atoi(y2_str), graphics_make_color(atoi(r_str), atoi(g_str), atoi(b_str), 255));
}

#define CURSOR_WIDTH 10
#define CURSOR_HEIGHT 10

static graphics_color_t arrow_cursor_colors_data[CURSOR_WIDTH * CURSOR_HEIGHT] = {
    // X = black, . = transparent
    // X..........
    // XX.........
    // X.X........
    // X..X.......
    // X...X......
    // X....X.....
    // X.....X....
    // X......X...
    // X.......X..
    // X........X.
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_BLACK, COLOR_TRANSPARENT,
    COLOR_BLACK, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_TRANSPARENT, COLOR_BLACK
};

static graphics_surface_t* mouse_cursor_surface = NULL; // Will be initialized once

static void dks_mouse_pointer(char* args) { // Renamed from dks_mouse to avoid conflict and be more specific
    if (!args) { print("Usage: mouse <x> <y> [show|hide]\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* show_hide_str = strtok(NULL, " ");

    if (!x_str || !y_str) {
        print("Usage: mouse <x> <y> [show|hide]\n"); return;
    }

    int x = atoi(x_str);
    int y = atoi(y_str);
    bool show = true;

    if (show_hide_str) {
        if (strcmp(show_hide_str, "hide") == 0) {
            show = false;
        } else if (strcmp(show_hide_str, "show") == 0) {
            show = true;
        } else {
            print("Invalid argument for show/hide. Use 'show' or 'hide'.\n");
            return;
        }
    }
    
    // Initialize cursor surface if not already done
    if (!mouse_cursor_surface) {
        graphics_create_surface(CURSOR_WIDTH, CURSOR_HEIGHT, PIXEL_FORMAT_RGBA_8888, &mouse_cursor_surface);
        if (!mouse_cursor_surface) {
            print("ERROR: Failed to create mouse cursor surface.\n");
            return;
        }

        // Fill the surface with the cursor pixel data
        uint32_t* pixel_buffer = (uint32_t*)mouse_cursor_surface->pixels;
        if (!pixel_buffer) {
            print("ERROR: Mouse cursor surface pixel buffer is NULL.\n");
            graphics_destroy_surface(mouse_cursor_surface);
            mouse_cursor_surface = NULL;
            return;
        }

        for (int i = 0; i < CURSOR_WIDTH * CURSOR_HEIGHT; ++i) {
            pixel_buffer[i] = graphics_color_to_pixel(arrow_cursor_colors_data[i], mouse_cursor_surface->format);
        }
        graphics_set_cursor(mouse_cursor_surface, 0, 0); // Hotspot at top-left (0,0)
    }

    graphics_move_cursor(x, y);
    graphics_show_cursor(show);
    print("Mouse cursor "); print(show ? "shown" : "hidden");
    print(" at ("); print_dec(x); print(", "); print_dec(y); print(")\n");
}

static void dks_fill_screen(char* args) {
    if (!args) { print("Usage: fill_screen <r> <g> <b>\n"); return; }
    char* r_str = strtok(args, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!r_str || !g_str || !b_str) {
        print("Usage: fill_screen <r> <g> <b>\n"); return;
    }
    uint8 r = atoi(r_str);
    uint8 g = atoi(g_str);
    uint8 b = atoi(b_str);
    graphics_clear_screen(graphics_make_color(r, g, b, 255));
}

static void dks_clear_area(char* args) {
    if (!args) { print("Usage: clear_area <x> <y> <width> <height> <r> <g> <b>\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* width_str = strtok(NULL, " ");
    char* height_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!x_str || !y_str || !width_str || !height_str || !r_str || !g_str || !b_str) {
        print("Usage: clear_area <x> <y> <width> <height> <r> <g> <b>\n"); return;
    }
    graphics_rect_t rect = {atoi(x_str), atoi(y_str), atoi(width_str), atoi(height_str)};
    graphics_color_t color = graphics_make_color(atoi(r_str), atoi(g_str), atoi(b_str), 255);
    graphics_draw_rect(&rect, color, true); // Always filled
}

static void dks_draw_circle(char* args) {
    if (!args) { print("Usage: circle <center_x> <center_y> <radius> <r> <g> <b> [filled]\n"); return; }
    char* cx_str = strtok(args, " ");
    char* cy_str = strtok(NULL, " ");
    char* r_str_radius = strtok(NULL, " ");
    char* r_str_color = strtok(NULL, " ");
    char* g_str_color = strtok(NULL, " ");
    char* b_str_color = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");

    if (!cx_str || !cy_str || !r_str_radius || !r_str_color || !g_str_color || !b_str_color) {
        print("Usage: circle <center_x> <center_y> <radius> <r> <g> <b> [filled]\n"); return;
    }

    int32_t center_x = atoi(cx_str);
    int32_t center_y = atoi(cy_str);
    int32_t radius = atoi(r_str_radius);
    uint8_t r = atoi(r_str_color);
    uint8_t g = atoi(g_str_color);
    uint8_t b = atoi(b_str_color);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;

    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    if (filled) {
        // Filled circle
        for (int32_t y = -radius; y <= radius; y++) {
            for (int32_t x = -radius; x <= radius; x++) {
                if (x*x + y*y <= radius*radius) {
                    graphics_draw_pixel(center_x + x, center_y + y, color);
                }
            }
        }
    } else {
        // Outline circle using midpoint algorithm
        for (int angle = 0; angle < 360; angle++) {
            int32_t x = center_x + (int32_t)(radius * dks_cos(angle * 3.14159 / 180.0));
            int32_t y = center_y + (int32_t)(radius * dks_sin(angle * 3.14159 / 180.0));
            graphics_draw_pixel(x, y, color);
        }
    }
}

static void dks_draw_triangle(char* args) {
    if (!args) { print("Usage: triangle <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* x3_str = strtok(NULL, " ");
    char* y3_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");

    if (!x1_str || !y1_str || !x2_str || !y2_str || !x3_str || !y3_str || !r_str || !g_str || !b_str) {
        print("Usage: triangle <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return;
    }

    int32_t x1 = atoi(x1_str);
    int32_t y1 = atoi(y1_str);
    int32_t x2 = atoi(x2_str);
    int32_t y2 = atoi(y2_str);
    int32_t x3 = atoi(x3_str);
    int32_t y3 = atoi(y3_str);
    uint8_t r = atoi(r_str);
    uint8_t g = atoi(g_str);
    uint8_t b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;

    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    if (filled) {
        // Simple filled triangle using scanline algorithm
        // Sort vertices by y
        int32_t tx1 = x1, ty1 = y1, tx2 = x2, ty2 = y2, tx3 = x3, ty3 = y3;
        // Bubble sort by y
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        if (ty2 > ty3) { int32_t t = tx2; tx2 = tx3; tx3 = t; t = ty2; ty2 = ty3; ty3 = t; }
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        
        // Draw filled triangle
        for (int32_t y = ty1; y <= ty3; y++) {
            int32_t x_start, x_end;
            if (y < ty2) {
                // Top half
                x_start = tx1 + (tx2 - tx1) * (y - ty1) / (ty2 - ty1);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            } else {
                // Bottom half
                x_start = tx2 + (tx3 - tx2) * (y - ty2) / (ty3 - ty2);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            }
            if (x_start > x_end) { int32_t t = x_start; x_start = x_end; x_end = t; }
            for (int32_t x = x_start; x <= x_end; x++) {
                graphics_draw_pixel(x, y, color);
            }
        }
    } else {
        // Outline triangle
        graphics_draw_line(x1, y1, x2, y2, color);
        graphics_draw_line(x2, y2, x3, y3, color);
        graphics_draw_line(x3, y3, x1, y1, color);
    }
}

static font_t* default_font = NULL; // Will be loaded once

static void dks_draw_string(char* args) {
    if (!args) { print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b>\n"); return; }
    
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    
    if (!x_str || !y_str) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b>\n"); return;
    }

    int32_t x = atoi(x_str);
    int32_t y = atoi(y_str);

    // Find the start of the quoted string
    char* text_start = strchr(args, '\"');
    if (!text_start) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b> - Text must be quoted.\n"); return;
    }
    text_start++; // Move past the opening quote

    // Find the end of the quoted string
    char* text_end = strchr(text_start, '\"');
    if (!text_end) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b> - Mismatched quotes.\n"); return;
    }
    *text_end = '\0'; // Null-terminate the string

    char* text_str = text_start;

    // Advance args pointer past the quoted string for color parsing
    char* color_args_start = text_end + 1;
    while (*color_args_start == ' ') color_args_start++; // Skip spaces

    char* r_str = strtok(color_args_start, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");

    if (!r_str || !g_str || !b_str) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b>\n"); return;
    }

    uint8_t r = atoi(r_str);
    uint8_t g = atoi(g_str);
    uint8_t b = atoi(b_str);

    if (!default_font) {
        if (graphics_load_font("default_8x16", 16, &default_font) != GRAPHICS_SUCCESS) { // Using a more specific name
            print("ERROR: Failed to load default font for draw_string. (e.g., 'default_8x16')\n");
            return;
        }
    }
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    graphics_draw_text(x, y, text_str, default_font, color);
}

static void dks_draw_window(char* args) {
    if (!args) { print("Usage: draw_window <x> <y> <width> <height> \"<title>\" <border_r> <border_g> <border_b> <fill_r> <fill_g> <fill_b>\n"); return; }

    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* width_str = strtok(NULL, " ");
    char* height_str = strtok(NULL, " ");
    
    if (!x_str || !y_str || !width_str || !height_str) {
        print("Usage: draw_window <x> <y> <width> <height> \"<title>\" <border_r> <border_g> <border_b> <fill_r> <fill_g> <fill_b>\n"); return;
    }

    int32_t x = atoi(x_str);
    int32_t y = atoi(y_str);
    uint32_t width = atoi(width_str);
    uint32_t height = atoi(height_str);

    // Find the start of the quoted title string
    char* title_start = strchr(args, '\"');
    if (!title_start) {
        print("Usage: draw_window ... \"<title>\" ... - Title must be quoted.\n"); return;
    }
    title_start++; // Move past the opening quote

    // Find the end of the quoted title string
    char* title_end = strchr(title_start, '\"');
    if (!title_end) {
        print("Usage: draw_window ... \"<title>\" ... - Mismatched quotes.\n"); return;
    }
    *title_end = '\0'; // Null-terminate the string
    char* title_str = title_start;

    // Advance args pointer past the quoted string for color parsing
    char* color_args_start = title_end + 1;
    while (*color_args_start == ' ') color_args_start++; // Skip spaces

    char* border_r_str = strtok(color_args_start, " ");
    char* border_g_str = strtok(NULL, " ");
    char* border_b_str = strtok(NULL, " ");
    char* fill_r_str = strtok(NULL, " ");
    char* fill_g_str = strtok(NULL, " ");
    char* fill_b_str = strtok(NULL, " ");

    if (!border_r_str || !border_g_str || !border_b_str || !fill_r_str || !fill_g_str || !fill_b_str) {
        print("Usage: draw_window <x> <y> <width> <height> \"<title>\" <border_r> <border_g> <border_b> <fill_r> <fill_g> <fill_b>\n"); return;
    }

    graphics_color_t border_color = graphics_make_color(atoi(border_r_str), atoi(border_g_str), atoi(border_b_str), 255);
    graphics_color_t fill_color = graphics_make_color(atoi(fill_r_str), atoi(fill_g_str), atoi(fill_b_str), 255);

    // Draw filled background
    graphics_rect_t fill_rect = {x, y, width, height};
    graphics_draw_rect(&fill_rect, fill_color, true);

    // Draw border
    graphics_rect_t border_rect = {x, y, width, height};
    graphics_draw_rect(&border_rect, border_color, false);

    // Draw title (centered at top)
    if (!default_font) { // Ensure default_font is loaded for title
        if (graphics_load_font("default_8x16", 16, &default_font) != GRAPHICS_SUCCESS) {
            print("ERROR: Failed to load default font for window title.\n");
            return;
        }
    }

    if (default_font) {
        uint32_t text_width, text_height;
        graphics_get_text_bounds(title_str, default_font, &text_width, &text_height);
        int32_t text_x = x + (width / 2) - (text_width / 2);
        int32_t text_y = y + (text_height / 2); // Roughly centered vertically
        graphics_draw_text(text_x, text_y, title_str, default_font, border_color); // Use border color for text
    } else {
        print("Warning: No font loaded to draw window title.\n");
    }
} 

static void dks_swap_buffers(char* args) {
    if (args) { print("Usage: swap_buffers\n"); return; }
    graphics_swap_buffers();
    print("Buffers swapped.\n");
}

static void dks_enable_double_buffering(char* args) {
    if (!args) { print("Usage: enable_double_buffering <true|false>\n"); return; }
    bool enable = strcmp(args, "true") == 0;
    graphics_enable_double_buffering(enable);
    print("Double buffering "); print(enable ? "enabled" : "disabled"); print(".\n");
}

static void dks_wait_vsync(char* args) {
    if (args) { print("Usage: wait_vsync\n"); return; }
    graphics_wait_for_vsync();
    print("Waited for VSync.\n");
}

static void dks_set_resolution(char* args) {
    if (!args) { print("Usage: set_resolution <width> <height> <bpp> [refresh_rate]\n"); return; }
    char* width_str = strtok(args, " ");
    char* height_str = strtok(NULL, " ");
    char* bpp_str = strtok(NULL, " ");
    char* refresh_rate_str = strtok(NULL, " ");

    if (!width_str || !height_str || !bpp_str) {
        print("Usage: set_resolution <width> <height> <bpp> [refresh_rate]\n"); return;
    }

    uint32_t width = atoi(width_str);
    uint32_t height = atoi(height_str);
    uint32_t bpp = atoi(bpp_str);
    uint32_t refresh_rate = refresh_rate_str ? atoi(refresh_rate_str) : 0; // 0 for default/don't care

    if (graphics_set_mode(width, height, bpp, refresh_rate) == GRAPHICS_SUCCESS) {
        print("Resolution set to "); print_dec(width); print("x"); print_dec(height);
        print(" @ "); print_dec(bpp); print("bpp.\n");
    } else {
        print("ERROR: Failed to set resolution.\n");
    }
}

// Enhanced readStr with arrow key support for history and cursor movement
static string dks_readStr_enhanced(void) {
    #define KB_BUFFER_MAX 256
    string buffstr = (string)malloc(KB_BUFFER_MAX);
    if (!buffstr) {
        return NULL;
    }

    buffstr[0] = '\0';
    int len = 0;
    int cursor_pos = 0;
    history_nav_index = -1; // Reset history navigation
    char* current_edit = NULL; // Buffer for editing current line

    bool interrupts_were_enabled = irq_are_enabled();
    if (!interrupts_were_enabled) {
        irq_enable_safe();
    }

    while (1) {
        char ch = 0;
        bool have_char = keyboard_poll_char(&ch);

        if (!have_char) {
            __asm__ __volatile__("hlt");
            continue;
        }

        // Handle ANSI escape sequences for arrow keys
        static enum { NORMAL, ESC, ESC_BRACKET } esc_state = NORMAL;
        static int esc_pos = 0;
        
        if (esc_state == NORMAL && ch == 0x1B) { // ESC
            esc_state = ESC;
            esc_pos = 1;
            continue;
        }
        
        if (esc_state == ESC) {
            if (ch == '[') {
                esc_state = ESC_BRACKET;
                esc_pos = 2;
                continue;
            } else {
                esc_state = NORMAL;
                esc_pos = 0;
            }
        }
        
        if (esc_state == ESC_BRACKET) {
            esc_pos++;
            if (esc_pos >= 3 || (ch >= 'A' && ch <= 'D')) {
                // Process arrow key
                esc_state = NORMAL;
                esc_pos = 0;
                
                if (ch == 'A') { // Up arrow
                    if (history_count > 0) {
                        if (history_nav_index == -1) {
                            if (len > 0) {
                                current_edit = strdup(buffstr);
                            }
                            history_nav_index = history_count - 1;
                        } else if (history_nav_index > 0) {
                            history_nav_index--;
                        }
                        
                        int start = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
                        int idx = (start + history_nav_index) % HISTORY_SIZE;
                        if (history_buffer[idx]) {
                            for (int i = 0; i < len; i++) {
                                printch('\b');
                                printch(' ');
                                printch('\b');
                            }
                            strcpy(buffstr, history_buffer[idx]);
                            len = strlen(buffstr);
                            cursor_pos = len;
                            print(buffstr);
                        }
                    }
                    continue;
                }
                
                if (ch == 'B') { // Down arrow
                    if (history_nav_index >= 0) {
                        if (history_nav_index < history_count - 1) {
                            history_nav_index++;
                            int start = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
                            int idx = (start + history_nav_index) % HISTORY_SIZE;
                            if (history_buffer[idx]) {
                                for (int i = 0; i < len; i++) {
                                    printch('\b');
                                    printch(' ');
                                    printch('\b');
                                }
                                strcpy(buffstr, history_buffer[idx]);
                                len = strlen(buffstr);
                                cursor_pos = len;
                                print(buffstr);
                            }
                        } else {
                            history_nav_index = -1;
                            for (int i = 0; i < len; i++) {
                                printch('\b');
                                printch(' ');
                                printch('\b');
                            }
                            if (current_edit) {
                                strcpy(buffstr, current_edit);
                                len = strlen(buffstr);
                                cursor_pos = len;
                                print(buffstr);
                                free(current_edit);
                                current_edit = NULL;
                            } else {
                                buffstr[0] = '\0';
                                len = 0;
                                cursor_pos = 0;
                            }
                        }
                    }
                    continue;
                }
                
                if (ch == 'C') { // Right arrow
                    if (cursor_pos < len) {
                        cursor_pos++;
                        printch(buffstr[cursor_pos - 1]);
                        printch('\b');
                    }
                    continue;
                }
                
                if (ch == 'D') { // Left arrow
                    if (cursor_pos > 0) {
                        cursor_pos--;
                        printch('\b');
                    }
                    continue;
                }
            }
            continue;
        }

        // Handle regular characters
        
        if (ch == '\r' || ch == '\n') {
            printch('\n');
            buffstr[len] = '\0';
            if (current_edit) {
                free(current_edit);
                current_edit = NULL;
            }
            break;
        }

        if (ch == '\b' || ch == 0x7F) { // Backspace or Delete
            if (cursor_pos > 0) {
                // Shift characters left
                for (int i = cursor_pos - 1; i < len; i++) {
                    buffstr[i] = buffstr[i + 1];
                }
                len--;
                cursor_pos--;
                // Redraw line
                printch('\b');
                for (int i = cursor_pos; i < len; i++) {
                    printch(buffstr[i]);
                }
                printch(' '); // Clear last character
                // Move cursor back
                for (int i = cursor_pos; i <= len; i++) {
                    printch('\b');
                }
            }
            continue;
        }

        // Ctrl+C
        if (ch == 0x03) {
            print("\n^C\n");
            if (current_edit) {
                free(current_edit);
                current_edit = NULL;
            }
            buffstr[0] = '\0';
            break;
        }

        // Insert character at cursor
        if (len < KB_BUFFER_MAX - 1 && ch >= 32 && ch < 127) {
            // Shift characters right
            for (int i = len; i > cursor_pos; i--) {
                buffstr[i] = buffstr[i - 1];
            }
            buffstr[cursor_pos] = ch;
            len++;
            cursor_pos++;
            // Redraw from cursor position
            for (int i = cursor_pos - 1; i < len; i++) {
                printch(buffstr[i]);
            }
            // Move cursor back to correct position
            for (int i = cursor_pos; i < len; i++) {
                printch('\b');
            }
            buffstr[len] = '\0';
        }
    }

    if (!interrupts_were_enabled) {
        irq_disable_safe();
    }

    if (current_edit) {
        free(current_edit);
    }

    return buffstr;
}

// Additional draw commands
static void dks_draw_ellipse(char* args) {
    if (!args) { print("Usage: draw_ellipse <center_x> <center_y> <radius_x> <radius_y> <r> <g> <b> [filled]\n"); return; }
    char* cx_str = strtok(args, " ");
    char* cy_str = strtok(NULL, " ");
    char* rx_str = strtok(NULL, " ");
    char* ry_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    
    if (!cx_str || !cy_str || !rx_str || !ry_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_ellipse <center_x> <center_y> <radius_x> <radius_y> <r> <g> <b> [filled]\n"); return;
    }
    
    int32_t cx = atoi(cx_str);
    int32_t cy = atoi(cy_str);
    uint32_t rx = atoi(rx_str);
    uint32_t ry = atoi(ry_str);
    uint8 r = atoi(r_str);
    uint8 g = atoi(g_str);
    uint8 b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    // Simple ellipse drawing using midpoint algorithm
    if (filled) {
        for (int32_t y = -ry; y <= (int32_t)ry; y++) {
            for (int32_t x = -rx; x <= (int32_t)rx; x++) {
                // Check if point is inside ellipse: (x/rx)^2 + (y/ry)^2 <= 1
                int64_t dx = (int64_t)x * (int64_t)x * (int64_t)ry * (int64_t)ry;
                int64_t dy = (int64_t)y * (int64_t)y * (int64_t)rx * (int64_t)rx;
                int64_t r2 = (int64_t)rx * (int64_t)rx * (int64_t)ry * (int64_t)ry;
                if (dx + dy <= r2) {
                    graphics_draw_pixel(cx + x, cy + y, color);
                }
            }
        }
    } else {
        // Outline ellipse using parametric equation
        for (int angle = 0; angle < 360; angle++) {
            int32_t x = cx + (int32_t)(rx * dks_cos(angle * 3.14159 / 180.0));
            int32_t y = cy + (int32_t)(ry * dks_sin(angle * 3.14159 / 180.0));
            graphics_draw_pixel(x, y, color);
        }
    }
    print("Ellipse drawn.\n");
}

static void dks_draw_polygon(char* args) {
    if (!args) { print("Usage: draw_polygon <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return; }
    // Simple implementation - draw triangle for now
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* x3_str = strtok(NULL, " ");
    char* y3_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    
    if (!x1_str || !y1_str || !x2_str || !y2_str || !x3_str || !y3_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_polygon <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return;
    }
    
    int32_t x1 = atoi(x1_str), y1 = atoi(y1_str);
    int32_t x2 = atoi(x2_str), y2 = atoi(y2_str);
    int32_t x3 = atoi(x3_str), y3 = atoi(y3_str);
    uint8 r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    if (filled) {
        // Simple filled triangle using scanline algorithm
        // Sort vertices by y
        int32_t tx1 = x1, ty1 = y1, tx2 = x2, ty2 = y2, tx3 = x3, ty3 = y3;
        // Bubble sort by y
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        if (ty2 > ty3) { int32_t t = tx2; tx2 = tx3; tx3 = t; t = ty2; ty2 = ty3; ty3 = t; }
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        
        // Draw filled triangle
        for (int32_t y = ty1; y <= ty3; y++) {
            int32_t x_start, x_end;
            if (y < ty2) {
                // Top half
                x_start = tx1 + (tx2 - tx1) * (y - ty1) / (ty2 - ty1);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            } else {
                // Bottom half
                x_start = tx2 + (tx3 - tx2) * (y - ty2) / (ty3 - ty2);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            }
            if (x_start > x_end) { int32_t t = x_start; x_start = x_end; x_end = t; }
            for (int32_t x = x_start; x <= x_end; x++) {
                graphics_draw_pixel(x, y, color);
            }
        }
    } else {
        graphics_draw_line(x1, y1, x2, y2, color);
        graphics_draw_line(x2, y2, x3, y3, color);
        graphics_draw_line(x3, y3, x1, y1, color);
    }
    print("Polygon drawn.\n");
}

static void dks_draw_gradient(char* args) {
    if (!args) { print("Usage: draw_gradient <x> <y> <width> <height> <r1> <g1> <b1> <r2> <g2> <b2> [vertical]\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* r1_str = strtok(NULL, " ");
    char* g1_str = strtok(NULL, " ");
    char* b1_str = strtok(NULL, " ");
    char* r2_str = strtok(NULL, " ");
    char* g2_str = strtok(NULL, " ");
    char* b2_str = strtok(NULL, " ");
    char* vertical_str = strtok(NULL, " ");
    
    if (!x_str || !y_str || !w_str || !h_str || !r1_str || !g1_str || !b1_str || !r2_str || !g2_str || !b2_str) {
        print("Usage: draw_gradient <x> <y> <width> <height> <r1> <g1> <b1> <r2> <g2> <b2> [vertical]\n"); return;
    }
    
    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t w = atoi(w_str), h = atoi(h_str);
    uint8 r1 = atoi(r1_str), g1 = atoi(g1_str), b1 = atoi(b1_str);
    uint8 r2 = atoi(r2_str), g2 = atoi(g2_str), b2 = atoi(b2_str);
    bool vertical = vertical_str && strcmp(vertical_str, "vertical") == 0;
    
    if (vertical) {
        for (uint32_t i = 0; i < h; i++) {
            uint8 r = r1 + (r2 - r1) * i / h;
            uint8 g = g1 + (g2 - g1) * i / h;
            uint8 b = b1 + (b2 - b1) * i / h;
            graphics_color_t color = graphics_make_color(r, g, b, 255);
            graphics_draw_line(x, y + i, x + w - 1, y + i, color);
        }
    } else {
        for (uint32_t i = 0; i < w; i++) {
            uint8 r = r1 + (r2 - r1) * i / w;
            uint8 g = g1 + (g2 - g1) * i / w;
            uint8 b = b1 + (b2 - b1) * i / w;
            graphics_color_t color = graphics_make_color(r, g, b, 255);
            graphics_draw_line(x + i, y, x + i, y + h - 1, color);
        }
    }
    print("Gradient drawn.\n");
}

static void dks_draw_image(char* args) {
    if (!args) { print("Usage: draw_image <path> <x> <y> [scale_width] [scale_height]\n"); return; }
    char* path = strtok(args, " ");
    char* x_str = strtok(NULL, " ");
    char* y_str = strtok(NULL, " ");
    char* sw_str = strtok(NULL, " ");
    char* sh_str = strtok(NULL, " ");
    
    if (!path || !x_str || !y_str) {
        print("Usage: draw_image <path> <x> <y> [scale_width] [scale_height]\n"); return;
    }
    
    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t sw = sw_str ? atoi(sw_str) : 0;
    uint32_t sh = sh_str ? atoi(sh_str) : 0;
    
    bmp_image_t* image = NULL;
    bmp_result_t result = bmp_load_from_file(path, &image);
    if (result == BMP_SUCCESS && image) {
        if (sw > 0 && sh > 0) {
            bmp_draw_image_scaled(image, x, y, sw, sh);
        } else {
            bmp_draw_image(image, x, y);
        }
        bmp_free(image);
        print("Image drawn.\n");
    } else {
        print("Failed to load image: "); print(path); print("\n");
    }
}

static void dks_draw_rounded_rect(char* args) {
    if (!args) { print("Usage: draw_rounded_rect <x> <y> <width> <height> <radius> <r> <g> <b> [filled]\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* rad_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    
    if (!x_str || !y_str || !w_str || !h_str || !rad_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_rounded_rect <x> <y> <width> <height> <radius> <r> <g> <b> [filled]\n"); return;
    }
    
    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t w = atoi(w_str), h = atoi(h_str);
    uint32_t radius = atoi(rad_str);
    uint8 r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    // Simple rounded rect - draw main rect and rounded corners
    if (filled) {
        // Fill main rectangle
        graphics_rect_t rect = {x + radius, y, w - 2*radius, h};
        graphics_draw_rect(&rect, color, true);
        rect = (graphics_rect_t){x, y + radius, w, h - 2*radius};
        graphics_draw_rect(&rect, color, true);
        // Fill corner circles
        for (uint32_t i = 0; i < radius; i++) {
            for (uint32_t j = 0; j < radius; j++) {
                if (i*i + j*j <= radius*radius) {
                    graphics_draw_pixel(x + radius - i, y + radius - j, color);
                    graphics_draw_pixel(x + w - radius + i, y + radius - j, color);
                    graphics_draw_pixel(x + radius - i, y + h - radius + j, color);
                    graphics_draw_pixel(x + w - radius + i, y + h - radius + j, color);
                }
            }
        }
    } else {
        // Draw outline
        graphics_draw_line(x + radius, y, x + w - radius, y, color);
        graphics_draw_line(x + radius, y + h, x + w - radius, y + h, color);
        graphics_draw_line(x, y + radius, x, y + h - radius, color);
        graphics_draw_line(x + w, y + radius, x + w, y + h - radius, color);
        // Draw rounded corners
        for (int angle = 0; angle < 90; angle++) {
            int32_t dx = (int32_t)(radius * dks_cos(angle * 3.14159 / 180.0));
            int32_t dy = (int32_t)(radius * dks_sin(angle * 3.14159 / 180.0));
            graphics_draw_pixel(x + radius - dx, y + radius - dy, color);
            graphics_draw_pixel(x + w - radius + dx, y + radius - dy, color);
            graphics_draw_pixel(x + radius - dx, y + h - radius + dy, color);
            graphics_draw_pixel(x + w - radius + dx, y + h - radius + dy, color);
        }
    }
    print("Rounded rectangle drawn.\n");
}

static void dks_draw_arc(char* args) {
    if (!args) { print("Usage: draw_arc <center_x> <center_y> <radius> <start_angle> <end_angle> <r> <g> <b>\n"); return; }
    char* cx_str = strtok(args, " ");
    char* cy_str = strtok(NULL, " ");
    char* rad_str = strtok(NULL, " ");
    char* sa_str = strtok(NULL, " ");
    char* ea_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    
    if (!cx_str || !cy_str || !rad_str || !sa_str || !ea_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_arc <center_x> <center_y> <radius> <start_angle> <end_angle> <r> <g> <b>\n"); return;
    }
    
    int32_t cx = atoi(cx_str), cy = atoi(cy_str);
    uint32_t radius = atoi(rad_str);
    int start_angle = atoi(sa_str), end_angle = atoi(ea_str);
    uint8 r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    for (int angle = start_angle; angle <= end_angle; angle++) {
        int32_t x = cx + (int32_t)(radius * dks_cos(angle * 3.14159 / 180.0));
        int32_t y = cy + (int32_t)(radius * dks_sin(angle * 3.14159 / 180.0));
        graphics_draw_pixel(x, y, color);
    }
    print("Arc drawn.\n");
}


void dks_run(void) {
    print("\n[DKS] Direct Kernel Shell online. Type 'help' for commands.\n");

    while (1) {
        char prompt[MAX_CWD_LEN + 6];
        strcpy(prompt, "dks:/");
        strcat(prompt, dks_cwd);
        strcat(prompt, "> ");
        print(prompt);
        
        string input_str_const = dks_readStr_enhanced();
        if (!input_str_const) {
            print("[DKS] Input error.\n");
            continue;
        }
        
        char* input_str = strdup(input_str_const);
        add_to_history(input_str);

        char* command = strtok(input_str, " ");
        char* args = strtok(NULL, "");

        if (!command || strlen(command) == 0) {
            free(input_str);
            continue;
        }

        if (strcmp(command, "help") == 0) dks_print_help();
        else if (strcmp(command, "mem") == 0) dks_dump_memory();
        else if (strcmp(command, "cls") == 0 || strcmp(command, "clear") == 0) {
            if (tty_is_ready()) {
                tty_clear();
            } else {
                graphics_clear_screen(COLOR_BLACK);
            }
        }
        else if (strcmp(command, "ls") == 0) dks_ls(args);
        else if (strcmp(command, "cat") == 0) dks_cat(args);
        else if (strcmp(command, "run") == 0) dks_run_program(args);
        else if (strcmp(command, "tui") == 0) dks_tui_demo();
        else if (strcmp(command, "cpuid") == 0) dks_show_cpuid();
        else if (strcmp(command, "panic") == 0) kernel_panic("User-triggered test panic");
        else if (strcmp(command, "shell") == 0) {
            if (!shell_launch_embedded()) print("[DKS] Shell launch failed.\n");
            else return;
        } else if (strcmp(command, "halt") == 0) {
            print("[DKS] Halting CPU.\n");
            __asm__ __volatile__("cli; hlt");
        } else if (strcmp(command, "shutdown") == 0) {
            if (!power_shutdown()) print("[DKS] Shutdown failed.\n");
        } else if (strcmp(command, "reboot") == 0) {
            if (!power_reboot()) print("[DKS] Reboot failed.\n");
        } else if (strcmp(command, "cd") == 0) dks_cd(args);
        else if (strcmp(command, "pwd") == 0) dks_pwd();
        else if (strcmp(command, "ps") == 0) dks_ps();
        else if (strcmp(command, "kill") == 0) dks_kill(args);
        else if (strcmp(command, "head") == 0) dks_head(args);
        else if (strcmp(command, "tail") == 0) dks_tail(args);
        else if (strcmp(command, "wc") == 0) dks_wc(args);
        else if (strcmp(command, "echo") == 0) { if(args) print(args); print("\n");}
        else if (strcmp(command, "whoami") == 0) print("root\n");
        else if (strcmp(command, "sleep") == 0) { if(args) sleep_interruptible(atoi(args)); }
        else if (strcmp(command, "history") == 0) dks_print_history();
        else if (strcmp(command, "wait") == 0) { if(args) dks_wait(atoi(args)); }
        else if (strcmp(command, "pixel") == 0) dks_pixel(args);
        else if (strcmp(command, "rect") == 0) dks_rect(args);
        else if (strcmp(command, "line") == 0) dks_line(args);
        else if (strcmp(command, "mouse") == 0) dks_mouse_pointer(args);
        else if (strcmp(command, "fill_screen") == 0) dks_fill_screen(args);
        else if (strcmp(command, "clear_area") == 0) dks_clear_area(args);
        else if (strcmp(command, "circle") == 0) dks_draw_circle(args);
        else if (strcmp(command, "triangle") == 0) dks_draw_triangle(args);
        else if (strcmp(command, "draw_string") == 0) dks_draw_string(args);
        else if (strcmp(command, "draw_window") == 0) dks_draw_window(args);
        else if (strcmp(command, "swap_buffers") == 0) dks_swap_buffers(args);
        else if (strcmp(command, "enable_double_buffering") == 0) dks_enable_double_buffering(args);
        else if (strcmp(command, "wait_vsync") == 0) dks_wait_vsync(args);
        else if (strcmp(command, "set_resolution") == 0) dks_set_resolution(args);
        else if (strcmp(command, "draw_ellipse") == 0) dks_draw_ellipse(args);
        else if (strcmp(command, "draw_polygon") == 0) dks_draw_polygon(args);
        else if (strcmp(command, "draw_gradient") == 0) dks_draw_gradient(args);
        else if (strcmp(command, "draw_image") == 0) dks_draw_image(args);
        else if (strcmp(command, "draw_rounded_rect") == 0) dks_draw_rounded_rect(args);
        else if (strcmp(command, "draw_arc") == 0) dks_draw_arc(args);
        else {
            print("[DKS] Unknown command '");
            print(command);
            print("'. Type 'help'.\n");
        }
        free(input_str);
    }
}
