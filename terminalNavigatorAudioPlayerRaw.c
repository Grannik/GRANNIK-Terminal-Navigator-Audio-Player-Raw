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
#include <strings.h>
#include <math.h>

void draw_file_list(WINDOW *win);
void draw_field_frame(WINDOW *win);

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
#define BYTES_PER_SECOND 176400LL
#define SAFE_RETURN_IF_NULL(ptr, val) if (!(ptr)) { return (val); }
#define SAFE_CONTINUE_IF_NULL(ptr) if (!(ptr)) { continue; }
#define SAFE_ACTION_IF_NULL(ptr, ...) if (!(ptr)) { __VA_ARGS__; }
#define SAFE_FREE_GENERIC(ptr, free_func, ...) if ((ptr)) { free_func((ptr), ##__VA_ARGS__); (ptr) = NULL; }
#define SAFE_FREE(ptr) SAFE_FREE_GENERIC((ptr), free)
#define SAFE_FREE_ARRAY(arr, count) SAFE_FREE_GENERIC((arr), free_names, (count), 0)
#define SAFE_STRDUP(src) safe_strdup(src)
#define SAFE_CALLOC(num, size) calloc((num), (size))
#define SAFE_CLEANUP_RESOURCES(filep, handlep, pollfdsp, currfilep) safe_cleanup_resources((filep), (handlep), (pollfdsp), (currfilep))
#define SAFE_STRNCPY(dest, src, size) do { strncpy((dest), (src), (size)); (dest)[(size)-1] = '\0'; } while (0)
#define COLOR_ATTR_ON(win, attr) wattron(win, COLOR_PAIR(attr))
#define COLOR_ATTR_OFF(win, attr) wattroff(win, COLOR_PAIR(attr))
#define ACCESS_DENIED_MSG "Access denied to: %s"
#define SAFE_MUTEX_LOCK(m) do { int ret = pthread_mutex_lock(m); if (ret != 0) { display_message(ERROR, "Mutex lock failed: %s", strerror(ret)); } } while (0)

void endwin_and_clear_screen(void)
{
    endwin();
    if (write(STDOUT_FILENO, "\033[3J\033[2J\033[H", 11) < 0) {
    }
}

static int has_wide_chars(const wchar_t *w, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (wcwidth(w[i]) == 2)
            return 1;
    }
    return 0;
}

static inline int get_char_width(wchar_t c) {
    int w = wcwidth(c);
    return (w >= 0) ? w : 1;
}

static void add_ellipsis(wchar_t *dest, size_t *dest_idx, size_t dest_size, const wchar_t *ellipsis, size_t ell_len) {
    for (size_t j = 0; j < ell_len && *dest_idx < dest_size - 1; j++) {
        dest[(*dest_idx)++] = ellipsis[j];
    }
}

int convert_to_wchar(const char *src, wchar_t **wsrc_out, size_t *wlen_out) {
    if (!src || !wsrc_out || !wlen_out) {
        if (wsrc_out) *wsrc_out = NULL;
        if (wlen_out) *wlen_out = 0;
        return -1;
    }
    size_t src_len = strlen(src);
    wchar_t *wsrc = calloc(src_len + 1, sizeof(wchar_t));
    if (!wsrc) return -1;
    mbstate_t state = {0};
    const char *ptr = src;
    size_t wlen = mbsrtowcs(wsrc, &ptr, src_len + 1, &state);
    if (wlen == (size_t)-1) {
        free(wsrc);
        wsrc = calloc(src_len + 1, sizeof(wchar_t));
        if (!wsrc) return -1;
        const char *p = src;
        size_t idx = 0;
        while (*p && idx < src_len) {
            unsigned char c = (unsigned char)*p;
            if (c <= 127) {
                wsrc[idx++] = (wchar_t)c;
            } else if (c >= 192 && c <= 223 && p[1] != 0) {
                wsrc[idx++] = ((c & 0x1F) << 6) | ((unsigned char)p[1] & 0x3F);
                p++;
            } else if (c >= 224 && c <= 239 && p[1] != 0 && p[2] != 0) {
                wsrc[idx++] = ((c & 0x0F) << 12) | (((unsigned char)p[1] & 0x3F) << 6) | ((unsigned char)p[2] & 0x3F);
                p += 2;
            } else if (c >= 240 && c <= 247 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
                wsrc[idx++] = ((c & 0x07) << 18) | (((unsigned char)p[1] & 0x3F) << 12) | (((unsigned char)p[2] & 0x3F) << 6) | ((unsigned char)p[3] & 0x3F);
                p += 3;
            } else {
                wsrc[idx++] = L'?';
            }
            p++;
        }
        wsrc[idx] = L'\0';
        wlen = idx;
    }
    *wsrc_out = wsrc;
    *wlen_out = wlen;
    return 0;
}

static int compute_wchar_width(const wchar_t *w, size_t len) {
    int width = 0;
    for (size_t i = 0; i < len; i++) {
        width += get_char_width(w[i]);
    }
    return width;
}
static int calculate_visual_width(const char *src);
int prepare_display_wstring(const char *src, int max_visual_width, wchar_t *dest, size_t dest_size, int add_suffix, const wchar_t *ellipsis, int visual_offset, int use_middle_ellipsis) {
    if (!src || !dest || dest_size == 0) return -1;
    dest[0] = L'\0';
    if (strlen(src) == 0) return 0;
    if (!ellipsis) ellipsis = L"..";
    wchar_t *wsrc = NULL;
    size_t wlen = 0;
    if (convert_to_wchar(src, &wsrc, &wlen) != 0) return -1;
    int total_width = calculate_visual_width(src);
    if (total_width <= max_visual_width) {
        wcsncpy(dest, wsrc, dest_size - 1);
        dest[dest_size - 1] = L'\0';
        free(wsrc);
        return 0;
    }
	size_t ell_len = wcslen(ellipsis);
	int ell_width = 0;
	if (use_middle_ellipsis && has_wide_chars(wsrc, wlen) && !add_suffix) {
	    ellipsis = L"...";
	    ell_len = 3;
	}
	ell_width = compute_wchar_width(ellipsis, ell_len);
    int remaining_width = max_visual_width - ell_width;
    if (remaining_width < 2) {
        wcsncpy(dest, ellipsis, dest_size - 1);
        dest[dest_size - 1] = L'\0';
        free(wsrc);
        return 0;
    }
    size_t dest_idx = 0;
    int current_width = 0;
    if (use_middle_ellipsis) {
        int front_width = 0;
        int back_width = 0;
        if (remaining_width > 1) {
            front_width = remaining_width / 2;
            back_width = remaining_width - front_width;
        }
	if (has_wide_chars(wsrc, wlen)) {
	    if (front_width % 2 != 0) {
	        front_width--;
	        back_width++;
	    }
	    if (back_width % 2 != 0) {
	        back_width--;
	    }
}
        size_t i = 0;
        while (i < wlen && dest_idx < dest_size - 1 && current_width < front_width) {
int char_width = get_char_width(wsrc[i]);
            if (current_width + char_width > front_width) break;
            dest[dest_idx++] = wsrc[i++];
            current_width += char_width;
        }
add_ellipsis(dest, &dest_idx, dest_size, ellipsis, ell_len);
        current_width += ell_width;
        size_t back_start = wlen;
        int back_current = 0;
        while (back_start > i && back_current < back_width) {
            back_start--;
int char_width = get_char_width(wsrc[back_start]);
            if (back_current + char_width > back_width) {
                back_start++;
                break;
            }
            back_current += char_width;
        }
        for (size_t k = back_start; k < wlen && dest_idx < dest_size - 1; k++) {
            dest[dest_idx++] = wsrc[k];
        }
    } else {
        if (visual_offset < 0) visual_offset = 0;
        if (visual_offset > total_width) visual_offset = total_width;
        int current_offset = 0;
        size_t start_i = 0;
        while (start_i < wlen) {
int char_width = get_char_width(wsrc[start_i]);
            if (current_offset + char_width > visual_offset) break;
            current_offset += char_width;
            start_i++;
        }
        int need_end_ellipsis = (start_i < wlen && total_width - current_offset > max_visual_width);
        if (need_end_ellipsis) {
            remaining_width = max_visual_width - ell_width;
            if (remaining_width < 0) remaining_width = 0;
        } else {
            remaining_width = max_visual_width;
        }
        size_t i = start_i;
        while (i < wlen && dest_idx < dest_size - 1) {
int char_width = get_char_width(wsrc[i]);
            if (current_width + char_width > remaining_width) break;
            dest[dest_idx++] = wsrc[i++];
            current_width += char_width;
        }
        if (need_end_ellipsis) {
add_ellipsis(dest, &dest_idx, dest_size, ellipsis, ell_len);
        }
    }
    dest[dest_idx] = L'\0';
    if (add_suffix) {
        wchar_t suffix = L'/';
	int suffix_width = get_char_width(suffix);
if (current_width + suffix_width <= max_visual_width && dest_idx < dest_size - 1) {
            dest[dest_idx++] = suffix;
            dest[dest_idx] = L'\0';
        }
    }
    free(wsrc);
    return 0;
}

