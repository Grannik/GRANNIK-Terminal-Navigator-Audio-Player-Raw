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

void draw_file_list(WINDOW *win);

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

static int has_wide_chars(const wchar_t *w, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (wcwidth(w[i]) == 2)
            return 1;
    }
    return 0;
}

static int is_double_width(wchar_t c) {
    if ((c >= 0x1100 && c <= 0x115F) ||
        (c >= 0x231A && c <= 0x231B) ||
        (c >= 0x2329 && c <= 0x232A) ||
        (c >= 0x23E9 && c <= 0x23EC) ||
        (c >= 0x23F0 && c <= 0x23F0) ||
        (c >= 0x23F3 && c <= 0x23F3) ||
        (c >= 0x25FD && c <= 0x25FE) ||
        (c >= 0x2614 && c <= 0x2615) ||
        (c >= 0x2648 && c <= 0x2653) ||
        (c >= 0x267F && c <= 0x267F) ||
        (c >= 0x2693 && c <= 0x2693) ||
        (c >= 0x26A1 && c <= 0x26A1) ||
        (c >= 0x26AA && c <= 0x26AB) ||
        (c >= 0x26BD && c <= 0x26BE) ||
        (c >= 0x26C4 && c <= 0x26C5) ||
        (c >= 0x26CE && c <= 0x26CE) ||
        (c >= 0x26D4 && c <= 0x26D4) ||
        (c >= 0x26EA && c <= 0x26EA) ||
        (c >= 0x26F2 && c <= 0x26F3) ||
        (c >= 0x26F5 && c <= 0x26F5) ||
        (c >= 0x26FA && c <= 0x26FA) ||
        (c >= 0x26FD && c <= 0x26FD) ||
        (c >= 0x2705 && c <= 0x2705) ||
        (c >= 0x270A && c <= 0x270B) ||
        (c >= 0x2728 && c <= 0x2728) ||
        (c >= 0x274C && c <= 0x274C) ||
        (c >= 0x274E && c <= 0x274E) ||
        (c >= 0x2753 && c <= 0x2755) ||
        (c >= 0x2757 && c <= 0x2757) ||
        (c >= 0x2795 && c <= 0x2797) ||
        (c >= 0x27B0 && c <= 0x27B0) ||
        (c >= 0x27BF && c <= 0x27BF) ||
        (c >= 0x2B1B && c <= 0x2B1C) ||
        (c >= 0x2B50 && c <= 0x2B50) ||
        (c >= 0x2B55 && c <= 0x2B55) ||
        (c >= 0x2E80 && c <= 0x2E99) ||
        (c >= 0x2E9B && c <= 0x2EF3) ||
        (c >= 0x2F00 && c <= 0x2FD5) ||
        (c >= 0x3000 && c <= 0x3000) ||
        (c >= 0x3001 && c <= 0x3003) ||
        (c >= 0x3004 && c <= 0x3004) ||
        (c >= 0x3005 && c <= 0x3005) ||
        (c >= 0x3006 && c <= 0x3006) ||
        (c >= 0x3007 && c <= 0x3007) ||
        (c >= 0x3008 && c <= 0x3009) ||
        (c >= 0x300A && c <= 0x300B) ||
        (c >= 0x300C && c <= 0x300D) ||
        (c >= 0x300E && c <= 0x300F) ||
        (c >= 0x3010 && c <= 0x3011) ||
        (c >= 0x3012 && c <= 0x3013) ||
        (c >= 0x3014 && c <= 0x3015) ||
        (c >= 0x3016 && c <= 0x3017) ||
        (c >= 0x3018 && c <= 0x3019) ||
        (c >= 0x301A && c <= 0x301B) ||
        (c >= 0x3020 && c <= 0x3020) ||
        (c >= 0x3021 && c <= 0x3029) ||
        (c >= 0x302A && c <= 0x302D) ||
        (c >= 0x302E && c <= 0x302F) ||
        (c >= 0x3030 && c <= 0x3030) ||
        (c >= 0x3031 && c <= 0x3035) ||
        (c >= 0x3036 && c <= 0x3037) ||
        (c >= 0x3038 && c <= 0x303A) ||
        (c >= 0x303B && c <= 0x303B) ||
        (c >= 0x303C && c <= 0x303C) ||
        (c >= 0x303D && c <= 0x303D) ||
        (c >= 0x303E && c <= 0x303E) ||
        (c >= 0x3041 && c <= 0x3096) ||
        (c >= 0x3099 && c <= 0x309A) ||
        (c >= 0x309B && c <= 0x309C) ||
        (c >= 0x309D && c <= 0x309E) ||
        (c >= 0x309F && c <= 0x309F) ||
        (c >= 0x30A0 && c <= 0x30A0) ||
        (c >= 0x30A1 && c <= 0x30FA) ||
        (c >= 0x30FB && c <= 0x30FB) ||
        (c >= 0x30FC && c <= 0x30FE) ||
        (c >= 0x30FF && c <= 0x30FF) ||
        (c >= 0x3105 && c <= 0x312F) ||
        (c >= 0x3131 && c <= 0x318E) ||
        (c >= 0x3190 && c <= 0x3191) ||
        (c >= 0x3192 && c <= 0x3195) ||
        (c >= 0x3196 && c <= 0x319F) ||
        (c >= 0x31A0 && c <= 0x31BF) ||
        (c >= 0x31C0 && c <= 0x31E5) ||
        (c >= 0x31EF && c <= 0x31EF) ||
        (c >= 0x31F0 && c <= 0x31FF) ||
        (c >= 0x3200 && c <= 0x321E) ||
        (c >= 0x3220 && c <= 0x3229) ||
        (c >= 0x322A && c <= 0x3247) ||
        (c >= 0x3250 && c <= 0x3250) ||
        (c >= 0x3251 && c <= 0x325F) ||
        (c >= 0x3260 && c <= 0x327F) ||
        (c >= 0x3280 && c <= 0x3289) ||
        (c >= 0x328A && c <= 0x32B0) ||
        (c >= 0x32B1 && c <= 0x32BF) ||
        (c >= 0x32C0 && c <= 0x32FF) ||
        (c >= 0x3300 && c <= 0x33FF) ||
        (c >= 0x3400 && c <= 0x4DBF) ||
        (c >= 0x4DC0 && c <= 0x4DFF) ||
        (c >= 0x4E00 && c <= 0x9FFF) ||
        (c >= 0xA000 && c <= 0xA014) ||
        (c >= 0xA015 && c <= 0xA015) ||
        (c >= 0xA016 && c <= 0xA48C) ||
        (c >= 0xA490 && c <= 0xA4C6) ||
        (c >= 0xAC00 && c <= 0xD7A3) ||
        (c >= 0xF900 && c <= 0xFA6D) ||
        (c >= 0xFA70 && c <= 0xFAD9) ||
        (c >= 0xFE10 && c <= 0xFE16) ||
        (c >= 0xFE17 && c <= 0xFE18) ||
        (c >= 0xFE30 && c <= 0xFE30) ||
        (c >= 0xFE31 && c <= 0xFE32) ||
        (c >= 0xFE33 && c <= 0xFE34) ||
        (c >= 0xFE35 && c <= 0xFE35) ||
        (c >= 0xFE36 && c <= 0xFE36) ||
        (c >= 0xFE37 && c <= 0xFE38) ||
        (c >= 0xFE39 && c <= 0xFE3A) ||
        (c >= 0xFE3B && c <= 0xFE3C) ||
        (c >= 0xFE3D && c <= 0xFE3E) ||
        (c >= 0xFE3F && c <= 0xFE40) ||
        (c >= 0xFE41 && c <= 0xFE42) ||
        (c >= 0xFE43 && c <= 0xFE44) ||
        (c >= 0xFE45 && c <= 0xFE46) ||
        (c >= 0xFE47 && c <= 0xFE48) ||
        (c >= 0xFE49 && c <= 0xFE4C) ||
        (c >= 0xFE4D && c <= 0xFE4F) ||
        (c >= 0xFE50 && c <= 0xFE52) ||
        (c >= 0xFE54 && c <= 0xFE57) ||
        (c >= 0xFE58 && c <= 0xFE58) ||
        (c >= 0xFE59 && c <= 0xFE5A) ||
        (c >= 0xFE5B && c <= 0xFE5C) ||
        (c >= 0xFE5D && c <= 0xFE5E) ||
        (c >= 0xFE5F && c <= 0xFE61) ||
        (c >= 0xFE62 && c <= 0xFE62) ||
        (c >= 0xFE63 && c <= 0xFE63) ||
        (c >= 0xFE64 && c <= 0xFE66) ||
        (c >= 0xFE68 && c <= 0xFE68) ||
        (c >= 0xFE69 && c <= 0xFE69) ||
        (c >= 0xFE6A && c <= 0xFE6B) ||
        (c >= 0xFF01 && c <= 0xFF03) ||
        (c >= 0xFF04 && c <= 0xFF04) ||
        (c >= 0xFF05 && c <= 0xFF07) ||
        (c >= 0xFF08 && c <= 0xFF09) ||
        (c >= 0xFF0A && c <= 0xFF0A) ||
        (c >= 0xFF0B && c <= 0xFF0B) ||
        (c >= 0xFF0C && c <= 0xFF0C) ||
        (c >= 0xFF0D && c <= 0xFF0D) ||
        (c >= 0xFF0E && c <= 0xFF0F) ||
        (c >= 0xFF10 && c <= 0xFF19) ||
        (c >= 0xFF1A && c <= 0xFF1B) ||
        (c >= 0xFF1C && c <= 0xFF1E) ||
        (c >= 0xFF1F && c <= 0xFF20) ||
        (c >= 0xFF21 && c <= 0xFF3A) ||
        (c >= 0xFF3B && c <= 0xFF3B) ||
        (c >= 0xFF3C && c <= 0xFF3C) ||
        (c >= 0xFF3D && c <= 0xFF3D) ||
        (c >= 0xFF3E && c <= 0xFF3E) ||
        (c >= 0xFF3F && c <= 0xFF3F) ||
        (c >= 0xFF40 && c <= 0xFF40) ||
        (c >= 0xFF41 && c <= 0xFF5A) ||
        (c >= 0xFF5B && c <= 0xFF5B) ||
        (c >= 0xFF5C && c <= 0xFF5C) ||
        (c >= 0xFF5D && c <= 0xFF5D) ||
        (c >= 0xFF5E && c <= 0xFF5E) ||
        (c >= 0xFF5F && c <= 0xFF5F) ||
        (c >= 0xFF60 && c <= 0xFF60) ||
        (c >= 0xFF61 && c <= 0xFF61) ||
        (c >= 0xFF62 && c <= 0xFF62) ||
        (c >= 0xFF63 && c <= 0xFF63) ||
        (c >= 0xFF64 && c <= 0xFF65) ||
        (c >= 0xFF66 && c <= 0xFF6F) ||
        (c >= 0xFF70 && c <= 0xFF70) ||
        (c >= 0xFF71 && c <= 0xFF9D) ||
        (c >= 0xFF9E && c <= 0xFF9F) ||
        (c >= 0xFFA0 && c <= 0xFFBE) ||
        (c >= 0xFFC2 && c <= 0xFFC7) ||
        (c >= 0xFFCA && c <= 0xFFCF) ||
        (c >= 0xFFD2 && c <= 0xFFD7) ||
        (c >= 0xFFDA && c <= 0xFFDC) ||
        (c >= 0xFFE0 && c <= 0xFFE1) ||
        (c >= 0xFFE2 && c <= 0xFFE2) ||
        (c >= 0xFFE3 && c <= 0xFFE3) ||
        (c >= 0xFFE4 && c <= 0xFFE4) ||
        (c >= 0xFFE5 && c <= 0xFFE6) ||
        (c >= 0x1AFF0 && c <= 0x1AFF3) ||
        (c >= 0x1AFF5 && c <= 0x1AFFB) ||
        (c >= 0x1AFFD && c <= 0x1AFFE) ||
        (c >= 0x1B000 && c <= 0x1B122) ||
        (c >= 0x1B132 && c <= 0x1B132) ||
        (c >= 0x1B150 && c <= 0x1B152) ||
        (c >= 0x1B155 && c <= 0x1B155) ||
        (c >= 0x1B164 && c <= 0x1B167) ||
        (c >= 0x1B170 && c <= 0x1B2FB) ||
        (c >= 0x1D300 && c <= 0x1D356) ||
        (c >= 0x1D360 && c <= 0x1D376) ||
        (c >= 0x1F004 && c <= 0x1F004) ||
        (c >= 0x1F0CF && c <= 0x1F0CF) ||
        (c >= 0x1F18E && c <= 0x1F18E) ||
        (c >= 0x1F191 && c <= 0x1F19A) ||
        (c >= 0x1F1E6 && c <= 0x1F1FF) ||
        (c >= 0x1F200 && c <= 0x1F202) ||
        (c >= 0x1F210 && c <= 0x1F23B) ||
        (c >= 0x1F240 && c <= 0x1F248) ||
        (c >= 0x1F250 && c <= 0x1F251) ||
        (c >= 0x1F260 && c <= 0x1F265) ||
        (c >= 0x1F300 && c <= 0x1F320) ||
        (c >= 0x1F32D && c <= 0x1F335) ||
        (c >= 0x1F337 && c <= 0x1F37C) ||
        (c >= 0x1F37E && c <= 0x1F393) ||
        (c >= 0x1F3A0 && c <= 0x1F3CA) ||
        (c >= 0x1F3CF && c <= 0x1F3D3) ||
        (c >= 0x1F3E0 && c <= 0x1F3F0) ||
        (c >= 0x1F3F4 && c <= 0x1F3F4) ||
        (c >= 0x1F3F8 && c <= 0x1F43E) ||
        (c >= 0x1F440 && c <= 0x1F440) ||
        (c >= 0x1F442 && c <= 0x1F4FC) ||
        (c >= 0x1F4FF && c <= 0x1F53D) ||
        (c >= 0x1F54B && c <= 0x1F54E) ||
        (c >= 0x1F550 && c <= 0x1F567) ||
        (c >= 0x1F57A && c <= 0x1F57A) ||
        (c >= 0x1F595 && c <= 0x1F596) ||
        (c >= 0x1F5A4 && c <= 0x1F5A4) ||
        (c >= 0x1F5FB && c <= 0x1F64F) ||
        (c >= 0x1F680 && c <= 0x1F6C5) ||
        (c >= 0x1F6CC && c <= 0x1F6CC) ||
        (c >= 0x1F6D0 && c <= 0x1F6D2) ||
        (c >= 0x1F6D5 && c <= 0x1F6D7) ||
        (c >= 0x1F6DC && c <= 0x1F6DF) ||
        (c >= 0x1F6EB && c <= 0x1F6EC) ||
        (c >= 0x1F6F4 && c <= 0x1F6FC) ||
        (c >= 0x1F7E0 && c <= 0x1F7EB) ||
        (c >= 0x1F7F0 && c <= 0x1F7F0) ||
        (c >= 0x1F90C && c <= 0x1F93A) ||
        (c >= 0x1F93C && c <= 0x1F945) ||
        (c >= 0x1F947 && c <= 0x1F9FF) ||
        (c >= 0x1FA70 && c <= 0x1FA7C) ||
        (c >= 0x1FA80 && c <= 0x1FA88) ||
        (c >= 0x1FA90 && c <= 0x1FABD) ||
        (c >= 0x1FABF && c <= 0x1FAC5) ||
        (c >= 0x1FACE && c <= 0x1FADB) ||
        (c >= 0x1FAE0 && c <= 0x1FAE8) ||
        (c >= 0x1FAF0 && c <= 0x1FAF8) ||
        (c >= 0x20000 && c <= 0x2A6DF) ||
        (c >= 0x2A700 && c <= 0x2B739) ||
        (c >= 0x2B740 && c <= 0x2B81D) ||
        (c >= 0x2B820 && c <= 0x2CEA1) ||
        (c >= 0x2CEB0 && c <= 0x2EBE0) ||
        (c >= 0x2EBF0 && c <= 0x2EE5D) ||
        (c >= 0x2F800 && c <= 0x2FA1D) ||
        (c >= 0x30000 && c <= 0x3134A) ||
        (c >= 0x31350 && c <= 0x323AF)) {
        return 1;
    }
    return 0;
}

