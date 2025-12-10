#define _GNU_SOURCE
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/select.h>
#include <poll.h>
#include <alsa/asoundlib.h>
#include <ncursesw/ncurses.h>
#include <locale.h>
#include <dirent.h>
#include <wchar.h>
#include <sys/stat.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <libgen.h>
#include <time.h>
#include <fcntl.h>

int navigate_dir(const char *target_dir, char *status_buf);
static void display_message(int type, const char *fmt, ...);
void draw_file_list(WINDOW *win);
void draw_help(WINDOW *win, int start_index);

#define SCROLL_FILLED L'█'
#define SCROLL_EMPTY L'▒'
#define COLOR_PAIR_BORDER 1
#define COLOR_PAIR_RED 4
#define COLOR_PAIR_BLUE 5
#define COLOR_PAIR_YELLOW 6
#define COLOR_PAIR_WHITE 7
#define PATH_MAX 4096
#define MIN_WIDTH 84
#define MIN_HEIGHT 21
#define MAX_NAME_LEN 76
#define _XOPEN_SOURCE 700
#define CURSOR_WIDTH 77
#define FILE_LIST_FIXED_WIDTH 82
#define OUTER_FRAME_WIDTH 84
#define INNER_WIDTH 82
#define COLOR_PAIR_PROGRESS 8
#define ERROR  1
#define STATUS 2
#define FADE_STEPS 24
#define CHANNELS 2
#define RATE 44100
#define FRAME_SIZE (CHANNELS * 2)
#define BUFFER_FRAMES 122
#define STATUS_DURATION_SECONDS 5

static void print_formatted_time(WINDOW *win, int y, int x, int seconds) {
    if (seconds < 0) {
        mvwprintw(win, y, x, "--:--:--");
        return;
    }
    int hours = seconds / 3600;
    int mins = (seconds % 3600) / 60;
    int secs = seconds % 60;
    mvwprintw(win, y, x, "%02d:%02d:%02d", hours, mins, secs);
}
static void draw_progress_bar(WINDOW *win, int y, int start_x, double percent, int bar_length) {
    int filled = (int)((percent / 100.0) * bar_length + 0.5);
    wchar_t fill_char = SCROLL_FILLED;
    wchar_t empty_char = SCROLL_EMPTY;

    if (percent > 0.0) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
    } else {
        wattron(win, COLOR_PAIR(COLOR_PAIR_WHITE));
    }
    for (int i = 0; i < bar_length; i++) {
        wmove(win, y, start_x + i);
        waddnwstr(win, &empty_char, 1);
    }
    if (percent > 0.0) {
        wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
    } else {
        wattroff(win, COLOR_PAIR(COLOR_PAIR_WHITE));
    }

    if (filled > 0) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_PROGRESS) | A_BOLD);
        for (int i = 0; i < filled; i++) {
            wmove(win, y, start_x + i);
            waddnwstr(win, &fill_char, 1);
        }
        wattroff(win, COLOR_PAIR(COLOR_PAIR_PROGRESS) | A_BOLD);
    }
}
static char status_msg[256] = "";
static int show_status = 0;
static time_t status_start_time = 0;
static inline void clear_rect(WINDOW *win, int start_y, int end_y, int start_x, int end_x) {
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            mvwaddch(win, y, x, ' ');
        }
    }
}
typedef struct {
    char *name;
    int is_dir;
} FileEntry;
FileEntry *file_list = NULL;
int file_count = 0;
int selected_index = 0;
char current_dir[PATH_MAX];
char root_dir[PATH_MAX] = {0};
WINDOW *list_win = NULL;
int term_height, term_width;
static inline void refresh_ui(void)
{
    draw_file_list(list_win);
    wnoutrefresh(stdscr);
    wnoutrefresh(list_win);
    doupdate();
}
char **forward_history = NULL;
int forward_count = 0;
int forward_capacity = 10;
__attribute__((unused)) static char *xasprintf(const char *fmt, ...)
{
    char *res = NULL;
    va_list ap;
    va_start(ap, fmt);
    if (vasprintf(&res, fmt, ap) == -1) {
        res = NULL;
    }
    va_end(ap);
    return res;
}

static void free_str_array(char **arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) {
        if (arr[i]) free(arr[i]);
    }
    free(arr);
}

void init_ncurses(const char *locale) {
    setlocale(LC_ALL, locale);
    initscr();
    cbreak();
    noecho();
    use_default_colors();
    keypad(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        init_pair(COLOR_PAIR_BORDER, COLOR_GREEN, COLOR_BLACK);
        init_pair(2, COLOR_CYAN, COLOR_BLACK);
        init_pair(3, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(COLOR_PAIR_RED, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_PAIR_BLUE, COLOR_BLUE, COLOR_BLACK);
        init_pair(COLOR_PAIR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_WHITE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PAIR_PROGRESS, COLOR_GREEN, COLOR_BLACK);
    }
}
int set_alsa_params(snd_pcm_t *handle, unsigned int rate, int channels) {
    if (!handle) {
        fprintf(stderr, "Invalid handle in set_alsa_params\n");
        return -1;
    }
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_any(handle, params) < 0) {
        fprintf(stderr, "snd_pcm_hw_params_any failed\n");
        return -1;
    }
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, channels);
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0);
    snd_pcm_uframes_t period_size = 256;
    snd_pcm_hw_params_set_period_size_near(handle, params, &period_size, 0);
    snd_pcm_uframes_t buffer_size_frames = 1024;
    snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_size_frames);
    int ret = snd_pcm_hw_params(handle, params);
    if (ret < 0) {
        fprintf(stderr, "snd_pcm_hw_params failed: %s — аудио отключено\n", snd_strerror(ret));
        return -1;
    }
    return 0;
}
void play_audio(snd_pcm_t *handle, char *buffer, int size) {
    if (!handle || !buffer || size <= 0) {
        fprintf(stderr, "Invalid params in play_audio\n");
        return;
    }
    snd_pcm_uframes_t frames = size / 4;
    char *ptr = buffer;
    snd_pcm_uframes_t remaining = frames;
    while (remaining > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(handle, ptr, remaining);
        if (written < 0) {
            if (written == -EPIPE) {
                if (snd_pcm_prepare(handle) < 0) {
                    fprintf(stderr, "snd_pcm_prepare failed after EPIPE\n");
                    snd_pcm_drop(handle);
                    return;
                }
            } else {
                fprintf(stderr, "snd_pcm_writei failed: %s\n", snd_strerror(written));
                snd_pcm_drop(handle);
                return;
            }
            continue;
        }
        remaining -= written;
        ptr += written * 4;
    }
}