static int calculate_visual_width(const char *src) {
    if (!src) return 0;
    wchar_t *wsrc = NULL;
    size_t wlen = 0;
    if (convert_to_wchar(src, &wsrc, &wlen) != 0) return 0;
    int total_width = 0;
    total_width = compute_wchar_width(wsrc, wlen);
SAFE_FREE(wsrc);
    return total_width;
}

typedef struct {
    char *name;
    int is_dir;
} FileEntry;
static char *safe_strdup(const char *src);
static void display_message(int type, const char *fmt, ...);
static void memory_error(void) {
    display_message(ERROR, "Out of memory");
}
static void check_alloc(void *ptr) {if (!ptr) memory_error();}
static int is_raw_file(const char *name);
__attribute__((unused)) static char *xasprintf(const char *fmt, ...);
static void free_names(void *entries, int count, int is_file_entry);
static inline int should_skip_entry(const struct dirent *entry, int filter_raw) {
    return (entry->d_name[0] == '.') || (filter_raw && !is_raw_file(entry->d_name));
}
static int scan_directory(const char *dir_path, int filter_raw, void **entries_out, int *count_out, int for_playlist) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }
struct dirent *entry;
int count = 0;
while ((entry = readdir(dir))) {
if (should_skip_entry(entry, filter_raw)) continue;
    count++;
}
rewinddir(dir);
    if (count == 0) {
        closedir(dir);
        return 0;
    }
    size_t entry_size = for_playlist ? sizeof(char *) : sizeof(FileEntry);
	    size_t capacity = (count > 0) ? count : 1;
	    void *entries = calloc(capacity, entry_size);
    if (!entries) {
        closedir(dir);
        return -1;
    }
size_t idx = 0;
    while ((entry = readdir(dir))) {
        if (should_skip_entry(entry, filter_raw)) continue;
        char *name = strdup(entry->d_name);
        if (!name) continue;
        char *full_path = xasprintf("%s/%s", dir_path, entry->d_name);
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
	        full_path = safe_strdup(resolved_path);
        if (!full_path) {
            free(name);
            continue;
        }
        if (for_playlist) {
            ((char **)entries)[idx] = full_path;
            free(name);
        } else {
struct stat st;
if (lstat(full_path, &st) == -1) {
    display_message(ERROR, "lstat failed for: %s (errno: %d)", full_path, errno);
    ((FileEntry *)entries)[idx].is_dir = 0;
} else {
    ((FileEntry *)entries)[idx].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
}
            ((FileEntry *)entries)[idx].name = name;
            free(full_path);
        }
        idx++;
    }
    closedir(dir);
if (idx == 0) {
    free(entries);
    entries = NULL;
}
    *entries_out = entries;
    *count_out = idx;
    return 0;
}