static inline int safe_wcwidth(wchar_t c) {
    int w = wcwidth(c);
    if (w < 0) {
        if (is_double_width(c)) return 2;
        else return 1;
    }
    return w;
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
            if ((unsigned char)*p <= 127) {
                wsrc[idx++] = (wchar_t)(unsigned char)*p;
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

static int calculate_visual_width(const char *src);
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
    for (size_t j = 0; j < ell_len; j++) {
    ell_width += safe_wcwidth(ellipsis[j]);
    }
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
        int front_width = remaining_width / 2;
        int back_width = remaining_width - front_width;
if (has_wide_chars(wsrc, wlen)) {
    if (front_width % 2 != 0) {
        front_width--;
        back_width++;
    }
}
        size_t i = 0;
        while (i < wlen && dest_idx < dest_size - 1 && current_width < front_width) {
int char_width = safe_wcwidth(wsrc[i]);
            if (current_width + char_width > front_width) break;
            dest[dest_idx++] = wsrc[i++];
            current_width += char_width;
        }
        for (size_t j = 0; j < ell_len && dest_idx < dest_size - 1; j++) {
            dest[dest_idx++] = ellipsis[j];
        }
        current_width += ell_width;
        size_t back_start = wlen;
        int back_current = 0;
        while (back_start > i && back_current < back_width) {
            back_start--;
int char_width = safe_wcwidth(wsrc[back_start]);
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
int char_width = safe_wcwidth(wsrc[start_i]);
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
int char_width = safe_wcwidth(wsrc[i]);
            if (current_width + char_width > remaining_width) break;
            dest[dest_idx++] = wsrc[i++];
            current_width += char_width;
        }
        if (need_end_ellipsis) {
            for (size_t j = 0; j < ell_len && dest_idx < dest_size - 1; j++) {
                dest[dest_idx++] = ellipsis[j];
            }
        }
    }
    dest[dest_idx] = L'\0';
    if (add_suffix) {
        wchar_t suffix = L'/';
int suffix_width = safe_wcwidth(suffix);
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
    for (size_t k = 0; k < wlen; k++) {
total_width += safe_wcwidth(wsrc[k]);
    }
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
static int is_raw_file(const char *name);
__attribute__((unused)) static char *xasprintf(const char *fmt, ...);
static void free_names(void *entries, int count, int is_file_entry);
static int scan_directory(const char *dir_path, int filter_raw, void **entries_out, int *count_out, int for_playlist) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }
struct dirent *entry;
int count = 0;
while ((entry = readdir(dir))) {
    if (entry->d_name[0] == '.') continue;
    if (filter_raw && !is_raw_file(entry->d_name)) continue;
    count++;
}
rewinddir(dir);
    if (count == 0) {
        closedir(dir);
        return 0;
    }
    size_t entry_size = for_playlist ? sizeof(char *) : sizeof(FileEntry);
    size_t capacity = 16;
    void *entries = calloc(capacity, entry_size);
    if (!entries) {
        closedir(dir);
        return -1;
    }
size_t idx = 0;
    while ((entry = readdir(dir))) {
        if (idx >= capacity) {
            capacity *= 2;
            void *new_entries = realloc(entries, capacity * entry_size);
            if (!new_entries) {
                free_names(entries, idx, !for_playlist);
                closedir(dir);
                return -1;
            }
            entries = new_entries;
        }
        if (entry->d_name[0] == '.') continue;
        if (filter_raw && !is_raw_file(entry->d_name)) continue;
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
            ((FileEntry *)entries)[idx].is_dir = (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
            ((FileEntry *)entries)[idx].name = name;
            free(full_path);
        }
        idx++;
    }
    closedir(dir);
    *entries_out = entries;
    *count_out = idx;
    return 0;
}