FILE* open_audio_file(const char *filename) {
    if (!filename) {
        fprintf(stderr, "Invalid filename in open_audio_file\n");
        return NULL;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        display_message(ERROR, "Failed to open file: %s", filename);
        return NULL;
    }

    return file;
}
static void full_cleanup(FILE **file, snd_pcm_t **handle, struct pollfd **poll_fds, char **current_filename) {
    if (*file) fclose(*file);
    if (*handle) {
        snd_pcm_drop(*handle);
        snd_pcm_drain(*handle);
        snd_pcm_close(*handle);
    }
    if (file) *file = NULL;
    if (handle) *handle = NULL;
    if (poll_fds && *poll_fds) {
        free(*poll_fds);
        *poll_fds = NULL;
    }
    if (current_filename && *current_filename) {
        free(*current_filename);
        *current_filename = NULL;
    }
}
snd_pcm_t* init_audio_device(unsigned int rate, int channels) {
    snd_pcm_t *handle = NULL;
    int ret = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
        fprintf(stderr, "Ошибка открытия устройства ALSA: %s — аудио отключено\n", snd_strerror(ret));
        return NULL;
    }
    if (set_alsa_params(handle, rate, channels) < 0) {
        fprintf(stderr, "set_alsa_params failed — аудио отключено\n");
        snd_pcm_close(handle);
        return NULL;
    }
    ret = snd_pcm_prepare(handle);
    if (ret < 0) {
        fprintf(stderr, "snd_pcm_prepare failed: %s — аудио отключено\n", snd_strerror(ret));
        snd_pcm_close(handle);
        return NULL;
    }
    return handle;
}
static void draw_single_frame(WINDOW *win, int start_y, int height, const char *title, int line_type);
void draw_outer_frame(WINDOW *win, int rows, int cols, int headers_only) {
if (cols < MIN_WIDTH) {
    return;
}
    if (headers_only) {
        return;
    }
draw_single_frame(win, 0, rows, "GRANNIK | COMPLEX SOFTWARE ECOSYSTEM | FILE NAVIGATOR RAW", 1);
    wnoutrefresh(win);
    doupdate();
}
static void draw_fill_line(WINDOW *win, int start_y, int start_x, int length, const void *symbol, int is_horizontal, int is_wide) {
    for (int i = 0; i < length; i++) {
        if (is_horizontal) {
            if (is_wide) {
                mvwaddnwstr(win, start_y, start_x + i, (const wchar_t *)symbol, 1);
            } else {
                mvwprintw(win, start_y, start_x + i, "%s", (const char *)symbol);
            }
        } else {
            if (is_wide) {
                mvwaddnwstr(win, start_y + i, start_x, (const wchar_t *)symbol, 1);
            } else {
                mvwprintw(win, start_y + i, start_x, "%s", (const char *)symbol);
            }
        }
    }
}
static void draw_single_frame(WINDOW *win, int start_y, int height, const char *title, int line_type)
{
    int max_y, max_x;
    getmaxyx(win, max_y, max_x);
    (void)max_y;
    int fixed_width = line_type ? OUTER_FRAME_WIDTH : FILE_LIST_FIXED_WIDTH;
    int actual_width = (max_x < fixed_width) ? max_x : fixed_width;
    wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
    const char *top_left = line_type ? "╔" : "┌";
    const char *top_right = line_type ? "╗" : "┐";
    const char *bottom_left = line_type ? "╚" : "└";
    const char *bottom_right = line_type ? "╝" : "┘";
    const char *horizontal = line_type ? "═" : "─";
    const char *vertical = line_type ? "║" : "│";
    const char *title_left = line_type ? "╡" : "┤";
    const char *title_right = line_type ? "╞" : "├";
    mvwprintw(win, start_y, 0, "%s", top_left);
    draw_fill_line(win, start_y, 1, actual_width - 2, horizontal, 1, 0);
    mvwprintw(win, start_y, actual_width - 1, "%s", top_right);
    int title_len = strlen(title) + 4;
    int title_start = (actual_width - title_len) / 2;
    if (title_start < 1) title_start = 1;
    if (title_start + title_len > actual_width - 1) title_start = actual_width - title_len - 1;
    mvwprintw(win, start_y, title_start, "%s %s %s", title_left, title, title_right);
    draw_fill_line(win, start_y + 1, 0, height - 2, vertical, 0, 0);
    draw_fill_line(win, start_y + 1, actual_width - 1, height - 2, vertical, 0, 0);
    mvwprintw(win, start_y + height - 1, 0, "%s", bottom_left);
    draw_fill_line(win, start_y + height - 1, 1, actual_width - 2, horizontal, 1, 0);
    mvwprintw(win, start_y + height - 1, actual_width - 1, "%s", bottom_right);
    wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
}

typedef struct PlayerControl {
    char *filename;
    int pause;
    int stop;
    int quit;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    FILE *current_file;
    char *current_filename;
    int paused;
    char **playlist;
    int playlist_size;
    int playlist_capacity;
    int current_track;
    int playlist_mode;
    int    seek_delta;
    double duration;
    long   bytes_read;
    int is_silent;
    int fading_out;
    int fading_in;
    int current_fade;
    char *playlist_dir;
    int loop_mode;
} PlayerControl;
PlayerControl player_control;
void top(WINDOW *win)
{
    draw_single_frame(win, 0, 3, "PATH", 0);
    char path_display[256];
    strncpy(path_display, current_dir, sizeof(path_display) - 1);
    path_display[sizeof(path_display) - 1] = '\0';
	    pthread_mutex_lock(&player_control.mutex);
	    int is_playlist_active = player_control.playlist_mode &&
	                             player_control.playlist_dir != NULL &&
	                             strcmp(player_control.playlist_dir, current_dir) == 0;
	    pthread_mutex_unlock(&player_control.mutex);
    if (is_playlist_active) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_BLUE));
    }
    mvwprintw(win, 1, 2, "%s", path_display);
    if (is_playlist_active) {
        wattroff(win, COLOR_PAIR(COLOR_PAIR_BLUE));
    }
}
void draw_menu_frame(WINDOW *win)
{
    int max_y;
    getmaxyx(win, max_y, term_width);
    int usable_height = max_y - 6;
    if (usable_height < 3) usable_height = 3;

draw_single_frame(win, 3, usable_height, "FILES & DIRECTORIES", 0);
}
void draw_help_frame(WINDOW *win)
{
    int max_y;
    getmaxyx(win, max_y, term_width);
    int offset_y = 3;
    int usable_height = max_y - offset_y - 3;
    if (usable_height < 3) usable_height = 3;

draw_single_frame(win, offset_y, usable_height, "HELP", 0);
}
void perform_seek(PlayerControl *control, snd_pcm_t *handle);
PlayerControl player_control = {
    .filename = NULL, .pause = 0, .stop = 0, .quit = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER,
    .current_file = NULL, .current_filename = NULL, .paused = 0,
    .loop_mode = 0,
    .playlist = NULL, .playlist_size = 0, .current_track = 0, .playlist_mode = 0,
.seek_delta = 0, .duration = 0.0, .bytes_read = 0,
    .is_silent = 0, .fading_out = 0, .fading_in = 0, .current_fade = FADE_STEPS,
.playlist_dir = NULL,
};
static int is_raw_file(const char *name) {
    if (!name) return 0;
    size_t len = strlen(name);
    return (len >= 4) && (strcmp(name + len - 4, ".raw") == 0);
}
	char error_msg[256] = "";
	static int show_error = 0;
        static int error_toggle = 0;
	static int system_time_toggle = 0;
static void display_message(int type, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (type == ERROR) {
        vsnprintf(error_msg, sizeof(error_msg), fmt, ap);
        show_error = 1;
    } else if (type == STATUS) {
        vsnprintf(status_msg, sizeof(status_msg), fmt, ap);
        show_status = 1;
status_start_time = time(NULL);
    }
    va_end(ap);
}
static void print_centered(WINDOW *win, int y, int width, const char *text, int color_pair, int attr)
{
    int len = strlen(text);
if (len > width - 4) {
    char truncated[256];
    snprintf(truncated, width - 3, "%s", text);
    strcat(truncated, "...");
    text = truncated;
    len = strlen(text);
}
    int x = (width - len) / 2;
    if (x < 2) x = 2;
    if (x + len > width - 2) x = width - len - 2;
    wattron(win, COLOR_PAIR(color_pair) | attr);
    mvwprintw(win, y, x, "%s", text);
    wattroff(win, COLOR_PAIR(color_pair) | attr);
}