static char *safe_strdup(const char *src) {
    if (!src) return NULL;
    char *dup = strdup(src);
    check_alloc(dup);
    return dup;
}
// verification!
static void assign_safe_strdup(char **dest, const char *src) {
    SAFE_FREE(*dest);
    *dest = safe_strdup(src);
}
// verification!
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
static void draw_fill_line(WINDOW *win, int start_y, int start_x, int length, const void *symbol, int is_horizontal, int is_wide);
static void draw_progress_bar(WINDOW *win, int y, int start_x, double percent, int bar_length) {
    if (percent < 0.0 || isnan(percent)) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    int filled = (int)((percent / 100.0) * bar_length + 0.5);
    if (filled > bar_length) filled = bar_length;
    wchar_t fill_char = SCROLL_FILLED;
    wchar_t empty_char = SCROLL_EMPTY;
int color_empty = (percent > 0.0) ? COLOR_PAIR_BORDER : COLOR_PAIR_WHITE;
wattron(win, COLOR_PAIR(color_empty));
COLOR_ATTR_ON(win, color_empty);
draw_fill_line(win, y, start_x, bar_length, &empty_char, 1, 1);
if (filled > 0) {
COLOR_ATTR_ON(win, COLOR_PAIR_PROGRESS | A_BOLD);
draw_fill_line(win, y, start_x, filled, &fill_char, 1, 1);
COLOR_ATTR_OFF(win, COLOR_PAIR_PROGRESS | A_BOLD);
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

FileEntry *file_list = NULL;
int file_count = 0;
int selected_index = 0;
char current_dir[PATH_MAX];
char root_dir[PATH_MAX] = {0};
int path_visual_offset = 0;
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
static char *xasprintf(const char *fmt, ...)
{
    char *res = NULL;
    va_list ap;
    va_start(ap, fmt);
    if (vasprintf(&res, fmt, ap) == -1) {
        res = NULL;
    }
    check_alloc(res);
    va_end(ap);
    return res;
}
static void free_names(void *entries, int count, int is_file_entry) {
    if (!entries) return;
    if (is_file_entry) {
        FileEntry *list = (FileEntry *)entries;
        for (int i = 0; i < count; i++) {
            if (list[i].name) free(list[i].name);
        }
    } else {
        char **arr = (char **)entries;
        for (int i = 0; i < count; i++) {
            if (arr[i]) free(arr[i]);
        }
    }
    free(entries);
}
// verification!
void cleanup_playlist_resources(void *files, int file_count, char *current_file, char *current_full) {
    if (!files) return;
    free_names(files, file_count, 0);
    SAFE_FREE(current_file);
    SAFE_FREE(current_full);
}
// verification!
void init_ncurses(const char *locale) {
if (setlocale(LC_ALL, locale) == NULL) {
    fprintf(stderr, "Failed to set locale: %s — fallback to default\n", locale);
    if (setlocale(LC_ALL, "") == NULL) {
        fprintf(stderr, "Failed to set default locale — fallback to C.UTF-8\n");
        if (setlocale(LC_ALL, "C.UTF-8") == NULL) {
            fprintf(stderr, "Failed to set C.UTF-8 — using C locale\n");
            setlocale(LC_ALL, "C");
        }
    }
}

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

static int setup_alsa_hw_params(snd_pcm_t *handle, snd_pcm_hw_params_t *params, unsigned int *rate, int channels, snd_pcm_uframes_t *period_size, snd_pcm_uframes_t *buffer_size);
void play_audio(snd_pcm_t *handle, char *buffer, int size) {
    if (!handle || !buffer || size <= 0) {
display_message(ERROR, "Invalid params in play_audio");
        return;
    }
    snd_pcm_uframes_t frames = size / 4;
    char *ptr = buffer;
    snd_pcm_uframes_t remaining = frames;
    while (remaining > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(handle, ptr, remaining);
    if (written < 0) {
        if (written == -EAGAIN) {
            continue;
        }
        if (written == -EPIPE) {
            if (snd_pcm_prepare(handle) < 0) {
                display_message(ERROR, "snd_pcm_prepare failed after EPIPE");
                snd_pcm_drop(handle);
                return;
            }
        } else {
            display_message(ERROR, "snd_pcm_writei failed: %s", snd_strerror(written));
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
display_message(ERROR, "Invalid filename in open_audio_file");
        return NULL;
    }
int fd = open(filename, O_RDONLY | O_CLOEXEC);
if (fd == -1) {
    display_message(ERROR, "Failed to open file: %s", filename);
    return NULL;
}
FILE *file = fdopen(fd, "rb");
if (!file) {
    close(fd);
    display_message(ERROR, "fdopen failed for: %s", filename);
    return NULL;
}
    return file;
}

static void safe_cleanup_resources(FILE **file, snd_pcm_t **handle, struct pollfd **poll_fds, char **current_filename) {
    if (file && *file) { fclose(*file); *file = NULL; }
    if (handle && *handle) {
        snd_pcm_drop(*handle);
        snd_pcm_close(*handle);
        *handle = NULL;
    }
    if (poll_fds && *poll_fds) { free(*poll_fds); *poll_fds = NULL; }
    if (current_filename && *current_filename) { free(*current_filename); *current_filename = NULL; }
}

static int handle_alsa_error(int ret, const char *msg, int do_return) {
    if (ret < 0) {
        const char *err = snd_strerror(ret);
        if (err) {
            display_message(ERROR, "%s: %s — audio disabled", msg, err);
        } else {
            display_message(ERROR, "%s — audio disabled", msg);
        }
        if (do_return) return -1;
    }
    return 0;
}
// verification
static int setup_alsa_hw_params(snd_pcm_t *handle, snd_pcm_hw_params_t *params, unsigned int *rate, int channels, snd_pcm_uframes_t *period_size, snd_pcm_uframes_t *buffer_size) {
    int dir = 0;
    int ret;
    ret = snd_pcm_hw_params_any(handle, params);
    if (handle_alsa_error(ret, "snd_pcm_hw_params_any failed", 1) < 0)
        return -1;
    ret = snd_pcm_hw_params_set_access(
            handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (handle_alsa_error(ret, "snd_pcm_hw_params_set_access failed", 1) < 0)
        return -1;
    ret = snd_pcm_hw_params_set_format(
            handle, params, SND_PCM_FORMAT_S16_LE);
    if (handle_alsa_error(ret, "snd_pcm_hw_params_set_format failed", 1) < 0)
        return -1;
    ret = snd_pcm_hw_params_set_channels(
            handle, params, channels);
    if (handle_alsa_error(ret, "snd_pcm_hw_params_set_channels failed", 1) < 0)
        return -1;
    ret = snd_pcm_hw_params_set_rate_near(
            handle, params, rate, &dir);
    if (handle_alsa_error(ret, "snd_pcm_hw_params_set_rate_near failed", 1) < 0)
        return -1;
    ret = snd_pcm_hw_params_set_period_size_near(
            handle, params, period_size, &dir);
    if (handle_alsa_error(ret, "snd_pcm_hw_params_set_period_size_near failed", 1) < 0)
        return -1;
    ret = snd_pcm_hw_params_set_buffer_size_near(
            handle, params, buffer_size);
    if (handle_alsa_error(ret, "snd_pcm_hw_params_set_buffer_size_near failed", 1) < 0)
        return -1;
    if (handle_alsa_error(
            snd_pcm_hw_params(handle, params),
            "snd_pcm_hw_params failed", 1) < 0) {
        return -1;
    }
    return 0;
}
// verification
snd_pcm_t* init_audio_device(unsigned int rate, int channels) {
    snd_pcm_t *handle = NULL;
    int ret = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (handle_alsa_error(ret, "ALSA device open error", 1) < 0) {
        return NULL;
    }
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_uframes_t period_size = 256;
    snd_pcm_uframes_t buffer_size = 1024;
    if (setup_alsa_hw_params(handle, params, &rate, channels, &period_size, &buffer_size) < 0) {
        snd_pcm_close(handle);
        return NULL;
    }

    ret = snd_pcm_prepare(handle);
    if (handle_alsa_error(ret, "snd_pcm_prepare failed", 1) < 0) {
        snd_pcm_close(handle);
        return NULL;
    }
    return handle;
}

static void draw_single_frame(WINDOW *win, int start_y, int height, const char *title, int line_type);
static void draw_fill_line(WINDOW *win, int start_y, int start_x, int length, const void *symbol, int is_horizontal, int is_wide) {
int y = start_y;
int x = start_x;
for (int i = 0; i < length; i++) {
    if (is_wide) {
        mvwaddnwstr(win, y, x, (const wchar_t *)symbol, 1);
    } else {
        mvwprintw(win, y, x, "%s", (const char *)symbol);
    }
    if (is_horizontal) x++; else y++;
}
}

static void center_title_on_frame(WINDOW *win, int y, int actual_width, const char *title, const char *title_left, const char *title_right) {
    wchar_t wtitle[INNER_WIDTH + 1];
    prepare_display_wstring(title, actual_width - 4, wtitle, sizeof(wtitle) / sizeof(wchar_t), 0, L"..", 0, 0);
    int title_wlen = wcslen(wtitle);
    int title_width = 0;
    title_width = compute_wchar_width(wtitle, title_wlen);
    int title_len = title_width + 4;
    int title_start = (actual_width - title_len) / 2;
    if (title_start < 1) title_start = 1;
    if (title_start + title_len > actual_width - 1) title_start = actual_width - title_len - 1;
    mvwprintw(win, y, title_start, "%s ", title_left);
    mvwaddnwstr(win, y, title_start + 2, wtitle, title_wlen);
    mvwprintw(win, y, title_start + 2 + title_width, " %s", title_right);
}

static void draw_single_frame(WINDOW *win, int start_y, int height, const char *title, int line_type)
{
    int max_x = getmaxx(win);
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
    center_title_on_frame(win, start_y, actual_width, title, title_left, title_right);
    draw_fill_line(win, start_y + 1, 0, height - 2, vertical, 0, 0);
    draw_fill_line(win, start_y + 1, actual_width - 1, height - 2, vertical, 0, 0);
    mvwprintw(win, start_y + height - 1, 0, "%s", bottom_left);
    draw_fill_line(win, start_y + height - 1, 1, actual_width - 2, horizontal, 1, 0);
    mvwprintw(win, start_y + height - 1, actual_width - 1, "%s", bottom_right);
    wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
}

void draw_help_frame(WINDOW *win)
{
    int max_y;
    getmaxyx(win, max_y, term_width);
    draw_single_frame(win, 3, max_y - 6, "HELP", 0);
}

void draw_help(WINDOW *win, int start_index) {
    int max_y, max_x;
    getmaxyx(win, max_y, max_x); (void)max_x;
    clear_rect(win, 3, max_y - 3, 0, 83);
    draw_help_frame(win);
    const char *help_lines[] = {
    " COMMANDS:",
    " a       load playlist from current directory",
    " b       -10 seconds",
    " f       +10 seconds",
    " h       show help / hide help.",
    " l       toggle loop mode (enable/disable cyclic playback of file)",
    " n       play next file",
    " p       pause / continue",
    " q       quit",
    " s       stop",
    " t       time",
    "",
    " ↑       move up the list",
    " ↓       move down the list",
    " ←       up one level",
    " →       forward in history",
    "",
    " Shift ← scroll path left",
    " Shift → scroll path right",
    "",
    " In help mode:",
    " Shift ↓ scroll help text down.",
    " Shift ↑ scroll help text up.",
    "",
    " Enter   enter folder",
    " Enter   play .raw",
    " Space   load playlist from folder",
    "",
    " COLORS:",
    " Blue",
    " — color of the selection bar for files and directories in the list.",
    " Yellow",
    " — used for the selection bar and status messages when playback is paused.",
    " Red",
    " — error messages",
    " Green",
    " — highlights the selected file or directory in the list.",
    "",
    " FILE REQUIREMENTS:",
    " Only .raw files are played, .raw PCM without header",
    " s16le, 2 channels, 44100 Hz, 16 bits/sample, stereo, little-endian",
    "",
    " ffmpeg -i input.mp3 -f s16le -ac 2 -ar 44100 output.raw",
    "",
    " 我爱中国",
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
    const char *line = help_lines[i];
int avail_width = max_x - 2;
wchar_t wline[CURSOR_WIDTH + 4] = {0};
prepare_display_wstring(line, avail_width, wline, sizeof(wline)/sizeof(wchar_t), 0, L"..", 0, 0);
mvwaddwstr(win, y++, 1, wline);
    }
    wnoutrefresh(win);
int field_y = max_y - 3;
    clear_rect(win, field_y, max_y, 0, max_x);
    draw_field_frame(win);
    doupdate();
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
    long long bytes_read;
    int is_silent;
    int fading_out;
    int fading_in;
    int current_fade;
    char *playlist_dir;
    int loop_mode;
} PlayerControl;
 void lock_and_signal(PlayerControl *control, void (*action)(PlayerControl *));
void cleanup_playlist(PlayerControl *control) {
    if (!control) return;
    char **old_list = control->playlist;
    int old_size = control->playlist_size;
    char *old_dir = control->playlist_dir;
    control->playlist = NULL;
    control->playlist_size = 0;
    control->playlist_capacity = 0;
    control->current_track = 0;
    control->playlist_dir = NULL;
    if (old_list) {
        free_names(old_list, old_size, 0);
    }
    if (old_dir) {
        free(old_dir);
    }
}

static void reset_playback_fields(PlayerControl *control) {
    control->paused = 0;
    control->is_silent = 0;
    control->fading_in = 0;
    control->fading_out = 0;
    control->current_fade = FADE_STEPS;
    control->bytes_read = 0LL;
    control->duration = 0.0;
    control->seek_delta = 0;
}
// verification!
void init_playlist_start(PlayerControl *control) {
    if (!control) return;
    control->current_track = 0;
    control->playlist_mode = 1;
    reset_playback_fields(control);
    pthread_cond_signal(&control->cond);
    pthread_mutex_unlock(&control->mutex);
}
// verification!
struct AssignNextData {
    char *next_full_path;
    char *next_basename;
    char **files;
    int file_count;
    char *dir_path;
    int next_idx;
};

void action_assign_next(PlayerControl *control, void *user_data) {
    struct AssignNextData *d = user_data;
    SAFE_FREE(control->filename);
    control->filename = d->next_full_path;
    SAFE_FREE(control->current_filename);
    control->current_filename = d->next_basename;
    if (control->playlist) {
        free_names(control->playlist, control->playlist_size, 0);
    }
    control->playlist = d->files;
    control->playlist_size = d->file_count;
    control->playlist_capacity = d->file_count;
    control->playlist_mode = 1;
    SAFE_FREE(control->playlist_dir);
    assign_safe_strdup(&control->playlist_dir, d->dir_path);
    control->current_track = d->next_idx;
    reset_playback_fields(control);
    control->stop = 0;
    pthread_cond_signal(&control->cond);
}

struct StopData {
    int dummy;
};

void action_set_stop(PlayerControl *control, void *user_data) {
(void)user_data;
control->stop = 1;
}

void action_set_stop_playlist(PlayerControl *control, void *user_data) {
(void)user_data;
control->stop = 1;
control->playlist_mode = 0;
}

struct LoadMainData {
const char *dir_path;
};
static int load_raw_files(const char *dir_path, char ***files_out, int *count_out);
void action_load_main(PlayerControl *control, void *user_data) {
    struct LoadMainData *d = user_data;
    while (control->stop == 1) {
        struct timespec ts_wait;
        clock_gettime(CLOCK_REALTIME, &ts_wait);
        ts_wait.tv_sec += 1;
        int ret = pthread_cond_timedwait(&control->cond, &control->mutex, &ts_wait);
        if (ret == ETIMEDOUT) {
            display_message(ERROR, "Warning: audio stream not responding — continuing without waiting");
            break;
        }
    }
    if (control->playlist) {
        free_names(control->playlist, control->playlist_size, 0);
    }
    control->playlist = NULL;
    control->playlist_size = 0;
    control->playlist_capacity = 0;
    control->current_track = 0;
    control->playlist_dir = NULL;
    char **entries = NULL;
    int count = 0;
    if (load_raw_files(d->dir_path, &entries, &count) != 0 || count == 0) {
        if (entries) free(entries);
        control->playlist_mode = 0;
        return;
    }
    control->playlist = entries;
    control->playlist_capacity = count;
    control->playlist_size = count;
    control->current_track = 0;
    control->playlist_mode = 1;
    if (control->playlist_size > 0 && control->playlist[0]) {
        if (control->filename) free(control->filename);
        control->filename = strdup(control->playlist[0]);
        pthread_cond_signal(&control->cond);
        assign_safe_strdup(&control->playlist_dir, d->dir_path);
    }
}

struct LoopData {
    int loop;
};

void action_get_loop(PlayerControl *control, void *user_data) {
    struct LoopData *d = user_data;
    d->loop = control->loop_mode;
}

void with_mutex(PlayerControl *control, void (*action)(PlayerControl *, void *), void *user_data, int do_signal) {
    SAFE_MUTEX_LOCK(&control->mutex);
    if (action) action(control, user_data);
    if (do_signal) pthread_cond_signal(&control->cond);
    pthread_mutex_unlock(&control->mutex);
}

PlayerControl player_control = {
    .filename = NULL,
    .pause = 0,
    .stop = 0,
    .quit = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .current_file = NULL,
    .current_filename = NULL,
    .paused = 0,
    .loop_mode = 0,
    .playlist = NULL,
    .playlist_size = 0,
    .playlist_capacity = 0,
    .current_track = 0,
    .playlist_mode = 0,
    .seek_delta = 0,
    .duration = 0.0,
    .bytes_read = 0LL,
    .is_silent = 0,
    .fading_out = 0,
    .fading_in = 0,
    .current_fade = FADE_STEPS,
    .playlist_dir = NULL,
};

void top(WINDOW *win)
{
    draw_single_frame(win, 0, 3, "PATH", 0);
    char path_display[256];
    strncpy(path_display, current_dir, sizeof(path_display) - 1);
    path_display[sizeof(path_display) - 1] = '\0';
    SAFE_MUTEX_LOCK(&player_control.mutex);
    int is_playlist_active = player_control.playlist_mode &&
                             player_control.playlist_dir != NULL &&
                             (strcmp(player_control.playlist_dir, current_dir) == 0 ||
                              strstr(player_control.playlist_dir, current_dir) == player_control.playlist_dir);
	    pthread_mutex_unlock(&player_control.mutex);
    if (is_playlist_active) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_BLUE));
    }
wchar_t wpath[INNER_WIDTH + 1];
int max_path_width = INNER_WIDTH - 4;
int total_width = calculate_visual_width(path_display);
int max_offset = total_width - max_path_width;
if (max_offset < 0) max_offset = 0;
if (path_visual_offset > max_offset) path_visual_offset = max_offset;
prepare_display_wstring(path_display, max_path_width, wpath, sizeof(wpath)/sizeof(wchar_t), 0, L"..", path_visual_offset, 0);
mvwaddnwstr(win, 1, 2, wpath, wcslen(wpath));
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
void perform_seek(PlayerControl *control, snd_pcm_t *handle);
static int is_raw_file(const char *name) {
    if (!name) return 0;
    size_t len = strlen(name);
    return (len > 4) && (strcasecmp(name + len - 4, ".raw") == 0);
}
	char error_msg[256] = "";
	static int show_error = 0;
        static int error_toggle = 0;
	static int system_time_toggle = 0;

static void display_message(int type, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = (type == ERROR) ? error_msg : status_msg;
    size_t msg_size = sizeof(error_msg);
    vsnprintf(msg, msg_size, fmt, ap);
    if (type == ERROR) {
        show_error = 1;
    } else if (type == STATUS) {
        show_status = 1;
        status_start_time = time(NULL);
    }
    va_end(ap);
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
    wchar_t werror[256];
prepare_display_wstring(error_msg, actual_width - 4, werror, sizeof(werror)/sizeof(wchar_t), 0, L"..", 0, 0);
int error_len = wcswidth(werror, wcslen(werror));
int ex = (actual_width - error_len) / 2;
if (ex < 2) ex = 2;
wattron(win, COLOR_PAIR(COLOR_PAIR_RED));
mvwaddwstr(win, field_y + 1, ex, werror);
wattroff(win, COLOR_PAIR(COLOR_PAIR_RED));
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
wchar_t wtime[128];
prepare_display_wstring(time_str, actual_width - 4, wtime, sizeof(wtime)/sizeof(wchar_t), 0, L"..", 0, 0);
int time_len = wcswidth(wtime, wcslen(wtime));
int tx = (actual_width - time_len) / 2;
if (tx < 2) tx = 2;
mvwaddwstr(win, field_y + 1, tx, wtime);
        } else if (show_status) {
            clear_rect(win, field_y + 1, field_y + 2, 2, actual_width - 2);
wattron(win, COLOR_PAIR(COLOR_PAIR_YELLOW));
int status_width = actual_width - 4;
wchar_t wstatus[256];
prepare_display_wstring(status_msg, status_width, wstatus, sizeof(wstatus)/sizeof(wchar_t), 0, L"..", 0, 0);
mvwaddwstr(win, field_y + 1, 2, wstatus);
wattroff(win, COLOR_PAIR(COLOR_PAIR_YELLOW));
        } else {
SAFE_MUTEX_LOCK(&player_control.mutex);
            double duration = player_control.duration;
long long bytes_read = player_control.bytes_read;
            pthread_mutex_unlock(&player_control.mutex);
            if (duration > 0.0) {
                  long long total_bytes = (long long)(duration * 176400.0);
                  double percent = total_bytes > 0 ? ((double)bytes_read * 100.0) / total_bytes : 0.0;
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
free_names(forward_history, forward_count, 0);
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
        free_names(file_list, file_count, 1);
    }
    file_list = NULL;
    file_count = 0;
    selected_index = 0;
}

static int file_entry_cmp(const void *a, const void *b) {
    const FileEntry *entry_a = (const FileEntry *)a;
    const FileEntry *entry_b = (const FileEntry *)b;
    if (entry_a->is_dir != entry_b->is_dir) {
        return entry_b->is_dir - entry_a->is_dir;
    }
return strcasecmp(entry_a->name, entry_b->name);
}

void update_file_list(void) {
    free_file_list();
int count = 0;
FileEntry *entries = NULL;
if (scan_directory(current_dir, 0, (void **)&entries, &count, 0) != 0) {
    file_list = calloc(1, sizeof(FileEntry));
    file_list[0].name = strdup("(access denied)");
    file_list[0].is_dir = 0;
    file_count = 1;
    selected_index = 0;
    display_message(ERROR, "Access denied to: %s", current_dir);
    return;
} if (count == 0) {
    file_list = calloc(1, sizeof(FileEntry));
    if (file_list) {
        file_list[0].name = strdup("Directory is empty or not enough memory.");
        file_list[0].is_dir = 0;
        file_count = 1;
    }
    selected_index = 0;
    free(entries);
    return;
}
qsort(entries, count, sizeof(FileEntry), file_entry_cmp);
file_list = entries;
file_count = count;
selected_index = 0;
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
    SAFE_MUTEX_LOCK(&player_control.mutex);
    const char *active_filename = player_control.current_filename;
    if (!active_filename && player_control.filename) {
        active_filename = player_control.filename;
    }
    if (active_filename && strlen(active_filename) > 0) {
        const char *slash = strrchr(active_filename, '/');
    assign_safe_strdup(&current_file_name, slash ? slash + 1 : active_filename);
if (current_file_name) { current_paused = player_control.paused;
}
}
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
wchar_t wmsg[CURSOR_WIDTH + 4] = {0};
prepare_display_wstring(msg, CURSOR_WIDTH, wmsg, sizeof(wmsg)/sizeof(wchar_t), 0, L"..", 0, 0);
mvwaddwstr(win, msg_row, 3, wmsg);
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
int reserve =
    (file_list[i].is_dir ? 1 : 0) +
    2;

prepare_display_wstring(
    file_list[i].name,
    CURSOR_WIDTH - reserve,
    wname,
    sizeof(wname) / sizeof(wchar_t),
    file_list[i].is_dir,
    L"..",
    0,
    1
);
  int printed = wcswidth(wname, wcslen(wname));
if (!file_list[i].is_dir) {
    size_t wl = wcslen(wname);
    if (wl >= 2 &&
        wname[wl - 1] == L'.' &&
        wname[wl - 2] == L'.')
    {
        int last = wcwidth(wname[wl - 3]);
        if (last == 2) {
            printed += 1;
        }
    }
}
    if (i == selected_index) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
wchar_t fill_ch1 = L'▒';
draw_fill_line(win, row, 2, cursor_end - 2, &fill_ch1, 1, 1);
        wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
        int text_color = COLOR_PAIR_BORDER;
        if (file_list[i].is_dir && player_control.playlist_mode && 
            player_control.playlist_dir) 
        {
            char *full_folder_path = xasprintf("%s/%s", current_dir, file_list[i].name);
            char resolved_path[PATH_MAX];
            char resolved_playlist_dir[PATH_MAX];
            if (full_folder_path && 
                realpath(full_folder_path, resolved_path) != NULL &&
                realpath(player_control.playlist_dir, resolved_playlist_dir) != NULL &&
                strcmp(resolved_path, resolved_playlist_dir) == 0) {
                text_color = COLOR_PAIR_BLUE;
            }
            free(full_folder_path);
        }
        wattron(win, COLOR_PAIR(text_color));
        mvwaddwstr(win, row, 3, wname);
        wattroff(win, COLOR_PAIR(text_color));
        wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
    wchar_t fill_ch2 = L'▒';
draw_fill_line(win, row, 3 + printed, cursor_end - (3 + printed), &fill_ch2, 1, 1);
        wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));
        } else {
            int text_color = 0;
            if (player_control.playlist_mode && 
                player_control.playlist_dir && 
                file_list[i].is_dir) 
            {
                char *full_folder_path = xasprintf("%s/%s", current_dir, file_list[i].name);
                char resolved_path[PATH_MAX];
                char resolved_playlist_dir[PATH_MAX];
                if (full_folder_path && 
                    realpath(full_folder_path, resolved_path) != NULL &&
                    realpath(player_control.playlist_dir, resolved_playlist_dir) != NULL &&
                    strcmp(resolved_path, resolved_playlist_dir) == 0) {
                    text_color = COLOR_PAIR_BLUE;
                }
                free(full_folder_path);
            }
            if (text_color == 0) {
                if (current_file_name && 
                    strcmp(file_list[i].name, current_file_name) == 0 && 
                    is_raw_file(file_list[i].name)) {
                    text_color = current_paused ? COLOR_PAIR_YELLOW : COLOR_PAIR_BLUE;
                } else if (file_list[i].is_dir) {
                    text_color = 2;
                } else if (is_raw_file(file_list[i].name)) {
                    text_color = 3;
                }
            }
            if (text_color) wattron(win, COLOR_PAIR(text_color));
            mvwaddwstr(win, row, 3, wname);
            if (text_color) wattroff(win, COLOR_PAIR(text_color));
		wchar_t space_ch = L' ';
		draw_fill_line(win, row, 3 + printed, cursor_end - (3 + printed), &space_ch, 1, 1);
	    }
}
    if (file_count > visible_lines) {
        int scroll_height       = max_y - 8;
        float ratio             = (float)scroll_height / file_count;
        int scroll_bar_height   = (int)(visible_lines * ratio + 0.5f);
        if (scroll_bar_height < 1) scroll_bar_height = 1;
        if (scroll_bar_height > scroll_height) scroll_bar_height = scroll_height;

        int max_scroll_offset   = file_count - visible_lines;
        int scroll_pos          = 4;

        if (max_scroll_offset > 0) {
            float normalized_pos = (float)start_index / max_scroll_offset;
            scroll_pos = 4 + (int)(normalized_pos * (scroll_height - scroll_bar_height));
            if (scroll_pos < 4) scroll_pos = 4;
            if (scroll_pos > 4 + scroll_height - scroll_bar_height) scroll_pos = 4 + scroll_height - scroll_bar_height;
        }
        int bar_x = 2 + CURSOR_WIDTH + 1;
wchar_t empty_char = SCROLL_EMPTY;
wchar_t fill_char = SCROLL_FILLED;
COLOR_ATTR_ON(win, COLOR_PAIR_BORDER);
draw_fill_line(win, 4, bar_x, scroll_height, &empty_char, 0, 1);
draw_fill_line(win, scroll_pos, bar_x, scroll_bar_height, &fill_char, 0, 1);
COLOR_ATTR_OFF(win, COLOR_PAIR_BORDER);
    }
SAFE_FREE(current_file_name);
int last_list_row = end_index - start_index + 4;
int safe_clear_end = max_y - 5;
if (last_list_row < safe_clear_end) {
        clear_rect(win, last_list_row, safe_clear_end, 1, max_x - 1);
}
	draw_field_frame(win);
}

