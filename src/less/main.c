/*
 * less — a pager you can scroll back through and search.
 *
 * Buffers the whole input into a line array, then shows a full-screen window
 * with a cursor. Unlike more (forward-only, streaming) this supports backward
 * motion and /search — which is why tools default PAGER=less. When stdout is
 * not a tty it just cats (so `foo | less | cat` and scripts still work).
 *
 * Keys: space/f/PgDn page fwd · b/PgUp page back · j/k/arrows line · g/G top/
 * bottom · /pat search fwd · n next match · q quit.
 * ponytail ceiling: long lines are truncated to the terminal width (no
 * horizontal scroll); a single concatenated buffer (no :n multi-file nav).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

static char **lines;
static int    nlines, cap;
static int    rows = 24, cols = 80;
static int    keyfd = -1;
static struct termios saved;
static int    saved_ok;

static void restore_tty(void) {
    if (saved_ok) tcsetattr(keyfd, TCSANOW, &saved);
}

static void addline(char *s) {
    if (nlines >= cap) { cap = cap ? cap * 2 : 256; lines = realloc(lines, cap * sizeof *lines); }
    lines[nlines++] = s;
}

/* Slurp a stream into the line array (newlines stripped, remembered as split). */
static void slurp(FILE *f) {
    char *buf = NULL; size_t bc = 0; ssize_t n;
    while ((n = getline(&buf, &bc, f)) != -1) {
        if (n && buf[n - 1] == '\n') buf[--n] = 0;
        char *copy = malloc(n + 1);
        memcpy(copy, buf, n); copy[n] = 0;
        addline(copy);
    }
    free(buf);
}

static void get_winsize(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        rows = ws.ws_row; cols = ws.ws_col ? ws.ws_col : 80;
    }
}

/* Draw one screenful starting at line `top`; status line at the bottom. */
static void draw(int top, const char *msg) {
    fputs("\033[H\033[2J", stdout);                 /* home + clear */
    int body = rows - 1;
    for (int i = 0; i < body; i++) {
        int ln = top + i;
        if (ln >= nlines) break;
        const char *s = lines[ln];
        int len = (int)strlen(s);
        if (len > cols) len = cols;                 /* truncate — no h-scroll */
        fwrite(s, 1, len, stdout);
        fputs("\r\n", stdout);
    }
    /* status line (reverse video) */
    fputs("\033[7m", stdout);
    if (msg && *msg) fputs(msg, stdout);
    else if (top + body >= nlines) fputs("(END)", stdout);
    else {
        int pct = nlines ? (int)((long)(top + body) * 100 / nlines) : 100;
        printf(":%d%%", pct > 100 ? 100 : pct);
    }
    fputs("\033[m", stdout);
    fflush(stdout);
}

/* Read one logical key. Arrows map to j/k/space/b sentinels. */
enum { K_UP = 1000, K_DOWN, K_PGUP, K_PGDN };
static int readkey(void) {
    unsigned char c;
    if (read(keyfd, &c, 1) != 1) return 'q';
    if (c != 0x1b) return c;
    unsigned char seq[2];
    if (read(keyfd, &seq[0], 1) != 1) return 0x1b;
    if (read(keyfd, &seq[1], 1) != 1) return 0x1b;
    if (seq[0] == '[') switch (seq[1]) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case '5': { unsigned char t; read(keyfd, &t, 1); return K_PGUP; }  /* ESC[5~ */
        case '6': { unsigned char t; read(keyfd, &t, 1); return K_PGDN; }  /* ESC[6~ */
    }
    return 0x1b;
}

/* Read a line (echoed) from the tty for the /pattern prompt. */
static void read_prompt(char prefix, char *out, int cap) {
    struct termios cooked = saved;                  /* echo/canonical for input */
    tcsetattr(keyfd, TCSANOW, &cooked);
    printf("\r\033[K%c", prefix); fflush(stdout);
    int n = 0; unsigned char ch;
    while (read(keyfd, &ch, 1) == 1 && ch != '\n' && ch != '\r') {
        if ((ch == 0x7f || ch == 8) && n > 0) { n--; printf("\b \b"); fflush(stdout); continue; }
        if (ch >= 0x20 && n < cap - 1) { out[n++] = ch; putchar(ch); fflush(stdout); }
    }
    out[n] = 0;
    tcsetattr(keyfd, TCSANOW, &saved);              /* back to raw */
}

static int search_from(const char *pat, int start) {
    for (int i = start; i < nlines; i++)
        if (strstr(lines[i], pat)) return i;
    return -1;
}

int main(int argc, char **argv) {
    int paged = isatty(STDOUT_FILENO);

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-' && argv[i][1]) continue;   /* ignore flags */
            FILE *f = fopen(argv[i], "r");
            if (!f) { fprintf(stderr, "less: %s: cannot open\n", argv[i]); continue; }
            slurp(f); fclose(f);
        }
    } else {
        slurp(stdin);
    }

    /* Not a tty (piped/redirected): behave as cat and exit. */
    if (!paged) {
        for (int i = 0; i < nlines; i++) { fputs(lines[i], stdout); putchar('\n'); }
        return 0;
    }

    /* Key source: the controlling terminal (stdin may be the piped data). */
    keyfd = open("/dev/tty", O_RDONLY);
    if (keyfd < 0) keyfd = STDIN_FILENO;
    if (tcgetattr(keyfd, &saved) == 0) {
        saved_ok = 1; atexit(restore_tty);
        struct termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        tcsetattr(keyfd, TCSANOW, &raw);
    }
    get_winsize();

    int top = 0, body = rows - 1, maxtop = nlines - body;
    if (maxtop < 0) maxtop = 0;
    char pat[256] = ""; const char *msg = NULL;

    for (;;) {
        if (top < 0) top = 0;
        if (top > maxtop) top = maxtop;
        draw(top, msg);
        msg = NULL;
        int k = readkey();
        switch (k) {
        case 'q': case 'Q':
            fputs("\r\033[K", stdout); fflush(stdout); return 0;
        case ' ': case 'f': case 6: case K_PGDN: top += body; break;
        case 'b': case 2: case K_PGUP: top -= body; break;
        case 'j': case '\r': case '\n': case K_DOWN: top += 1; break;
        case 'k': case K_UP: top -= 1; break;
        case 'd': top += body / 2; break;
        case 'u': top -= body / 2; break;
        case 'g': case '<': top = 0; break;
        case 'G': case '>': top = maxtop; break;
        case '/': {
            read_prompt('/', pat, sizeof pat);
            if (pat[0]) {
                int hit = search_from(pat, top + 1);
                if (hit < 0) hit = search_from(pat, 0);   /* wrap */
                if (hit < 0) msg = "Pattern not found";
                else top = hit;
            }
            break;
        }
        case 'n': {
            if (pat[0]) {
                int hit = search_from(pat, top + 1);
                if (hit < 0) hit = search_from(pat, 0);
                if (hit < 0) msg = "Pattern not found";
                else top = hit;
            }
            break;
        }
        default: break;
        }
    }
}