static char *safe_strdup(const char *src) {
    if (!src) return NULL;
	    char *dup = strdup(src);
	    if (!dup) memory_error();
	    return dup;
}

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
    if (percent < 0.0) percent = 0.0;
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
memory_error();
        res = NULL;
    }
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
int set_alsa_params(snd_pcm_t *handle, unsigned int rate, int channels) {
    if (!handle) {
display_message(ERROR, "Invalid handle in set_alsa_params — audio disabled");
        return -1;
    }
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_uframes_t period_size = 256;
    snd_pcm_uframes_t buffer_size = 1024;
    if (setup_alsa_hw_params(handle, params, &rate, channels, &period_size, &buffer_size) < 0) {
        return -1;
    }
    return 0;
}
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

    FILE *file = fopen(filename, "rb");
    if (!file) {
        display_message(ERROR, "Failed to open file: %s", filename);
        return NULL;
    }

    return file;
}

static void safe_cleanup_resources(FILE **file, snd_pcm_t **handle, struct pollfd **poll_fds, char **current_filename) {
    if (file && *file) { fclose(*file); *file = NULL; }
    if (handle && *handle) {
        snd_pcm_drop(*handle);
        snd_pcm_drain(*handle);
        snd_pcm_close(*handle);
        *handle = NULL;
    }
    if (poll_fds && *poll_fds) { free(*poll_fds); *poll_fds = NULL; }
    if (current_filename && *current_filename) { free(*current_filename); *current_filename = NULL; }
}