static void cleanup_playlist_and_filename(PlayerControl *control) {
    if (control->playlist) {
        free_names(control->playlist, control->playlist_size, 0);
        control->playlist = NULL;
        control->playlist_size = 0;
        control->playlist_capacity = 0;
        free(control->playlist_dir);
        control->playlist_dir = NULL;
    }
    if (control->filename) {
SAFE_FREE(control->filename);
    }
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
    safe_cleanup_resources(&file, &handle, &poll_fds, &control->current_filename);
    cleanup_playlist_and_filename(control);
    if (control->playlist_mode) {
	        display_message(STATUS, "End of playlist reached");
	    }
	    control->stop = 0;
	    control->duration = 0.0;
            control->bytes_read = 0LL;
	    control->paused = 0;
	    control->playlist_mode = 0;
	    control->current_track = 0;
	    pthread_cond_signal(&control->cond);
	    pthread_mutex_unlock(&control->mutex);
	    continue;
	}
        if (control->filename && (!control->current_filename || strcmp(control->filename, control->current_filename) != 0)) {
safe_cleanup_resources(&file, &handle, NULL, &control->current_filename);
    free(poll_fds); poll_fds = NULL;

    file = open_audio_file(control->filename);
    if (file) {
        struct stat st;
        if (fstat(fileno(file), &st) == 0) {
            long file_size = st.st_size;
            control->duration = (double)file_size / BYTES_PER_SECOND;
            control->bytes_read = 0LL;
        } else {
            control->duration = 0.0;
            control->bytes_read = 0LL;
        }

        handle = init_audio_device(rate, channels);
        if (!handle) {
            safe_cleanup_resources(&file, &handle, &poll_fds, NULL);
            pthread_mutex_unlock(&control->mutex);
            continue;
        }
        control->current_file = file;
control->current_filename = (control->filename) ? SAFE_STRDUP(control->filename) : NULL;
if (control->current_filename) {
                            control->is_silent = 0;
	                    control->fading_in = 0;
	                    control->fading_out = 0;
	                    control->current_fade = FADE_STEPS;
                    poll_count = snd_pcm_poll_descriptors_count(handle);
 if (poll_count > 0) {
     poll_fds = malloc(poll_count * sizeof(struct pollfd));
     if (poll_fds) {
         snd_pcm_poll_descriptors(handle, poll_fds, poll_count);
     } else {
safe_cleanup_resources(&file, &handle, NULL, NULL);
         continue;
     }
 }
                } else {
SAFE_FREE(control->filename);
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
SAFE_MUTEX_LOCK(&control->mutex);
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
            safe_cleanup_resources(&file, &handle, &poll_fds, &control->current_filename);
            control->current_track++;
            control->filename = SAFE_STRDUP(control->playlist[control->current_track]);
            if (!control->filename) {
                control->current_track = control->playlist_size;
            }
            pthread_mutex_unlock(&control->mutex);
            continue;
        } else if (control->loop_mode && !control->playlist_mode) {
            fseek(file, 0, SEEK_SET);
            control->bytes_read = 0LL;
            control->seek_delta = 0;
            pthread_mutex_unlock(&control->mutex);
            continue;
        } else {
            if (control->playlist_mode) {
                display_message(STATUS, "End of playlist reached");
            }
            if (control->playlist) {
                free_names(control->playlist, control->playlist_size, 0);
                control->playlist = NULL;
                control->playlist_size = 0;
                control->playlist_capacity = 0;
                free(control->playlist_dir);
                control->playlist_dir = NULL;
            }
            SAFE_FREE(control->filename);
            safe_cleanup_resources(&file, &handle, &poll_fds, &control->current_filename);
            control->playlist_mode = 0;
            control->duration = 0.0;
            control->bytes_read = 0LL;
            pthread_mutex_unlock(&control->mutex);
            continue;
        }
    } else {
        display_message(ERROR, "File read error");
        if (control->playlist) {
            free_names(control->playlist, control->playlist_size, 0);
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
        safe_cleanup_resources(&file, &handle, &poll_fds, &control->current_filename);
        cleanup_playlist_and_filename(control);
        control->playlist_mode = 0;
        control->duration = 0.0;
        control->bytes_read = 0LL;
        pthread_mutex_unlock(&control->mutex);
        continue;
    }
}

control->bytes_read += (long long)read_size;
}
pthread_mutex_unlock(&control->mutex);
	int actual_size = (read_size / 4) * 4;
	if (actual_size <= 0) continue;
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
    safe_cleanup_resources(&file, &handle, &poll_fds, &control->current_filename);
    cleanup_playlist_and_filename(control);
    return NULL;
}