void draw_field_frame(WINDOW *win)
{
    int max_y, max_x;
    getmaxyx(win, max_y, max_x);
    int actual_width = (max_x < FILE_LIST_FIXED_WIDTH) ? max_x : FILE_LIST_FIXED_WIDTH;
    int field_y = max_y - 3;
    const char *title;
    if (show_error || show_status) {
        title = "NOTIFICATION";
    } else if (system_time_toggle) {
        title = "TIME";
    } else {
        title = "PLAYBACK TIME & PROGRESS";
    }
    draw_single_frame(win, field_y, 3, title, 0);
    if (show_error) {
        print_centered(win, field_y + 1, actual_width, error_msg, COLOR_PAIR_RED, 0);
        system_time_toggle = 0;
    } else {
        wattron(win, COLOR_PAIR(COLOR_PAIR_WHITE));
        if (system_time_toggle) {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            const char *months[] = {"January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December"};
            const char *weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
            char time_str[128];
            snprintf(time_str, sizeof(time_str), "%d | %s | %s | %02d | %02d:%02d:%02d",
                     tm_info->tm_year + 1900,
                     months[tm_info->tm_mon],
                     weekdays[tm_info->tm_wday],
                     tm_info->tm_mday,
                     tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
            print_centered(win, field_y + 1, actual_width, time_str, COLOR_PAIR_WHITE, 0);
        } else if (show_status) {
            clear_rect(win, field_y + 1, field_y + 2, 2, actual_width - 2);
            wattron(win, COLOR_PAIR(COLOR_PAIR_YELLOW));
            int status_width = actual_width - 4;
            mvwprintw(win, field_y + 1, 2, "%.*s", status_width, status_msg);
            wattroff(win, COLOR_PAIR(COLOR_PAIR_YELLOW));
        } else {
            pthread_mutex_lock(&player_control.mutex);
            double duration = player_control.duration;
            long bytes_read = player_control.bytes_read;
            pthread_mutex_unlock(&player_control.mutex);
            if (duration > 0.0) {
                long total_bytes = (long)(duration * 176400.0);
                double percent = total_bytes > 0 ? (bytes_read * 100.0) / total_bytes : 0.0;
                int elapsed_sec = (int)(bytes_read / 176400.0);
                int total_sec = (int)duration;
                print_formatted_time(win, field_y + 1, 2, elapsed_sec);
                mvwprintw(win, field_y + 1, 11, "/");
                print_formatted_time(win, field_y + 1, 13, total_sec);
                wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                mvwprintw(win, field_y + 1, 22, "|");
                wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                mvwprintw(win, field_y + 1, 24, "%3d%%", (int)(percent + 0.5));
                wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                draw_progress_bar(win, field_y + 1, 30, percent, 50);
            } else {
                print_formatted_time(win, field_y + 1, 2, -1);
                mvwprintw(win, field_y + 1, 11, "/");
                print_formatted_time(win, field_y + 1, 13, -1);
                wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                mvwprintw(win, field_y + 1, 22, "|");
                wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                mvwprintw(win, field_y + 1, 24, "  0%%");
                wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
                draw_progress_bar(win, field_y + 1, 30, 0.0, 50);
            }
        }
        wattroff(win, COLOR_PAIR(COLOR_PAIR_WHITE));
    }
}
    void free_forward_history(void) {
free_str_array(forward_history, forward_count);
    forward_history = NULL;
    forward_count = 0;
    forward_capacity = 0;
}

int add_to_forward(const char *dir) {
    if (!dir) return -1;

    if (forward_count >= forward_capacity) {
        int new_capacity = forward_capacity == 0 ? 10 : forward_capacity * 2;
        char **temp = realloc(forward_history, new_capacity * sizeof(char *));
        if (!temp) {
            return -1;
        }
        forward_history = temp;
        memset(forward_history + forward_count, 0,
               (new_capacity - forward_count) * sizeof(char *));
        forward_capacity = new_capacity;
    }
    forward_history[forward_count] = strdup(dir);
    if (!forward_history[forward_count]) {
        return -1;
    }
    forward_count++;
    return 0;
}
void free_file_list(void) {
    if (file_list) {
        for (int i = 0; i < file_count; i++) {
            if (file_list[i].name) free(file_list[i].name);
        }
        free(file_list);
    }
    file_list = NULL;
    file_count = 0;
    selected_index = 0;
}
void update_file_list(void) {
    free_file_list();
DIR *dir = opendir(current_dir);
if (!dir) {
    free_file_list();
    file_list = calloc(1, sizeof(FileEntry)); file_list[0].name = strdup("(access denied)"); file_list[0].is_dir = 0; file_count = 1;
    selected_index = 0;
    display_message(ERROR, "Access denied to: %s", current_dir);
    return;
}
    int capacity = 128;
    int count = 0;
    FileEntry *entries = calloc(capacity, sizeof(FileEntry));
    if (!entries) {
        closedir(dir);
        file_list = calloc(1, sizeof(FileEntry));
        if (file_list) {
            file_list[0].name = strdup("(out of memory)");
            file_list[0].is_dir = 0;
            file_count = file_list[0].name ? 1 : 0;
        }
        selected_index = 0;
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
	        if (count >= capacity) {
	            capacity *= 2;
	            FileEntry *tmp = realloc(entries, capacity * sizeof(FileEntry));
	            if (!tmp) {
	                for (int i = 0; i < count; i++) {
	                    free(entries[i].name);
	                }
	                free(entries);
	                entries = NULL;
	                count = 0;
	                break;
	            }
	            entries = tmp;
	        }
        char *name = strdup(entry->d_name);
        if (!name) continue;
	        char *full_path = xasprintf("%s/%s", current_dir, entry->d_name);
	        if (!full_path) {
	            free(name);
	            continue;
	        }
	        char resolved_path[PATH_MAX];
	        if (realpath(full_path, resolved_path) == NULL) {
	            display_message(ERROR, "realpath failed for: %s (errno: %d)", full_path, errno);
	            free(full_path);
	            free(name);
	            continue;
	        }
	        free(full_path);
	        full_path = strdup(resolved_path);
	        if (!full_path) {
	            free(name);
	            continue;
	        }
struct stat st;
if (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
    entries[count].is_dir = 1;
} else {
    entries[count].is_dir = 0;
}
free(full_path);
        entries[count].name = name;
        count++;
    }
    closedir(dir);
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            int a_dir = entries[i].is_dir;
            int b_dir = entries[j].is_dir;
            int cmp = strcmp(entries[i].name, entries[j].name);

            if ((a_dir && !b_dir) || (a_dir == b_dir && cmp > 0)) {
                FileEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    file_list = entries;
    file_count = count;
    selected_index = 0;
    if (file_count == 0) {
        file_list = calloc(1, sizeof(FileEntry));
        if (file_list) {
            file_list[0].name = strdup("Directory is empty or not enough memory.");
            file_list[0].is_dir = 0;
            file_count = 1;
        }
    }
}
void draw_file_list(WINDOW *win) {
    if (!win) return;
    werase(win);
    top(win);
	    int max_y = getmaxy(win);
	    int max_x = getmaxx(win);
            draw_menu_frame(win);
    wnoutrefresh(win);
    if (file_count == 0 || !file_list) {
        mvwprintw(win, 3, 1, "(empty)");
        wrefresh(win);
        return;
    }
    int visible_lines = (max_y < 12) ? 1 : max_y - 8;
    char *current_file_name = NULL;
    int current_paused = 0;

    pthread_mutex_lock(&player_control.mutex);
    const char *active_filename = player_control.current_filename;
    if (!active_filename && player_control.filename) {
        active_filename = player_control.filename;
    }
    if (active_filename && strlen(active_filename) > 0) {
        const char *slash = strrchr(active_filename, '/');
        current_file_name = strdup(slash ? slash + 1 : active_filename);
        if (current_file_name) {
            current_paused = player_control.paused;
        }
    }
char *playlist_dir = player_control.playlist_dir ? strdup(player_control.playlist_dir) : NULL;
   pthread_mutex_unlock(&player_control.mutex);
    int start_index = 0;
    int end_index = file_count;
    if (file_count > 0 && visible_lines > 0) {
        if (file_count > visible_lines) {
            start_index = selected_index - (visible_lines / 2);
            if (start_index < 0) start_index = 0;
            if (start_index > file_count - visible_lines) start_index = file_count - visible_lines;
        }
        end_index = start_index + visible_lines;
        if (end_index > file_count) end_index = file_count;
    }
	if (file_count == 1 && file_list[0].name &&
	    strcmp(file_list[0].name, "Directory is empty or not enough memory.") == 0) {
	    int msg_row = 4 + (visible_lines / 2);
	    if (msg_row < 5) msg_row = 5;
	    if (msg_row >= max_y - 5) msg_row = max_y - 6;
            wattron(win, COLOR_PAIR(COLOR_PAIR_RED));
	    const char* msg = file_list[0].name;
	    int msg_len = strlen(msg);
	    int max_msg_width = CURSOR_WIDTH - 2;
	    if (msg_len > max_msg_width) {
	        mvwprintw(win, msg_row, 3, "%.*s...", max_msg_width - 3, msg);
	    } else {
	        mvwprintw(win, msg_row, 3, "%-*s", max_msg_width, msg);
	    }
            wattroff(win, COLOR_PAIR(COLOR_PAIR_RED));
	    if (current_file_name) free(current_file_name);
	    draw_field_frame(win);
	    return;
	}
	int cursor_end = 2 + CURSOR_WIDTH;
	for (int i = start_index; i < end_index; i++) {
	    if (!file_list[i].name) continue;
	    int row = i - start_index + 4;
    wchar_t wname[CURSOR_WIDTH + 4] = {0};
    const char *src = file_list[i].name;
    mbstate_t state = {0};
    size_t wlen = mbsrtowcs(wname, &src, CURSOR_WIDTH, &state);
	    if (wlen != (size_t)-1) wname[wlen] = L'\0';
    if (wlen == (size_t)-1) {
        wname[0] = L'?';
        wname[1] = L'\0';
        wlen = 1;
    } else if (wlen >= CURSOR_WIDTH) {
        wname[CURSOR_WIDTH - 4] = L'.';
        wname[CURSOR_WIDTH - 3] = L'.';
        wname[CURSOR_WIDTH - 2] = L'.';
        wname[CURSOR_WIDTH - 1] = L'\0';
        wlen = CURSOR_WIDTH - 1;
    } else {
        wname[wlen] = L'\0';
    }
    if (file_list[i].is_dir) {
        int dirlen = wcslen(wname);
        if (dirlen < CURSOR_WIDTH - 1) {
            wname[dirlen] = L'/';
            wname[dirlen + 1] = L'\0';
        }
    }
    if (i == selected_index) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
wchar_t fill_ch1 = L'▒';
draw_fill_line(win, row, 2, cursor_end - 2, &fill_ch1, 1, 1);
        wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
        int text_color = COLOR_PAIR_BORDER;
        if (current_file_name && strcmp(file_list[i].name, current_file_name) == 0 && is_raw_file(file_list[i].name))
            text_color = current_paused ? COLOR_PAIR_YELLOW : COLOR_PAIR_BLUE;
if (file_list[i].is_dir && playlist_dir) {
    char *full_path = xasprintf("%s/%s", current_dir, file_list[i].name);
    if (full_path && strcmp(full_path, playlist_dir) == 0) {
        text_color = COLOR_PAIR_BLUE;
    }
    free(full_path);
}
        wattron(win, COLOR_PAIR(text_color));
        mvwaddwstr(win, row, 3, wname);
        wattroff(win, COLOR_PAIR(text_color));
        wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
        int printed = wcslen(wname);
wchar_t fill_ch2 = L'▒';
draw_fill_line(win, row, 3 + printed, cursor_end - (3 + printed), &fill_ch2, 1, 1);
        wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
    } else {
        int text_color = 0;
        if (current_file_name && strcmp(file_list[i].name, current_file_name) == 0 && is_raw_file(file_list[i].name))
            text_color = current_paused ? COLOR_PAIR_YELLOW : COLOR_PAIR_BLUE;
if (file_list[i].is_dir) text_color = 2;
        else if (is_raw_file(file_list[i].name)) text_color = 3;
if (file_list[i].is_dir && playlist_dir) {
    char *full_path = xasprintf("%s/%s", current_dir, file_list[i].name);
    if (full_path && strcmp(full_path, playlist_dir) == 0) {
        text_color = COLOR_PAIR_BLUE;
    }
    free(full_path);
}
        if (text_color) wattron(win, COLOR_PAIR(text_color));
        mvwaddwstr(win, row, 3, wname);
        if (text_color) wattroff(win, COLOR_PAIR(text_color));

        int printed = wcslen(wname);
wchar_t space_ch = L' ';
draw_fill_line(win, row, 3 + printed, cursor_end - (3 + printed), &space_ch, 1, 1);
    }
}
    if (file_count > visible_lines) {
        int scroll_height       = max_y - 8;
        float ratio             = (float)scroll_height / file_count;
        int scroll_bar_height   = (int)(visible_lines * ratio + 0.5f);
        if (scroll_bar_height < 1) scroll_bar_height = 1;

        int max_scroll_offset   = file_count - visible_lines;
        int scroll_pos          = 4;

        if (max_scroll_offset > 0) {
            float normalized_pos = (float)start_index / max_scroll_offset;
            scroll_pos = 4 + (int)(normalized_pos * (scroll_height - scroll_bar_height));
        }

        int bar_x = 2 + CURSOR_WIDTH + 1;
        wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
        for (int i = 4; i < max_y - 4; i++) {
            if (i >= scroll_pos && i < scroll_pos + scroll_bar_height) {
                mvwprintw(win, i, bar_x, "%lc", SCROLL_FILLED);
            } else {
                mvwprintw(win, i, bar_x, "%lc", SCROLL_EMPTY);
            }
        }

        wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
    }
if (playlist_dir) free(playlist_dir);
if (current_file_name) free(current_file_name);
int last_list_row = end_index - start_index + 4;
int safe_clear_end = max_y - 5;
if (last_list_row < safe_clear_end) {
        clear_rect(win, last_list_row, safe_clear_end, 1, max_x - 1);
}
	draw_field_frame(win);
}
void *player_thread(void *arg) {
    PlayerControl *control = (PlayerControl *)arg;
    unsigned int rate = 44100;
    int channels = 2;
    snd_pcm_t *handle = NULL;
    FILE *file = NULL;
    int local_paused = 0;
    const int buffer_size = 4096;
    char buffer[buffer_size];
    unsigned int poll_count = 0;
    struct pollfd *poll_fds = NULL;

    while (1) {
        pthread_mutex_lock(&control->mutex);
        if (control->quit) {
            pthread_mutex_unlock(&control->mutex);
            break;
        }
	if (control->stop) {
	    full_cleanup(&file, &handle, &poll_fds, &control->current_filename);
	    if (control->playlist) {
	        free_str_array(control->playlist, control->playlist_size);
	        control->playlist = NULL;
	        control->playlist_size = 0;
	        control->playlist_capacity = 0;
                free(control->playlist_dir);
                control->playlist_dir = NULL;
	    }
	    if (control->filename) {
	        free(control->filename);
	        control->filename = NULL;
	    }
	    if (control->playlist_mode) {
	        display_message(STATUS, "End of playlist reached");
	    }
	    control->stop = 0;
	    control->duration = 0.0;
	    control->bytes_read = 0;
	    control->paused = 0;
	    control->playlist_mode = 0;
	    control->current_track = 0;
	    pthread_cond_signal(&control->cond);
	    pthread_mutex_unlock(&control->mutex);
	    continue;
	}
        if (control->filename && (!control->current_filename || strcmp(control->filename, control->current_filename) != 0)) {
    full_cleanup(&file, &handle, NULL, &control->current_filename);
            free(poll_fds); poll_fds = NULL;

    file = open_audio_file(control->filename);
    if (file) {
        struct stat st;
        if (fstat(fileno(file), &st) == 0) {
            long file_size = st.st_size;
            control->duration = (double)file_size / 176400.0;
            control->bytes_read = 0;
        } else {
            control->duration = 0.0;
            control->bytes_read = 0;
        }

        handle = init_audio_device(rate, channels);
        if (!handle) {
            if (file) {
                fclose(file);
                file = NULL;
            }
            free(poll_fds); poll_fds = NULL;
            pthread_mutex_unlock(&control->mutex);
            continue;
        }
        control->current_file = file;
        char *temp_filename = strdup(control->filename);
                if (temp_filename) {
                    control->current_filename = temp_filename;
	                    control->is_silent = 0;
	                    control->fading_in = 0;
	                    control->fading_out = 0;
	                    control->current_fade = FADE_STEPS;
                    poll_count = snd_pcm_poll_descriptors_count(handle);
	perform_seek(control, handle);
 if (poll_count > 0) {
     poll_fds = malloc(poll_count * sizeof(struct pollfd));
     if (poll_fds) {
         snd_pcm_poll_descriptors(handle, poll_fds, poll_count);
     } else {
    full_cleanup(&file, &handle, NULL, NULL);
         continue;
     }
 }
                } else {
    full_cleanup(&file, &handle, NULL, NULL);
                }
            } else {
                free(control->filename);
                control->filename = NULL;
            }
        }
        pthread_mutex_unlock(&control->mutex);

	        if (handle && file && !local_paused) {
	            if (!poll_fds || poll_count <= 0) {
	                usleep(100000);
	                continue;
	            }
	            if (poll(poll_fds, poll_count, 100) < 0) continue;

            unsigned short revents;
            snd_pcm_poll_descriptors_revents(handle, poll_fds, poll_count, &revents);
            if (revents & POLLOUT) {
pthread_mutex_lock(&control->mutex);
perform_seek(control, handle);
pthread_mutex_unlock(&control->mutex);

size_t read_size;
	pthread_mutex_lock(&control->mutex);
	if (control->is_silent) {
	    memset(buffer, 0, buffer_size);
	    read_size = buffer_size;
	} else {
	    read_size = fread(buffer, 1, buffer_size, file);
	    if (read_size == 0) {
		        if (feof(file)) {
		            if (control->playlist_mode && control->current_track < control->playlist_size - 1) {
		                full_cleanup(&file, &handle, &poll_fds, &control->current_filename);
		                control->current_track++;
		                control->filename = strdup(control->playlist[control->current_track]);
		                if (!control->filename) {
		                    control->current_track = control->playlist_size;
		                }
		                pthread_mutex_unlock(&control->mutex);
		                continue;
		            } else if (control->loop_mode && !control->playlist_mode) {
		                fseek(file, 0, SEEK_SET);
		                control->bytes_read = 0;
		                control->seek_delta = 0;
		                pthread_mutex_unlock(&control->mutex);
		                continue;
		            } else {
		                if (control->playlist_mode) {
		                    display_message(STATUS, "End of playlist reached");
		                }
		                if (control->playlist) {
		                    free_str_array(control->playlist, control->playlist_size);
		                    control->playlist = NULL;
		                    control->playlist_size = 0;
		                    control->playlist_capacity = 0;
		                    free(control->playlist_dir);
		                    control->playlist_dir = NULL;
		                }
		                if (control->filename) {
		                    free(control->filename);
		                    control->filename = NULL;
		                }
		                full_cleanup(&file, &handle, &poll_fds, &control->current_filename);
		                control->playlist_mode = 0;
		                control->duration = 0.0;
		                control->bytes_read = 0;
		                pthread_mutex_unlock(&control->mutex);
		                continue;
		            }
		        } else {
		            display_message(ERROR, "File read error");
		            if (control->playlist) {
		                free_str_array(control->playlist, control->playlist_size);
		                control->playlist = NULL;
		                control->playlist_size = 0;
		                control->playlist_capacity = 0;
		                free(control->playlist_dir);
		                control->playlist_dir = NULL;
		            }
		            if (control->filename) {
		                free(control->filename);
		                control->filename = NULL;
		            }
		            full_cleanup(&file, &handle, &poll_fds, &control->current_filename);
		            control->playlist_mode = 0;
		            control->duration = 0.0;
		            control->bytes_read = 0;
		            pthread_mutex_unlock(&control->mutex);
		            continue;
		        }
	    }
	    control->bytes_read += read_size;
	}
	pthread_mutex_unlock(&control->mutex);
int actual_size = (read_size / 4) * 4;
pthread_mutex_lock(&control->mutex);
if (control->fading_out || control->fading_in) {
    int16_t *samples = (int16_t *)buffer;
    size_t num_samples = actual_size / 2;

    float factor_start = (float)control->current_fade / FADE_STEPS;
    float factor_end;

    if (control->fading_out) {
        factor_end = (float)(control->current_fade - 1) / FADE_STEPS;
        if (factor_end < 0) factor_end = 0;
        control->current_fade--;
        if (control->current_fade <= 0) {
            control->fading_out = 0;
            control->is_silent = 1;
        }
	    } else {
	        factor_end = (float)(control->current_fade + 1) / FADE_STEPS;
	        control->current_fade++;
	        if (control->current_fade >= FADE_STEPS) {
	            control->fading_in = 0;
	            control->current_fade = FADE_STEPS;
	            control->is_silent = 0;
	        }
	    }
    for (size_t i = 0; i < num_samples; i++) {
        float interp = (num_samples > 1) ? (float)i / (num_samples - 1) : 0.0f;
        float factor = factor_start + (factor_end - factor_start) * interp;
        samples[i] = (int16_t)(samples[i] * factor);
    }
}
pthread_mutex_unlock(&control->mutex);
play_audio(handle, buffer, actual_size);
            }
        } else {
            usleep(100000);
        }
    }
    full_cleanup(&file, &handle, &poll_fds, &control->current_filename);
    return NULL;
}
void perform_seek(PlayerControl *control, snd_pcm_t *handle)
	{
	    if (!control->current_file) {
	        control->seek_delta = 0;
	        return;
	    }
	    if (control->seek_delta == 0 || !control->current_file)
	        return;
	    long long bytes_per_second = 176400LL;
	    long long seek_bytes = (long long)control->seek_delta * bytes_per_second;
	    long long current_pos = ftell(control->current_file);
	    long long new_pos = current_pos + seek_bytes;
	    if (new_pos < 0)
	        new_pos = 0;
	    if (control->duration > 0.0) {
	        long long max_bytes = (long long)(control->duration * 176400.0);
	        if (new_pos > max_bytes)
	            new_pos = max_bytes;
	    }
	    new_pos = (new_pos / 4) * 4;
	    long long delta_bytes = new_pos - current_pos;
	    if (delta_bytes == 0) {
	        control->seek_delta = 0;
	        return;
	    }
	    fseek(control->current_file, new_pos, SEEK_SET);
	    control->bytes_read = new_pos;
	    if (handle) {
	        if (seek_bytes < 0) {
	            snd_pcm_drop(handle);
	        }
	        long long delta_frames = llabs(delta_bytes) / 4;
	        if (seek_bytes > 0) {
	            snd_pcm_forward(handle, delta_frames);
	        } else {
	            snd_pcm_rewind(handle, delta_frames);
	        }
	        snd_pcm_prepare(handle);
	    }
	    control->seek_delta = 0;
	}
int playlist_cmp(const void *a, const void *b) {
    const char *path_a = *(const char **)a;
    const char *name_a = strrchr(path_a, '/');
    if (name_a) {
        name_a++;
    } else {
        name_a = path_a;
    }
    const char *path_b = *(const char **)b;
    const char *name_b = strrchr(path_b, '/');
    if (name_b) {
        name_b++;
    } else {
        name_b = path_b;
    }
    return strcmp(name_a, name_b);
}
void load_playlist(const char *dir_path, PlayerControl *control) {
    pthread_mutex_lock(&control->mutex);
    control->stop = 1;
    pthread_cond_signal(&control->cond);
    pthread_mutex_unlock(&control->mutex);
    pthread_mutex_lock(&control->mutex);
	    struct timespec ts;
	    clock_gettime(CLOCK_REALTIME, &ts);
	    ts.tv_sec += 3;

	    while (control->stop == 1) {
	        int ret = pthread_cond_timedwait(&control->cond, &control->mutex, &ts);
	        if (ret == ETIMEDOUT) {
	            fprintf(stderr, "Предупреждение: аудиопоток не отвечает — продолжаем без ожидания\n");
	            break;
	        }
	    }
    if (control->playlist) {
        free(control->playlist_dir);
        control->playlist_dir = NULL;
        free_str_array(control->playlist, control->playlist_size);
        control->playlist = NULL;
        control->playlist_size = 0;
        control->current_track = 0;
    }
    DIR *dir = opendir(dir_path);
    if (!dir) {
        control->playlist_mode = 0;
        pthread_mutex_unlock(&control->mutex);
        return;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
       if (entry->d_name[0] != '.' && strstr(entry->d_name, ".raw")) count++;
    }
    rewinddir(dir);

    if (count == 0) {
        closedir(dir);
        control->playlist_mode = 0;
        pthread_mutex_unlock(&control->mutex);
        return;
    }
	    control->playlist = calloc(count, sizeof(char*));
	    if (!control->playlist) {
	        closedir(dir);
	        control->playlist_mode = 0;
	        control->playlist_size = 0;
	        control->current_track = 0;
	        pthread_mutex_unlock(&control->mutex);
	        return;
	    }
    int idx = 0;
	while ((entry = readdir(dir)) && idx < count) {
	    size_t len = strlen(entry->d_name);
	    if (entry->d_name[0] != '.' && len >= 4 && strcmp(entry->d_name + len - 4, ".raw") == 0) {
		        char *full_path = xasprintf("%s/%s", dir_path, entry->d_name);
		        if (full_path) {
		            control->playlist[idx++] = full_path;
		        } else {
		            continue;
		        }
	    }
	}
    closedir(dir);
    control->playlist_capacity = count;
    control->playlist_size = idx;
    qsort(control->playlist, control->playlist_size, sizeof(char *), playlist_cmp);
    control->current_track = 0;
    control->playlist_mode = 1;
    if (idx > 0 && control->playlist[0]) {
        if (control->filename) free(control->filename);
        control->filename = strdup(control->playlist[0]);
        pthread_cond_signal(&control->cond);
    control->playlist_dir = strdup(dir_path);
    }

    pthread_mutex_unlock(&control->mutex);
}

void shutdown_player_thread(PlayerControl *control, pthread_t thread, int *have_player_thread) {
    if (!*have_player_thread) return;
    pthread_mutex_lock(&control->mutex);
    control->quit = 1;
    pthread_cond_signal(&control->cond);
    pthread_mutex_unlock(&control->mutex);
    int has_active_file = 0;
    pthread_mutex_lock(&control->mutex);
    has_active_file = (control->current_filename != NULL);
    pthread_mutex_unlock(&control->mutex);

    if (has_active_file) {
        int joined = 0;
        time_t start = time(NULL);
        while (!joined && (time(NULL) - start < 3)) {
            int kill_ret = pthread_kill(thread, 0);
            if (kill_ret == ESRCH) {
                joined = 1;
            } else if (kill_ret == 0) {
                usleep(100000);
            }
        }
        if (!joined) {
            double elapsed = control->bytes_read / 176400.0;
int hours = (int)elapsed / 3600;
int mins = ((int)elapsed % 3600) / 60;
int secs = (int)elapsed % 60;
            fprintf(stderr, "\033[31mWARNING: Audio thread did not finish in 3s — interrupted at %02d:%02d:%02d — leaving it (safer than pthread_cancel)\033[0m\n", hours, mins, secs);
            fflush(stderr);
            usleep(1000000);
        }
    } else {
        pthread_join(thread, NULL);
    }
}
void action_p(void)
	{
	    if (player_control.paused) {
	        player_control.paused = 0;
	        player_control.is_silent = 0;
	        player_control.fading_in = 1;
	        player_control.current_fade = 0;
	    } else {
	        player_control.paused = 1;
	        player_control.fading_out = 1;
	        player_control.current_fade = FADE_STEPS;
	    }
	}
void action_f(void) {
    player_control.seek_delta = 10;
}

void action_b(void) {
    player_control.seek_delta = -10;
}

void action_s(void) {
    player_control.stop = 1;
    player_control.duration   = 0.0;
    player_control.bytes_read = 0;
    if (player_control.current_filename) {
        free(player_control.current_filename);
        player_control.current_filename = NULL;
    }
    player_control.paused = 0;
    player_control.playlist_mode = 0;
    player_control.current_track = 0;
}

void with_player_control_lock(void (*action)(void)) {
    pthread_mutex_lock(&player_control.mutex);
    if (action) action();
    pthread_cond_signal(&player_control.cond);
    pthread_mutex_unlock(&player_control.mutex);
}
int navigate_and_play(void) {
    init_ncurses("ru_RU.UTF-8");
    curs_set(0);
    clearok(stdscr, TRUE);
    idlok(stdscr, TRUE);
    scrollok(stdscr, FALSE);
    getmaxyx(stdscr, term_height, term_width);
    draw_outer_frame(stdscr, term_height, term_width, 0);
    doupdate();

    if (term_width < MIN_WIDTH || term_height < MIN_HEIGHT) {
        endwin();
fprintf(stderr, "\033[31mTerminal too small! Minimum: %dx%d\033[0m\n", MIN_WIDTH, MIN_HEIGHT);
        return -1;
    }
    list_win = newwin(term_height - 2, INNER_WIDTH, 1, 1);
    if (!list_win) { endwin(); return -1; }
	wtimeout(list_win, 50);
	wtimeout(stdscr, 50);
    keypad(list_win, TRUE);

    if (getcwd(current_dir, PATH_MAX) == NULL) {
        delwin(list_win); endwin(); return -1;
    }
forward_history = malloc(forward_capacity * sizeof(char *));
if (!forward_history) {
    fprintf(stderr, "Ошибка: не хватает памяти для истории навигации — функция отключена\n");
    forward_capacity = 0;
    forward_count = 0;
} else {
    memset(forward_history, 0, forward_capacity * sizeof(char *));
}
strncpy(root_dir, current_dir, sizeof(root_dir) - 2);
root_dir[sizeof(root_dir) - 2] = '\0';
if (root_dir[0] != '\0' && root_dir[strlen(root_dir) - 1] != '/') {
    strcat(root_dir, "/");
}
atexit(free_forward_history);
    update_file_list();
    draw_file_list(list_win);
    refresh();
static int help_mode = 0;
int ch;
	while (1) {
	    ch = wgetch(list_win);
	if (show_status && (time(NULL) - status_start_time >= STATUS_DURATION_SECONDS)) {
    show_status = 0;
    status_msg[0] = '\0';
    draw_file_list(list_win);
}
	    if (ch == ERR) {
	        static time_t last_update_time = 0;
	        time_t now = time(NULL);
	        if (system_time_toggle && now != last_update_time) {
	            last_update_time = now;
	        } else {
pthread_mutex_lock(&player_control.mutex);
char *current_file_name = NULL;
char *playlist_dir = player_control.playlist_dir;
char *active_filename = player_control.current_filename;
int playlist_mode = player_control.playlist_mode;
if (active_filename && strlen(active_filename) > 0) {
    const char *slash = strrchr(active_filename, '/');
    current_file_name = strdup(slash ? slash + 1 : active_filename);
}
pthread_mutex_unlock(&player_control.mutex);
if (playlist_mode && playlist_dir && strcmp(playlist_dir, current_dir) == 0 && current_file_name) {
    for (int i = 0; i < file_count; i++) {
        if (strcmp(file_list[i].name, current_file_name) == 0 && !file_list[i].is_dir) {
            selected_index = i;
            break;
        }
    }
}
if (current_file_name) free(current_file_name);
draw_file_list(list_win);
	        }
	        continue;
    }
    if (help_mode) {
        help_mode = 0;
        update_file_list();
	        display_message(STATUS, "Back to files — press 'h' for help");
        continue;
     }
	if (ch != 10) {
	    show_error = 0;
	    error_msg[0] = '\0';
	    show_status = 0;
	    status_msg[0] = '\0';
	}
        if (ch != 't' && ch != 'T') system_time_toggle = 0;
    strcpy(status_msg, "");
    if (ch == 'q' || ch == 'Q') break;
    switch (ch) {
        case KEY_UP:
            if (file_count > 0 && selected_index > 0) selected_index--;
            break;
        case KEY_DOWN:
            if (file_count > 0 && selected_index < file_count - 1) selected_index++;
            break;
case KEY_LEFT:
    {
        if (strcmp(current_dir, "/") == 0) {
display_message(STATUS, "Already at filesystem root");
            break;
        }

        char temp_path[PATH_MAX];
        strncpy(temp_path, current_dir, sizeof(temp_path)-1);
        temp_path[sizeof(temp_path)-1] = '\0';

        char *last_slash = strrchr(temp_path, '/');
        if (last_slash == temp_path) {
            strcpy(temp_path, "/");
        } else if (last_slash) {
            *last_slash = '\0';
        }
// * If desired, insert a small script here that blocks exiting the directory in which the user is located. Enable at the user's discretion. *
        if (add_to_forward(current_dir) == 0) {
            if (navigate_dir(temp_path, status_msg) == 0) {
            } else {
                if (forward_count > 0) {
                    free(forward_history[forward_count - 1]);
                    forward_history[forward_count - 1] = NULL;
                    forward_count--;
                }
            }
        } else {
display_message(STATUS, "Cannot save forward history");
        }
        break;
    }
case KEY_RIGHT:
    {
        if (forward_count > 0) {
            char *next_dir = forward_history[forward_count - 1];
            if (navigate_dir(next_dir, status_msg) == 0) {
                free(forward_history[forward_count - 1]);
                forward_history[forward_count - 1] = NULL;
                forward_count--;
            }
        } else {
display_message(STATUS, "No forward history");
        }
        break;
    }
        case 10:
	            if (file_count > 0 && selected_index >= 0 && file_list && file_list[selected_index].name) {
	                if (!file_list[selected_index].is_dir) {
	                    if (is_raw_file(file_list[selected_index].name)) {
	                        char *full_path = xasprintf("%s/%s", current_dir, file_list[selected_index].name);
	                        if (!full_path) {
	display_message(STATUS, "Out of memory! Cannot play file.");
	                            break;
	                        }

	                        pthread_mutex_lock(&player_control.mutex);
	                        if (player_control.filename) free(player_control.filename);
	                        player_control.filename = full_path;
	                        if (player_control.playlist_mode) {
	                            for (int i = 0; i < player_control.playlist_size; i++) {
	                                if (player_control.playlist[i]) free(player_control.playlist[i]);
	                            }
	                            free(player_control.playlist);
	                            player_control.playlist = NULL;
	                            player_control.playlist_size = 0;
	                        }
	                        player_control.playlist_mode = 0;
	                        player_control.current_track = 0;
	                        player_control.paused = 0;
	                        pthread_cond_signal(&player_control.cond);
	                        pthread_mutex_unlock(&player_control.mutex);
	                        pthread_mutex_lock(&player_control.mutex);
	                        if (player_control.current_filename) {
	                            free(player_control.current_filename);
	                        }
	                        player_control.current_filename = strdup(file_list[selected_index].name);
	                        player_control.paused = 0;
	                        pthread_mutex_unlock(&player_control.mutex);

	                    } else {
	display_message(ERROR, "Not a .raw file");
	                    }
	                } else {
                    int dir_fd = openat(AT_FDCWD, file_list[selected_index].name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir_fd == -1) {
        display_message(ERROR, "Access denied to: %s/%s", current_dir, file_list[selected_index].name);
        break;
    }
    struct stat st;
    if (fstat(dir_fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        display_message(ERROR, "Not a directory or state changed: %s/%s", current_dir, file_list[selected_index].name);
        close(dir_fd);
        break;
    }
    if (fchdir(dir_fd) != 0) {
        display_message(ERROR, "Access denied to: %s/%s", current_dir, file_list[selected_index].name);
        close(dir_fd);
        break;
    }
                    close(dir_fd);
	                    char old_dir[PATH_MAX];
	                    strncpy(old_dir, current_dir, sizeof(old_dir));
	                    old_dir[sizeof(old_dir)-1] = '\0';
	                    if (getcwd(current_dir, PATH_MAX) == NULL) {
	                        display_message(STATUS, "getcwd failed after fchdir");
	                        char fallback_path[PATH_MAX];
	                        if (snprintf(fallback_path, sizeof(fallback_path), "%s/%s", old_dir, file_list[selected_index].name) < (int)sizeof(fallback_path)) {
	                            char normalized_fallback[PATH_MAX];
	                            if (realpath(fallback_path, normalized_fallback) != NULL) {
	                                strncpy(current_dir, normalized_fallback, PATH_MAX - 1);
	                                current_dir[PATH_MAX - 1] = '\0';
	                            } else {
	                                strncpy(current_dir, old_dir, PATH_MAX - 1);
	                                current_dir[PATH_MAX - 1] = '\0';
	                                char *display_dir = basename(old_dir);
display_message(STATUS, "Fallback to old dir: %s", display_dir);
	                            }
	                        } else {
	                            strncpy(current_dir, old_dir, PATH_MAX - 1);
	                            current_dir[PATH_MAX - 1] = '\0';
display_message(STATUS, "Path too long, fallback to old dir");
	                        }
	                    }
	                    update_file_list();
	                }
	            }
	            break;
case 'p': case 'P':
with_player_control_lock(action_p);
	display_message(STATUS, "%s", player_control.paused ? "PAUSED (fading out)" : "RESUMED (fading in)");
	break;
case 'f':
if (player_control.current_file) {
with_player_control_lock(action_f);
} else {
display_message(STATUS, "Nothing to fast-forward");
}
break;
case 'b':
if (player_control.current_file) {
with_player_control_lock(action_b);
} else {
display_message(STATUS, "Nothing to rewind");
}
break;
	case 's': case 'S':
with_player_control_lock(action_s);
display_message(STATUS, "Playback stopped");
break;
		case 'h': case 'H':
		{
		    int help_start_index = 0;
		    help_mode = 1;
		    wtimeout(list_win, -1);
		    while (help_mode) {
		        draw_help(list_win, help_start_index);
		        int ch_help = wgetch(list_win);
		        switch (ch_help) {
		            case 'u': case 'U':
		                if (help_start_index > 0) help_start_index--;
		                break;
		            case 'd': case 'D':
		                int max_y, max_x;
		                getmaxyx(list_win, max_y, max_x); (void)max_x;
		                int visible_lines = max_y - 6;
		                int help_count = 36;
		                if (help_start_index < help_count - visible_lines) help_start_index++;
		                break;
		            case 'q': case 'Q':
		                help_mode = 0;
		                ch = 'q';
		                break;
		            default:
		                help_mode = 0;
		                break;
		        }
		    }
		    wtimeout(list_win, 50);
		    help_mode = 0;
		    draw_file_list(list_win);
		    display_message(STATUS, "Back to files — press 'h' for help");
		}
		break;
case 't': case 'T':
    system_time_toggle = !system_time_toggle;
    error_toggle = 0;
    break;
	case ' ':
	    if (file_count > 0 && selected_index >= 0 && file_list && file_list[selected_index].name && file_list[selected_index].is_dir) {
	            char full_path[PATH_MAX];
	            if (snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, file_list[selected_index].name) >= PATH_MAX) {
	                display_message(STATUS, "Path too long!");
	            } else {
	                load_playlist(full_path, &player_control);

	                if (player_control.playlist_size == 0) {
	                    display_message(ERROR, "Directory does not contain raw files.");
	                } else {
	                    pthread_mutex_lock(&player_control.mutex);

	                    if (player_control.filename) {
	                        free(player_control.filename);
	                    }
	                    player_control.filename = strdup(player_control.playlist[0]);

	                    if (player_control.current_filename) {
	                        free(player_control.current_filename);
	                    }
	                    player_control.current_filename = strdup(basename((char*)player_control.playlist[0]));
	                    player_control.current_track = 0;
	                    player_control.playlist_mode = 1;
	                    player_control.paused = 0;
	                    player_control.is_silent = 0;
	                    player_control.fading_in = 0;
	                    player_control.fading_out = 0;
	                    player_control.current_fade = FADE_STEPS;
	                    player_control.bytes_read = 0;
	                    player_control.duration = 0.0;
	                    player_control.seek_delta = 0;
	                    pthread_cond_signal(&player_control.cond);
	                    pthread_mutex_unlock(&player_control.mutex);
            }
	            }
	    } else {
	            display_message(STATUS, "Select a directory for playlist!");
	    }
	    break;
case 'a': case 'A':
    load_playlist(current_dir, &player_control);
    if (player_control.playlist_size == 0) {
        display_message(ERROR, "No .raw files in current directory.");
    } else {
        pthread_mutex_lock(&player_control.mutex);
        if (player_control.filename) free(player_control.filename);
        player_control.filename = strdup(player_control.playlist[0]);
        if (player_control.current_filename) free(player_control.current_filename);
        player_control.current_filename = strdup(basename(player_control.playlist[0]));
        player_control.current_track = 0;
        player_control.playlist_mode = 1;
        player_control.paused = 0;
        player_control.is_silent = 0;
        player_control.fading_in = 0;
        player_control.fading_out = 0;
        player_control.current_fade = FADE_STEPS;
        player_control.bytes_read = 0;
        player_control.duration = 0.0;
        player_control.seek_delta = 0;
        pthread_cond_signal(&player_control.cond);
        pthread_mutex_unlock(&player_control.mutex);
    }
    break;
	case 'l': case 'L':
	    pthread_mutex_lock(&player_control.mutex);
const char *selected_file_name = NULL;
if (file_count > 0 && selected_index >= 0 && file_list && file_list[selected_index].name &&
    !file_list[selected_index].is_dir && is_raw_file(file_list[selected_index].name)) {
    selected_file_name = file_list[selected_index].name;
}
if (selected_file_name &&
    (player_control.current_filename == NULL || strcmp(player_control.current_filename, selected_file_name) != 0)) {
    char *full_path = xasprintf("%s/%s", current_dir, selected_file_name);
    if (full_path) {
        if (player_control.filename) free(player_control.filename);
        player_control.filename = full_path;
        if (player_control.playlist_mode) {
            free_str_array(player_control.playlist, player_control.playlist_size);
            player_control.playlist = NULL;
            player_control.playlist_size = 0;
            player_control.playlist_capacity = 0;
            free(player_control.playlist_dir);
            player_control.playlist_dir = NULL;
        }
        player_control.playlist_mode = 0;
        player_control.current_track = 0;
        player_control.paused = 0;
        player_control.loop_mode = 1;
        player_control.is_silent = 0;
        player_control.fading_in = 0;
        player_control.fading_out = 0;
        player_control.current_fade = FADE_STEPS;
        player_control.bytes_read = 0;
        player_control.duration = 0.0;
        player_control.seek_delta = 0;
        pthread_cond_signal(&player_control.cond);
        if (player_control.current_filename) free(player_control.current_filename);
        player_control.current_filename = strdup(selected_file_name);
        display_message(STATUS, "Playback started in loop mode on new file");
    } else {
        display_message(STATUS, "Out of memory! Cannot play file.");
    }
} else {
    if (player_control.current_file) {
        player_control.loop_mode = !player_control.loop_mode;
        display_message(STATUS, "%s", player_control.loop_mode ? "Loop mode enabled" : "Loop mode disabled");
    } else {
        display_message(STATUS, "Nothing to loop");
    }
}
	    pthread_mutex_unlock(&player_control.mutex);
	    break;
}
}
refresh_ui();
		if (show_status && ch != ERR) {
		    show_status = 0;
		}
    if (list_win) {
        delwin(list_win);
        list_win = NULL;
    }
    endwin();
    free_file_list();
    free_forward_history();
    return 0;
}
void draw_help(WINDOW *win, int start_index) {
    int max_y, max_x;
    getmaxyx(win, max_y, max_x); (void)max_x;
    clear_rect(win, 3, max_y - 3, 0, 83);
    draw_help_frame(win);
const char *help_lines[] = {
    " a       load playlist from current directory",
    " b       -10 seconds",
    " d       from down (downward) — to scroll down similarly.",
    " f       +10 seconds",
    " h       this help",
    " l       toggle loop mode (enable/disable cyclic playback of file)",
    " p       pause / continue",
    " The yellow color of the selection bar for files and directories in the list.",
    " q       quit",
    " s       stop",
    " t       time",
    " u       from up (upward) — to scroll up by one line",
    "         (or by a page, if visible_help_lines is large).",
    "         Press any key to return. Return the green selection color",
    "",
    " ↑       move up the list",
    " ↓       move down the list",
    " ←       up one level",
    " →       forward in history",
    "",
    " Enter   enter folder",
    " Enter   play .raw",
    " The blue color of the selection bar for files and directories in the list.",
    "",
    " Space   load playlist from folder",
    "",
    " Only .raw files are played, .raw PCM without header",
    " s16le, 2 channels, 44100 Hz, 16 bits/sample, stereo, little-endian",
    "",
    " ffmpeg -i input.mp3 -f s16le -ac 2 -ar 44100 output.raw",
    "",
    NULL
};
    int help_count = 0;
    while (help_lines[help_count]) help_count++;
    int visible_lines = max_y - 8;
    if (visible_lines < 1) visible_lines = 1;
if (start_index < 0) start_index = 0;
int max_start = help_count - visible_lines;
if (max_start < 0) max_start = 0;
if (start_index > max_start) start_index = max_start;
    int end_index = start_index + visible_lines;
    if (end_index > help_count) end_index = help_count;
    int y = 4;
    for (int i = start_index; i < end_index; i++) {
        mvwprintw(win, y++, 1, "%s", help_lines[i]);
    }
    wnoutrefresh(win);
    doupdate();
}
int navigate_dir(const char *target_dir, char *status_buf)
{
    if (!target_dir || !status_buf) return -1;

    if (chdir(target_dir) == 0) {
        if (getcwd(current_dir, PATH_MAX) == NULL) {
            display_message(STATUS, "getcwd failed");
            strncpy(current_dir, target_dir, PATH_MAX - 1);
            current_dir[PATH_MAX - 1] = '\0';
            return -1;
        }
        update_file_list();
        status_buf[0] = '\0';
        return 0;
    } else {
        display_message(ERROR, "Access denied or path not exists");
        return -1;
    }
}
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pthread_t thread = 0;
    int have_player_thread = 0;

    if (pthread_create(&thread, NULL, player_thread, &player_control) == 0) {
        have_player_thread = 1;
    } else {
        perror("pthread_create failed — плеер отключён");
    }
    int result = navigate_and_play();
    if (have_player_thread) {
        shutdown_player_thread(&player_control, thread, &have_player_thread);
    }
    pthread_mutex_lock(&player_control.mutex);
    if (player_control.playlist) {
    free_str_array(player_control.playlist, player_control.playlist_size);
        player_control.playlist = NULL;
        player_control.playlist_size = 0;
        player_control.playlist_capacity = 0;
        player_control.current_track = 0;
    }
    free(player_control.playlist_dir);
    pthread_mutex_unlock(&player_control.mutex);
    pthread_mutex_destroy(&player_control.mutex);
    pthread_cond_destroy(&player_control.cond);
    return result;
}
// 1792