static void alsa_error(const char *msg, const char *err) {
    if (err) {
        display_message(ERROR, "%s: %s — audio disabled", msg, err);
    } else {
        display_message(ERROR, "%s — audio disabled", msg);
    }
}

static int setup_alsa_hw_params(snd_pcm_t *handle, snd_pcm_hw_params_t *params, unsigned int *rate, int channels, snd_pcm_uframes_t *period_size, snd_pcm_uframes_t *buffer_size) {
    int ret;
    int dir = 0;
    if ((ret = snd_pcm_hw_params_any(handle, params)) < 0) {
        alsa_error("snd_pcm_hw_params_any failed", snd_strerror(ret));
        return -1;
    }
    if ((ret = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        alsa_error("snd_pcm_hw_params_set_access failed", snd_strerror(ret));
        return -1;
    }
    if ((ret = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE)) < 0) {
        alsa_error("snd_pcm_hw_params_set_format failed", snd_strerror(ret));
        return -1;
    }
    if ((ret = snd_pcm_hw_params_set_channels(handle, params, channels)) < 0) {
        alsa_error("snd_pcm_hw_params_set_channels failed", snd_strerror(ret));
        return -1;
    }
    if ((ret = snd_pcm_hw_params_set_rate_near(handle, params, rate, &dir)) < 0) {
        alsa_error("snd_pcm_hw_params_set_rate_near failed", snd_strerror(ret));
        return -1;
    }
    if ((ret = snd_pcm_hw_params_set_period_size_near(handle, params, period_size, &dir)) < 0) {
        alsa_error("snd_pcm_hw_params_set_period_size_near failed", snd_strerror(ret));
        return -1;
    }
    if ((ret = snd_pcm_hw_params_set_buffer_size_near(handle, params, buffer_size)) < 0) {
        alsa_error("snd_pcm_hw_params_set_buffer_size_near failed", snd_strerror(ret));
        return -1;
    }
    if ((ret = snd_pcm_hw_params(handle, params)) < 0) {
        alsa_error("snd_pcm_hw_params failed", snd_strerror(ret));
        return -1;
    }
    return 0;
}