void perform_seek(PlayerControl *control, snd_pcm_t *handle)
{
    if (!control->current_file) {
        control->seek_delta = 0;
        return;
    }
    if (control->seek_delta == 0)
        return;
    long long seek_bytes = (long long)control->seek_delta * BYTES_PER_SECOND;
    long long current_pos = ftell(control->current_file);
    long long new_pos = current_pos + seek_bytes;
    if (new_pos < 0)
        new_pos = 0;
    long original_pos = ftell(control->current_file);
    long long file_size = 0;
    if (fseek(control->current_file, 0, SEEK_END) == 0) {
        long long file_size = ftell(control->current_file);
        fseek(control->current_file, original_pos, SEEK_SET);
        if (new_pos > file_size)
            new_pos = file_size;
        control->duration = (double)file_size / BYTES_PER_SECOND;
    }
    new_pos = (new_pos / 4) * 4;
    if (new_pos > file_size)
        new_pos -= 4;
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
static const char *get_basename(const char *path) {
    return strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
}

static int playlist_cmp(const void *a, const void *b) {
    const char *path_a = *(const char **)a;
    const char *name_a = get_basename(path_a);
    const char *path_b = *(const char **)b;
    const char *name_b = get_basename(path_b);
    return strcasecmp(name_a, name_b);
}

static int load_raw_files(const char *dir_path, char ***files_out, int *count_out) {
    void *entries = NULL;
    if (scan_directory(dir_path, 1, &entries, count_out, 1) != 0) return -1;
    if (*count_out == 0) {
        free(entries);
        return 0;
    }
    if (entries && *count_out > 0) {
        qsort(entries, *count_out, sizeof(char *), playlist_cmp);
    }
    *files_out = (char **)entries;
    return 0;
}

void load_playlist(const char *dir_path, PlayerControl *control) {
with_mutex(control, action_set_stop_playlist, NULL, 1);
    usleep(100000);
    show_error = 0;
    error_msg[0] = '\0';
with_mutex(control, action_set_stop, NULL, 1);
struct LoadMainData data = {dir_path};
with_mutex(control, action_load_main, &data, 0);
}

void shutdown_player_thread(PlayerControl *control, pthread_t thread, int *have_player_thread) {
    if (!*have_player_thread) return;
    lock_and_signal(control, NULL);
    control->quit = 1;
    int has_active_file = 0;
SAFE_MUTEX_LOCK(&control->mutex);
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
        if (!joined){fflush(stderr);}
    } else {pthread_join(thread, NULL);}
}
void action_p(PlayerControl *control)
{
    if (!control->current_file) {
        display_message(STATUS, "Nothing to pause");
        return;
    }
    if (control->paused) {
        control->paused = 0;
        control->is_silent = 0;
        control->fading_in = 1;
        control->current_fade = 0;
        display_message(STATUS, "RESUMED (fading in)");
    } else {
        control->paused = 1;
        control->fading_out = 1;
        control->current_fade = FADE_STEPS;
        display_message(STATUS, "PAUSED (fading out)");
    }
}

void action_seek(PlayerControl *control, int delta, const char *msg_if_none) {
    if (control->current_file) {
        control->seek_delta = delta;
    } else {
        display_message(STATUS, "%s", msg_if_none);
    }
}

void action_n(PlayerControl *control) {
    if (!control) return;
    pthread_mutex_lock(&control->mutex);
    if (!control->current_file && !control->filename) {
        pthread_mutex_unlock(&control->mutex);
        display_message(STATUS, "Nothing is playing");
        return;
    }
    char *current_file = NULL;
    if (control->current_filename) {
        const char *slash = get_basename(control->current_filename);
        assign_safe_strdup(&current_file, slash);
    } else if (control->filename) {
const char *slash = get_basename(control->filename);
        assign_safe_strdup(&current_file, slash);
    }
    char *current_full = NULL;
    if (control->filename) assign_safe_strdup(&current_full, control->filename);
    pthread_mutex_unlock(&control->mutex);
    if (!current_file) {
        display_message(ERROR, "Cannot determine the current file");
        SAFE_FREE(current_full);
        return;
    }
    char dir_path[PATH_MAX];
    if (current_full && strchr(current_full, '/')) {
char *path_copy = NULL;
assign_safe_strdup(&path_copy, current_full);
        if (path_copy) {
            char *dir = dirname(path_copy);
            strncpy(dir_path, dir, sizeof(dir_path)-1);
            free(path_copy);
        } else {
            strncpy(dir_path, current_dir, sizeof(dir_path)-1);
        }
    } else {
        strncpy(dir_path, current_dir, sizeof(dir_path)-1);
    }
    dir_path[sizeof(dir_path)-1] = '\0';
char **files = NULL;
int file_count = 0;
if (load_raw_files(dir_path, &files, &file_count) != 0 || file_count == 0) {
    display_message(ERROR, "No RAW files in %s", dir_path);
    SAFE_FREE(current_file);
    SAFE_FREE(current_full);
    return;
}
    int current_idx = -1;
    for (int i = 0; i < file_count; i++) {
        char *full_path = files[i];
        const char *name_in_list = get_basename(full_path);
        if (name_in_list && current_file && strcmp(name_in_list, current_file) == 0) {
            current_idx = i;
            break;
        }
    }
    int next_idx;
    if (current_idx == -1) {
        display_message(STATUS, "File '%s' not found, starting from the first", current_file);
        next_idx = 0;
    } else if (current_idx < file_count - 1) {
        next_idx = current_idx + 1;
    } else {
struct LoopData data;
with_mutex(control, action_get_loop, &data, 0);
int loop = data.loop;
        if (loop) {
            next_idx = 0;
            display_message(STATUS, "Beginning of the list (loop)");
        } else {
            display_message(STATUS, "End of the file list");
cleanup_playlist_resources(files, file_count, current_file, current_full);
            return;
        }
    }
    if (next_idx < 0 || next_idx >= file_count) {
        display_message(ERROR, "Error: invalid index %d", next_idx);
cleanup_playlist_resources(files, file_count, current_file, current_full);
        return;
    }
char *next_full_path = NULL;
assign_safe_strdup(&next_full_path, files[next_idx]);
if (!next_full_path) {
    display_message(ERROR, "Not enough memory for the path");
    cleanup_playlist_resources(files, file_count, current_file, current_full);
    return;
}
const char *slash = get_basename(next_full_path);
char *next_basename = NULL;
assign_safe_strdup(&next_basename, slash);
if (!next_basename) {
    display_message(ERROR, "Not enough memory for the name");
    free(next_full_path);
    cleanup_playlist_resources(files, file_count, current_file, current_full);
    return;
}
with_mutex(control, action_set_stop, NULL, 1);
    struct timespec ts = {0, 50000000};
    nanosleep(&ts, NULL);
struct AssignNextData data = {next_full_path, next_basename, files, file_count, dir_path, next_idx};
with_mutex(control, action_assign_next, &data, 0);
    SAFE_FREE(current_file);
    SAFE_FREE(current_full);
    display_message(STATUS, "Next: %s", next_basename);
}