snd_pcm_t* init_audio_device(unsigned int rate, int channels) {
    snd_pcm_t *handle = NULL;
    int ret = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
alsa_error("ALSA device open error", snd_strerror(ret));
        return NULL;
    }
    if (set_alsa_params(handle, rate, channels) < 0) {
alsa_error("ALSA params setup failed", NULL);
        snd_pcm_close(handle);
        return NULL;
    }
    ret = snd_pcm_prepare(handle);
    if (ret < 0) {
alsa_error("snd_pcm_prepare failed", snd_strerror(ret));
        snd_pcm_close(handle);
        return NULL;
    }
    return handle;
}
static void draw_single_frame(WINDOW *win, int start_y, int height, const char *title, int line_type);
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

static char *shorten_title(const char *title, int max_len) {
    if (!title || max_len < 5) return strdup(title ? title : "");
    size_t len = strlen(title);
    if (len <= (size_t)max_len) return strdup(title);
    int front_len = (max_len - 3) / 2;
    int back_len = max_len - 3 - front_len;
    char *shortened = malloc(max_len + 1);
    if (!shortened) return NULL;
snprintf(shortened, max_len + 1, "%.*s...%s", front_len, title, title + len - back_len);
    return shortened;
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
    int title_len = strlen(title) + 4;
    int title_start = (actual_width - title_len) / 2;
    if (title_start < 1) title_start = 1;
    if (title_start + title_len > actual_width - 1) title_start = actual_width - title_len - 1;
char *short_title = shorten_title(title, actual_width - 4);
if (short_title) {
wchar_t wtitle[INNER_WIDTH + 1];
prepare_display_wstring(title, actual_width - 4, wtitle, sizeof(wtitle) / sizeof(wchar_t), 0, L"..", 0, 0);
int title_wlen = wcslen(wtitle);
int title_width = 0;
for (int i = 0; i < title_wlen; i++) {
    int w = wcwidth(wtitle[i]);
    title_width += (w < 0 ? 1 : w);
}
int title_len = title_width + 4;
int title_start = (actual_width - title_len) / 2;
if (title_start < 1) title_start = 1;
if (title_start + title_len > actual_width - 1) title_start = actual_width - title_len - 1;
mvwprintw(win, start_y, title_start, "%s ", title_left);
mvwaddnwstr(win, start_y, title_start + 2, wtitle, title_wlen);
mvwprintw(win, start_y, title_start + 2 + title_width, " %s", title_right);
    free(short_title);
}
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
    int offset_y = 3;
    int usable_height = max_y - offset_y - 3;
    if (usable_height < 3) usable_height = 3;
draw_single_frame(win, offset_y, usable_height, "HELP", 0);
}