void action_s(PlayerControl *control) {
    control->stop = 1;
    control->duration = 0.0;
    control->bytes_read = 0LL;
    show_error = 0;
    error_msg[0] = '\0';
    if (control->current_filename) {
        free(control->current_filename);
        control->current_filename = NULL;
    }
    control->paused = 0;
    control->playlist_mode = 0;
    control->current_track = 0;
    display_message(STATUS, "Playback stopped");
}

void lock_and_signal(PlayerControl *control, void (*action)(PlayerControl *)) {
    pthread_mutex_lock(&control->mutex);
    if (action) action(control);
    pthread_cond_signal(&control->cond);
    pthread_mutex_unlock(&control->mutex);
}

void lock_and_signal_seek(PlayerControl *control, int delta, const char *msg) {
    SAFE_MUTEX_LOCK(&player_control.mutex);
    action_seek(control, delta, msg);
    pthread_cond_signal(&control->cond);
    pthread_mutex_unlock(&control->mutex);
}

static void start_playback(const char *full_path, const char *file_name, int enable_loop) {
    if (!full_path || !file_name) return;
    show_error = 0;
    error_msg[0] = '\0';
    struct stat st;
    if (stat(full_path, &st) == 0) {
        if (st.st_size == 0) {
            display_message(ERROR, "File '%s' is empty (0 bytes)", file_name);
            return;
        }
	if (st.st_size < 4) {
	    display_message(ERROR, "File '%s' too small for a single frame (%ld bytes < 4)", file_name, st.st_size);
	    return;
	}
	if (st.st_size % 4 != 0) {
	    display_message(ERROR,
	        "File '%s': size %ld bytes not multiple of 4. "
	        "Required: stereo 16-bit RAW (2ch × 2bytes = 4bytes/frame)",
	        file_name, st.st_size);
	    return;
	}
    FILE *temp_file = fopen(full_path, "rb");
    if (!temp_file) {
        display_message(ERROR, "Failed to open for validation: %s", file_name);
        return;
    }
    char header[4] = {0};
    size_t read = fread(header, 1, 4, temp_file);
    fclose(temp_file);
    if (read >= 4) {
        if (memcmp(header, "RIFF", 4) == 0 || memcmp(header, "OggS", 4) == 0 ||
            memcmp(header, "fLaC", 4) == 0 || memcmp(header, "FORM", 4) == 0) {
            display_message(ERROR, "File '%s' appears to be formatted audio (e.g., WAV/OGG/FLAC/AIFF), not raw PCM", file_name);
            return;
        }
    }
        if (st.st_size < 1024) {
            display_message(STATUS,
                "File '%s' is very small (%ld bytes). Playback may be short.",
                file_name, st.st_size);
        }
    } else {
        display_message(ERROR, "Cannot access file: %s", file_name);
        return;
    }
    pthread_mutex_lock(&player_control.mutex);
    assign_safe_strdup(&player_control.filename, full_path);
    if (!player_control.filename) {
        display_message(STATUS, "Out of memory! Cannot play file.");
        pthread_mutex_unlock(&player_control.mutex);
        return;
    }
    cleanup_playlist(&player_control);
    player_control.loop_mode = enable_loop;
    reset_playback_fields(&player_control);
    assign_safe_strdup(&player_control.current_filename, file_name);
    pthread_cond_signal(&player_control.cond);
    if (enable_loop) {
        display_message(STATUS, "Playback started in loop mode on new file");
    }
    pthread_mutex_unlock(&player_control.mutex);
}

int navigate_dir(const char *target_dir, char *status_buf)
{
    if (!target_dir || !status_buf) return -1;

    if (chdir(target_dir) == 0) {
        if (getcwd(current_dir, PATH_MAX) == NULL) {
            display_message(ERROR, "getcwd failed - path may be too long");
            return -1;
        }
        update_file_list();
        status_buf[0] = '\0';
        return 0;
    } else {
   display_message(ERROR, ACCESS_DENIED_MSG, target_dir);
        return -1;
    }
}

int navigate_and_play(void) {
    init_ncurses("en_US.UTF-8");
    curs_set(0);
    clearok(stdscr, TRUE);
    idlok(stdscr, TRUE);
    scrollok(stdscr, FALSE);
    getmaxyx(stdscr, term_height, term_width);
    draw_single_frame(stdscr, 0, term_height, "GRANNIK | COMPLEX SOFTWARE ECOSYSTEM | FILE NAVIGATOR RAW", 1);
    wnoutrefresh(stdscr);
    doupdate();

    if (term_width < MIN_WIDTH || term_height < MIN_HEIGHT) {
        endwin();
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
fprintf(stderr, "Error: not enough memory for navigation history — function disabled\n");
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
    free_forward_history();
    update_file_list();
    draw_file_list(list_win);
    refresh();
static int help_mode = 0;
static int help_start_index = 0;
int ch;
	while (1) {
	    ch = wgetch(list_win);
if (help_mode) {
if (ch == KEY_SR) {
        if (help_start_index > 0)
            help_start_index--;
        continue;
    }
if (ch == KEY_SF) {
        int max_y = getmaxy(list_win);
        int visible_lines = max_y - 6;
        int help_count = 48;
        if (help_start_index < help_count - visible_lines)
            help_start_index++;
        continue;
    }
    if (ch == 'h' || ch == 'H') {
        help_mode = 0;
        continue;
    }
}
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
	if (help_mode)
	    draw_help(list_win, help_start_index);
	else
	    draw_file_list(list_win);
		        }
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
                    SAFE_FREE(forward_history[forward_count - 1]);
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
                        start_playback(full_path, file_list[selected_index].name, 0);
                        free(full_path);
                    } else {
                        display_message(ERROR, "Not a .raw file");
                    }
	                } else {
   int dir_fd = openat(AT_FDCWD, file_list[selected_index].name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir_fd == -1) {
display_message(ERROR, ACCESS_DENIED_MSG, xasprintf("%s/%s", current_dir, file_list[selected_index].name));
        break;
    }
    struct stat st;
    if (fstat(dir_fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        display_message(ERROR, "Not a directory or state changed: %s/%s", current_dir, file_list[selected_index].name);
        close(dir_fd);
        break;
    }
    if (fchdir(dir_fd) != 0) {
display_message(ERROR, ACCESS_DENIED_MSG, xasprintf("%s/%s", current_dir, file_list[selected_index].name));
        close(dir_fd);
        break;
    }
                    close(dir_fd);
	                    char old_dir[PATH_MAX];
                    SAFE_STRNCPY(old_dir, current_dir, sizeof(old_dir));
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
case 'p':
lock_and_signal(&player_control, action_p);
break;
case 'f':
case 'b': {
    int delta = (ch == 'f') ? 10 : -10;
    const char *msg = (ch == 'f') ? "Nothing to fast-forward" : "Nothing to rewind";
    lock_and_signal_seek(&player_control, delta, msg);
    break;
}
case 's':
lock_and_signal(&player_control, action_s);
break;
case 'h':
    help_mode = !help_mode;
    break;
case 't':
    system_time_toggle = !system_time_toggle;
    error_toggle = 0;
    if (help_mode) {
        draw_help(list_win, help_start_index);
    }
    break;
case ' ':
    if (file_count > 0 && selected_index >= 0 && file_list && file_list[selected_index].name && file_list[selected_index].is_dir) {
        char *full_path = xasprintf("%s/%s", current_dir, file_list[selected_index].name);
        if (!full_path) {
            display_message(STATUS, "Out of memory!");
        } else {
            load_playlist(full_path, &player_control);
            free(full_path);
            if (player_control.playlist_size == 0) {
                display_message(ERROR, "Directory does not contain raw files.");
            } else {
                display_message(STATUS, "Playlist loaded from %s", file_list[selected_index].name);
            }
        }
    } else {
        display_message(STATUS, "Select a directory for playlist!");
    }
    break;
case 'a':
    load_playlist(current_dir, &player_control);
    if (player_control.playlist_size == 0) {
        display_message(ERROR, "No .raw files in current directory.");
    } else {
        display_message(STATUS, "Playlist loaded from current directory");
    }
    break;
	case 'l':
	{
	    int different = 0;
	    const char *selected_name = NULL;
	    pthread_mutex_lock(&player_control.mutex);
	    if (file_count > 0 && selected_index >= 0 && file_list && file_list[selected_index].name &&
	        !file_list[selected_index].is_dir && is_raw_file(file_list[selected_index].name)) {
	        selected_name = file_list[selected_index].name;
	        different = selected_name &&
	            (!player_control.current_filename || strcmp(player_control.current_filename, selected_name) != 0);
	    }
	    pthread_mutex_unlock(&player_control.mutex);
	    if (different && selected_name) {
	        char *full_path = xasprintf("%s/%s", current_dir, selected_name);
	        if (full_path) {
	            start_playback(full_path, selected_name, 1);
	            free(full_path);
	        } else {
	            display_message(STATUS, "Out of memory! Cannot play file.");
	        }
	    } else {
	        pthread_mutex_lock(&player_control.mutex);
	        if (player_control.current_file || player_control.filename) {
	            player_control.loop_mode = !player_control.loop_mode;
	            display_message(STATUS, "%s", player_control.loop_mode ? "Loop mode enabled" : "Loop mode disabled");
	        } else {
	            display_message(STATUS, "Nothing to loop");
	        }
	        pthread_mutex_unlock(&player_control.mutex);
	    }
	    break;
	}
case 'n':
    SAFE_MUTEX_LOCK(&player_control.mutex);
    action_n(&player_control);
    pthread_mutex_unlock(&player_control.mutex);
    break;
case KEY_SLEFT:
    path_visual_offset = (path_visual_offset - 5 > 0) ? path_visual_offset - 5 : 0;
    break;
case KEY_SRIGHT:
    path_visual_offset += 5;
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

static void log_player_thread_disabled(void)
{
    display_message(ERROR, "pthread_create failed — player disabled");
}

static int try_start_player_thread(pthread_t *thread,
                                   void *(*func)(void *),
                                   void *arg)
{
if (pthread_create(thread, NULL, func, arg) == 0) {
    return 1;
}
    log_player_thread_disabled();
    return 0;
}

static void handle_program_exit(
    int result,
    int was_playing,
    int hours,
    int mins,
    int secs
) {
    if (result == 0) {
        endwin_and_clear_screen();

        if (was_playing) {
            printf(
                "\033[31mWARNING: Audio thread did not finish in 3s — "
                "interrupted at %02d:%02d:%02d — "
                "leaving it (safer than pthread_cancel)\033[0m\n",
                hours, mins, secs
            );
        } else {
            printf(" END\n");
        }
    } else {
        printf(
            "\033[31m Terminal too small! Minimum: %dx%d\033[0m\n",
            MIN_WIDTH, MIN_HEIGHT
        );
    }
}

static int handle_initial_directory(int argc, char *argv[]) {
    if (argc > 1) {
        if (chdir(argv[1]) != 0) {
            display_message(ERROR, "Failed to change to directory: %s", argv[1]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (handle_initial_directory(argc, argv) != 0) {return -1;}
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); // verification!
pthread_mutex_init(&player_control.mutex, &attr);
pthread_mutexattr_destroy(&attr);
    pthread_t thread = 0;
    int have_player_thread = 0;
    have_player_thread =
    try_start_player_thread(&thread,
                            player_thread,
                            &player_control);
int result = navigate_and_play();
SAFE_MUTEX_LOCK(&player_control.mutex);
bool was_playing = (player_control.current_filename != NULL);
double elapsed = 0.0;
if (was_playing) {
    elapsed = (double)player_control.bytes_read / BYTES_PER_SECOND;
}
int hours = (int)elapsed / 3600;
int mins = ((int)elapsed % 3600) / 60;
int secs = (int)elapsed % 60;
pthread_mutex_unlock(&player_control.mutex);
if (have_player_thread) {
    shutdown_player_thread(&player_control, thread, &have_player_thread);
}
lock_and_signal(&player_control, cleanup_playlist_and_filename);
if (have_player_thread) {
    pthread_mutex_destroy(&player_control.mutex);
    pthread_cond_destroy(&player_control.cond);
}
handle_program_exit(result, was_playing, hours, mins, secs);
return result;
}
// 2354