void draw_help(WINDOW *win, int start_index) {
    int max_y, max_x;
    getmaxyx(win, max_y, max_x); (void)max_x;
    clear_rect(win, 3, max_y - 3, 0, 83);
    draw_help_frame(win);
    const char *help_lines[] = {
    " a       load playlist from current directory",
    " b       -10 seconds",
    " c       scroll path left",
    " d       from down (downward) — to scroll down similarly.",
    " e       scroll path right",
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
    if (control->playlist) {
        free_names(control->playlist, control->playlist_size, 0);
        control->playlist = NULL;
        control->playlist_size = 0;
        control->playlist_capacity = 0;
        control->current_track = 0;
    }
    SAFE_FREE(control->playlist_dir);
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
PlayerControl player_control = {
    .filename = NULL, .pause = 0, .stop = 0, .quit = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER,
    .current_file = NULL, .current_filename = NULL, .paused = 0,
    .loop_mode = 0,
    .playlist = NULL, .playlist_size = 0, .current_track = 0, .playlist_mode = 0,
    .seek_delta = 0, .duration = 0.0, .bytes_read = 0LL,
    .is_silent = 0, .fading_out = 0, .fading_in = 0, .current_fade = FADE_STEPS,
.playlist_dir = NULL,
};
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
    char temp_msg[256];
    vsnprintf(temp_msg, sizeof(temp_msg), fmt, ap);
    char *msg = (type == ERROR) ? error_msg : status_msg;
    size_t msg_size = sizeof(error_msg);
    strncpy(msg, temp_msg, msg_size);
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
            pthread_mutex_lock(&player_control.mutex);
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

    pthread_mutex_lock(&player_control.mutex);
    const char *active_filename = player_control.current_filename;
    if (!active_filename && player_control.filename) {
        active_filename = player_control.filename;
    }
    if (active_filename && strlen(active_filename) > 0) {
        const char *slash = strrchr(active_filename, '/');
current_file_name = safe_strdup(slash ? slash + 1 : active_filename);
if (current_file_name) { current_paused = player_control.paused;
}
}
char *playlist_dir = (player_control.playlist_dir) ? strdup(player_control.playlist_dir) : NULL;
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
SAFE_FREE(playlist_dir);
SAFE_FREE(current_file_name);

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
                safe_cleanup_resources(&file, &handle, &poll_fds, &control->current_filename);
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
            if (file) {
                fclose(file);
                file = NULL;
            }
            free(poll_fds); poll_fds = NULL;
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
safe_cleanup_resources(&file, &handle, NULL, NULL);
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
		                if (control->filename) {
                                SAFE_FREE(control->filename);
		                }
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
            long long seek_bytes = (long long)control->seek_delta * BYTES_PER_SECOND;
	    long long current_pos = ftell(control->current_file);
	    long long new_pos = current_pos + seek_bytes;
	    if (new_pos < 0)
	        new_pos = 0;
	    if (control->duration > 0.0) {
            long long max_bytes = (long long)(control->duration * BYTES_PER_SECOND);
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
static int playlist_cmp(const void *a, const void *b) {
    const char *path_a = *(const char **)a;
    const char *name_a = strrchr(path_a, '/') ? strrchr(path_a, '/') + 1 : path_a;
    const char *path_b = *(const char **)b;
    const char *name_b = strrchr(path_b, '/') ? strrchr(path_b, '/') + 1 : path_b;
    return strcasecmp(name_a, name_b);
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
            display_message(ERROR, "Warning: audio stream not responding — continuing without waiting");
            break;
        }
    }
    if (control->playlist) {
SAFE_FREE(control->playlist_dir);
control->playlist_dir = NULL;
SAFE_FREE_ARRAY(control->playlist, control->playlist_size);
        control->playlist = NULL;
        control->playlist_size = 0;
        control->current_track = 0;
    }
int count = 0;
char **entries = NULL;
if (scan_directory(dir_path, 1, (void **)&entries, &count, 1) != 0) {
    control->playlist_mode = 0;
    pthread_mutex_unlock(&control->mutex);
    return;
} if (count == 0) {
    free(entries);
    control->playlist_mode = 0;
    pthread_mutex_unlock(&control->mutex);
    return;
}
control->playlist = entries;
control->playlist_capacity = count;
control->playlist_size = count;
qsort(control->playlist, control->playlist_size, sizeof(char *), playlist_cmp);
control->current_track = 0;
control->playlist_mode = 1;
if (control->playlist_size > 0 && control->playlist[0]) {
    if (control->filename) free(control->filename);
    control->filename = strdup(control->playlist[0]);
    pthread_cond_signal(&control->cond);
    control->playlist_dir = safe_strdup(dir_path);
}
pthread_mutex_unlock(&control->mutex);
}

void shutdown_player_thread(PlayerControl *control, pthread_t thread, int *have_player_thread) {
    if (!*have_player_thread) return;
    lock_and_signal(control, NULL);
    control->quit = 1;

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
double elapsed = (double)control->bytes_read / BYTES_PER_SECOND;
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
void action_f(PlayerControl *control) {
    if (control->current_file) {
        control->seek_delta = 10;
    } else {
        display_message(STATUS, "Nothing to fast-forward");
    }
}
void action_b(PlayerControl *control) {
    if (control->current_file) {
        control->seek_delta = -10;
    } else {
        display_message(STATUS, "Nothing to rewind");
    }
}
void action_s(PlayerControl *control) {
    control->stop = 1;
    control->duration = 0.0;
    control->bytes_read = 0LL;
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
void with_player_control_lock(void (*action)(PlayerControl *)) {
    lock_and_signal(&player_control, action);
}
static void start_playback(const char *full_path, const char *file_name, int enable_loop) {
    if (!full_path || !file_name) return;

    pthread_mutex_lock(&player_control.mutex);
    if (player_control.filename) free(player_control.filename);
	    player_control.filename = safe_strdup(full_path);
    if (!player_control.filename) {
        display_message(STATUS, "Out of memory! Cannot play file.");
        pthread_mutex_unlock(&player_control.mutex);
        return;
    }
    cleanup_playlist(&player_control);
    player_control.loop_mode = enable_loop;
    reset_playback_fields(&player_control);
    pthread_cond_signal(&player_control.cond);

    if (player_control.current_filename) free(player_control.current_filename);
	    player_control.current_filename = safe_strdup(file_name);
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
            display_message(STATUS, "getcwd failed");
            strncpy(current_dir, target_dir, PATH_MAX - 1);
            current_dir[PATH_MAX - 1] = '\0';
            return -1;
        }
        update_file_list();
        status_buf[0] = '\0';
        return 0;
    } else {
    display_message(ERROR, "Access denied to: %s", target_dir);
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
    current_file_name = safe_strdup(slash ? slash + 1 : active_filename);
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
case 'p': case 'P':
with_player_control_lock(action_p);
break;
case 'f':
with_player_control_lock(action_f);
break;
case 'b':
with_player_control_lock(action_b);
break;
case 's': case 'S':
with_player_control_lock(action_s);
break;
		case 'h': case 'H':
		{
		    int help_start_index = 0;
		    help_mode = 1;
	    wtimeout(list_win, 50);
		    while (help_mode) {
		        draw_help(list_win, help_start_index);
		        int ch_help = wgetch(list_win);
if (ch_help == ERR) continue;
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
char *full_path = xasprintf("%s/%s", current_dir, file_list[selected_index].name);
if (!full_path) {
    display_message(STATUS, "Out of memory!");
} else {
	                load_playlist(full_path, &player_control);

	                if (player_control.playlist_size == 0) {
	                    display_message(ERROR, "Directory does not contain raw files.");
	                } else {
	                    pthread_mutex_lock(&player_control.mutex);
                    SAFE_FREE(player_control.filename);
                    player_control.filename = SAFE_STRDUP(player_control.playlist[0]);
                    SAFE_FREE(player_control.current_filename);
                            free(full_path);
	                    player_control.current_track = 0;
	                    player_control.playlist_mode = 1;
                            reset_playback_fields(&player_control);
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
        SAFE_FREE(player_control.filename);
player_control.filename = SAFE_STRDUP(player_control.playlist[0]);
SAFE_FREE(player_control.current_filename);
player_control.current_filename = SAFE_STRDUP(basename(player_control.playlist[0]));
        player_control.current_track = 0;
        player_control.playlist_mode = 1;
        reset_playback_fields(&player_control);
        pthread_cond_signal(&player_control.cond);
        pthread_mutex_unlock(&player_control.mutex);
    }
    break;
case 'l': case 'L':
    const char *selected_file_name = NULL;
int is_different_file = 0;
void check_different_file(PlayerControl *control) {
if (file_count > 0 && selected_index >= 0 && file_list && file_list[selected_index].name &&
!file_list[selected_index].is_dir && is_raw_file(file_list[selected_index].name)) {
selected_file_name = file_list[selected_index].name;
}
is_different_file = selected_file_name &&
(!control->current_filename || strcmp(control->current_filename, selected_file_name) != 0);
}
with_player_control_lock(check_different_file);

if (is_different_file) {
char *full_path = xasprintf("%s/%s", current_dir, selected_file_name);
if (full_path) {
    start_playback(full_path, selected_file_name, 1);
    free(full_path);
} else {
    display_message(STATUS, "Out of memory! Cannot play file.");
}
    } else {
        pthread_mutex_lock(&player_control.mutex);
        if (player_control.current_file) {
            player_control.loop_mode = !player_control.loop_mode;
            display_message(STATUS, "%s", player_control.loop_mode ? "Loop mode enabled" : "Loop mode disabled");
        } else {
            display_message(STATUS, "Nothing to loop");
        }
        pthread_mutex_unlock(&player_control.mutex);
    }
break;
case 'c':
    path_visual_offset = (path_visual_offset - 5 > 0) ? path_visual_offset - 5 : 0;
    break;
case 'e':
    path_visual_offset += 5;
    break;
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

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    pthread_t thread = 0;
    int have_player_thread = 0;
    have_player_thread =
    try_start_player_thread(&thread,
                            player_thread,
                            &player_control);
    int result = navigate_and_play();
    if (have_player_thread) {
        shutdown_player_thread(&player_control, thread, &have_player_thread);
    }
	with_player_control_lock(cleanup_playlist);
    if (have_player_thread) {
        pthread_mutex_destroy(&player_control.mutex);
        pthread_cond_destroy(&player_control.cond);
    }
    return result;
}
// 2298
