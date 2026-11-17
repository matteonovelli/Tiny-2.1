/* tcmac.c - the parts of Turbo C 2.0 that TINY.C uses (conio, dos and the
   PC speaker), rebuilt for a macOS terminal so TINY.C compiles unchanged.

   Screen    An 80x25 buffer of DOS cells: a code page 437 character and a
             colour attribute. Drawing only changes the buffer. Whenever the
             program waits (delay, kbhit, getch) the changed cells go to the
             terminal as UTF-8 with ANSI colour codes. The VGA palette TINY
             programs (int 10h, AX=1010h) is kept and shown in 24-bit colour,
             or as the nearest of the 256 xterm colours.
   Keyboard  Terminal key codes become getch() values again: 0 and then a
             scan code for F1-F12 and the arrows, 13 Enter, 8 Backspace,
             27 Esc.
   Speaker   sound() and nosound() are time-stamped and played back LAG
             seconds later as a square wave through Core Audio. Playing
             from the time stamps keeps the 1-2 ms pitch changes of the
             robot voice.
   Time      delay() sleeps to a running deadline, so long runs of short
             delays keep their rhythm. clock() counts real time, as in DOS. */

#include <AudioToolbox/AudioToolbox.h>
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "conio.h"
#include "dos.h"

#define W 80
#define H 25

static void term_init(void);
static void screen_flush(void);
static void speaker_close(void);

static int tried;                     /* term_init() has run */
#define INIT() do { if (!tried) term_init(); } while (0)

/* ---- time ---------------------------------------------------------- */

static double now(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e9;
}

static void sleep_until(double t)
{
    double left;
    struct timespec ts;
    while ((left = t - now()) > 0) {
        ts.tv_sec = (time_t)left;
        ts.tv_nsec = (long)((left - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
    }
}

static double deadline;               /* when the last delay() ended */
static unsigned delays;               /* delay() calls so far        */

/* now, or the end of the last delay() if that was only just now: the
   program is then still on the same beat */
static double steady_now(void)
{
    double t = now();
    return t - deadline < 0.004 ? deadline : t;
}

clock_t tc_clock(void)
{
    static double t0;
    if (t0 == 0)
        t0 = now();
    return (clock_t)((now() - t0) * CLOCKS_PER_SEC);
}

void tc_delay(unsigned ms)
{
    INIT();
    screen_flush();
    delays++;
    deadline = steady_now() + ms / 1000.0;
    sleep_until(deadline);
}

void tc_gettime(struct time *t)
{
    struct timeval tv;
    struct tm tm;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    t->ti_hour = (unsigned char)tm.tm_hour;
    t->ti_min = (unsigned char)tm.tm_min;
    t->ti_sec = (unsigned char)tm.tm_sec;
    t->ti_hund = (unsigned char)(tv.tv_usec / 10000);
}

/* ---- the DOS screen ------------------------------------------------ */

typedef struct { unsigned char ch, at; } Cell;

static Cell scr[H][W];                /* what the program drew      */
static Cell shown[H][W];              /* what the terminal shows    */
static int cx = 1, cy = 1;            /* cursor, from 1, on screen  */
static int wl = 1, wt = 1, wr = W, wb = H;   /* text window         */
static int attr = 7, mode = C80;
static int cur_on = 1;
static unsigned short cur_shape = 0x0607;
static int blink = 1;                 /* attribute bit 7 blinks     */
static unsigned char dac[256][3];     /* VGA colour registers 0..63 */

/* text colour 0..15 -> VGA colour register (the standard EGA mapping) */
static const unsigned char pal[16] = {
    0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63
};

/* code page 437 below and above plain ASCII */
static const unsigned short cp_low[32] = {
    0x0020, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC
};
static const unsigned short cp_high[129] = {
    0x2302,
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

static unsigned unicode(unsigned char c)
{
    if (c >= 32 && c < 127)
        return c;
    return c < 32 ? cp_low[c] : cp_high[c - 127];
}

/* the colours a VGA card has after a mode set (the 64 EGA colours) */
static void default_dac(void)
{
    int i;
    memset(dac, 0, sizeof dac);
    for (i = 0; i < 64; i++) {
        dac[i][0] = (unsigned char)(42 * ((i >> 2) & 1) + 21 * ((i >> 5) & 1));
        dac[i][1] = (unsigned char)(42 * ((i >> 1) & 1) + 21 * ((i >> 4) & 1));
        dac[i][2] = (unsigned char)(42 * (i & 1) + 21 * ((i >> 3) & 1));
    }
}

static void fill(int l, int t, int r, int b)
{
    int x, y;
    for (y = t; y <= b; y++)
        for (x = l; x <= r; x++) {
            scr[y - 1][x - 1].ch = ' ';
            scr[y - 1][x - 1].at = (unsigned char)attr;
        }
}

static void scroll_up(void)
{
    int y;
    for (y = wt; y < wb; y++)
        memcpy(&scr[y - 1][wl - 1], &scr[y][wl - 1], (size_t)(wr - wl + 1) * sizeof(Cell));
    fill(wl, wb, wr, wb);
}

/* ---- the terminal -------------------------------------------------- */

static int active;                    /* the terminal is ours       */
static struct termios saved_tio, raw_tio;
static int truecolor;
static int cols = W, rows = H;        /* terminal size              */
static int ox, oy;                    /* where the 80x25 screen sits */
static int old_rows, old_cols, asked_rows, asked_cols;
static volatile sig_atomic_t resized, redraw;
static int shown_x, shown_y, shown_on = -1;

/* own screen, no line wrap, blinking underline cursor as in DOS */
static const char enter_seq[] = "\033[?1049h\033[?7l\033[3 q";
static const char leave_seq[] = "\033[?2026l\033[0m\033[?25h\033[0 q\033[?7h\033[?1049l";

static char ob[1 << 16];
static size_t obn;

static void raw_write(const char *s, size_t n)
{
    ssize_t k;
    while (n > 0) {
        k = write(1, s, n);
        if (k < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += k;
        n -= (size_t)k;
    }
}

static void ob_flush(void)
{
    raw_write(ob, obn);
    obn = 0;
}

static void ob_put(const char *s)
{
    size_t n = strlen(s);
    if (obn + n > sizeof ob)
        ob_flush();
    memcpy(ob + obn, s, n);
    obn += n;
}

static void ob_fmt(const char *fmt, ...)
{
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    ob_put(b);
}

static void ob_glyph(unsigned char c)
{
    unsigned u = unicode(c);
    char b[4] = { 0 };
    if (u < 0x80) {
        b[0] = (char)u;
    } else if (u < 0x800) {
        b[0] = (char)(0xC0 | u >> 6);
        b[1] = (char)(0x80 | (u & 0x3F));
    } else {
        b[0] = (char)(0xE0 | u >> 12);
        b[1] = (char)(0x80 | ((u >> 6) & 0x3F));
        b[2] = (char)(0x80 | (u & 0x3F));
    }
    ob_put(b);
}

static int xterm256(int r, int g, int b)
{
    static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
    int c[3], k[3], i, gi, gv, dc = 0, dg = 0;
    c[0] = r; c[1] = g; c[2] = b;
    for (i = 0; i < 3; i++)
        k[i] = c[i] < 48 ? 0 : c[i] < 115 ? 1 : (c[i] - 35) / 40;
    gi = ((r + g + b) / 3 - 3) / 10;
    if (gi < 0) gi = 0;
    if (gi > 23) gi = 23;
    gv = 8 + 10 * gi;
    for (i = 0; i < 3; i++) {
        dc += (c[i] - lv[k[i]]) * (c[i] - lv[k[i]]);
        dg += (c[i] - gv) * (c[i] - gv);
    }
    return dg < dc ? 232 + gi : 16 + 36 * k[0] + 6 * k[1] + k[2];
}

static void sgr(int a)
{
    int f[3], b[3], i;
    const unsigned char *df = dac[pal[a & 15]];
    const unsigned char *db = dac[pal[(a >> 4) & (blink ? 7 : 15)]];
    for (i = 0; i < 3; i++) {                    /* 6-bit VGA -> 8-bit */
        f[i] = df[i] << 2 | df[i] >> 4;
        b[i] = db[i] << 2 | db[i] >> 4;
    }
    if (truecolor)
        ob_fmt("\033[0;38;2;%d;%d;%d;48;2;%d;%d;%dm", f[0], f[1], f[2], b[0], b[1], b[2]);
    else
        ob_fmt("\033[0;38;5;%d;48;5;%dm", xterm256(f[0], f[1], f[2]), xterm256(b[0], b[1], b[2]));
    if (blink && (a & 0x80))
        ob_put("\033[5m");
}

static void get_size(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
    ox = cols > W ? (cols - W) / 2 : 0;
    oy = rows > H ? (rows - H) / 2 : 0;
}

/* send the cells that changed, then put the cursor where DOS has it */
static void screen_flush(void)
{
    int x, y, all = 0, last = -1, px = -1, py = -1, drew = 0, vis;
    Cell c;

    if (!active)
        return;
    if (resized) {
        resized = 0;
        get_size();
        redraw = 1;
    }
    if (redraw) {
        redraw = 0;
        all = drew = 1;
        ob_put("\033[?2026h\033[?25l");
        sgr(0);                                  /* the border is colour 0 */
        last = 0;
        ob_put("\033[2J");
    }
    for (y = 0; y < H && oy + y < rows; y++)
        for (x = 0; x < W && ox + x < cols; x++) {
            c = scr[y][x];
            if (!all && c.ch == shown[y][x].ch && c.at == shown[y][x].at)
                continue;
            if (!drew) {
                ob_put("\033[?2026h\033[?25l");
                drew = 1;
            }
            if (y != py || x != px)
                ob_fmt("\033[%d;%dH", oy + y + 1, ox + x + 1);
            if (c.at != last) {
                sgr(c.at);
                last = c.at;
            }
            ob_glyph(c.ch);
            shown[y][x] = c;
            px = x + 1;
            py = y;
        }
    vis = cur_on && ox + cx <= cols && oy + cy <= rows;
    if (drew || vis != shown_on || (vis && (cx != shown_x || cy != shown_y))) {
        if (vis)
            ob_fmt("\033[%d;%dH\033[?25h", oy + cy, ox + cx);
        else if (!drew)
            ob_put("\033[?25l");
        if (drew)
            ob_put("\033[?2026l");
        shown_on = vis;
        shown_x = cx;
        shown_y = cy;
    }
    ob_flush();
}

/* safe inside a signal handler */
static void term_restore(void)
{
    raw_write(leave_seq, sizeof leave_seq - 1);
    tcsetattr(0, TCSANOW, &saved_tio);
}

static void at_exit(void)
{
    char b[32];
    if (!active)
        return;
    speaker_close();
    term_restore();
    active = 0;
    get_size();
    if (asked_rows && rows == asked_rows && cols == asked_cols) {
        snprintf(b, sizeof b, "\033[8;%d;%dt", old_rows, old_cols);
        raw_write(b, strlen(b));
    }
}

static void on_quit(int sig)
{
    if (active)
        term_restore();
    signal(sig, SIG_DFL);
    raise(sig);                       /* delivered when this returns */
}

static void on_winch(int sig)
{
    (void)sig;
    resized = 1;
}

static void on_cont(int sig)
{
    (void)sig;
    if (!active)
        return;
    tcsetattr(0, TCSANOW, &raw_tio);
    raw_write(enter_seq, sizeof enter_seq - 1);
    redraw = 1;
}

/* TINY draws 80x25; a Mac terminal opens at 80x24. Ask the terminal to
   grow, and if it will not, ask the user. */
static void fit_window(void)
{
    char b[200];
    double until;
    fd_set fds;
    struct timeval tv;
    unsigned char k;

    get_size();
    if (cols >= W && rows >= H)
        return;
    old_rows = rows;
    old_cols = cols;
    asked_rows = rows < H ? H : rows;
    asked_cols = cols < W ? W : cols;
    snprintf(b, sizeof b, "\033[8;%d;%dt", asked_rows, asked_cols);
    raw_write(b, strlen(b));
    for (until = now() + 0.5; now() < until; usleep(20000)) {
        get_size();
        if (cols >= W && rows >= H)
            return;
    }
    for (;;) {
        snprintf(b, sizeof b, "\033[0m\033[2J\033[HTINY needs a window of at least 80 x 25 "
                 "characters.\nThis one is %d x %d. Make it bigger, or press Esc to quit.",
                 cols, rows);
        raw_write(b, strlen(b));
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        if (select(1, &fds, NULL, NULL, &tv) > 0 && read(0, &k, 1) == 1 && (k == 27 || k == 'q'))
            exit(0);
        get_size();
        if (cols >= W && rows >= H)
            return;
    }
}

static void term_init(void)
{
    struct sigaction sa;
    const char *c;

    tried = 1;
    default_dac();
    fill(1, 1, W, H);
    if (!isatty(0) || !isatty(1) || tcgetattr(0, &saved_tio) != 0)
        return;

    /* Text printed while the screen is ours (an error, "goodbye") waits in
       the buffer and appears once the normal screen is back: exit() runs
       at_exit() before it flushes stdout. */
    setvbuf(stdout, NULL, _IOFBF, BUFSIZ);

    raw_tio = saved_tio;
    raw_tio.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INLCR | IGNCR | ISTRIP | IXON);
    raw_tio.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN);
    raw_tio.c_cc[VMIN] = 1;
    raw_tio.c_cc[VTIME] = 0;
    raw_tio.c_cc[VSUSP] = _POSIX_VDISABLE;    /* Ctrl-Z is just a key, as in DOS */
    raw_tio.c_cc[VDSUSP] = _POSIX_VDISABLE;

    c = getenv("TINY_COLORS");
    if (c == NULL)
        c = getenv("COLORTERM");
    truecolor = c != NULL && (strstr(c, "truecolor") != NULL || strstr(c, "24bit") != NULL);

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_quit;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sa.sa_handler = on_winch;                 /* no SA_RESTART: wake getch() */
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = on_cont;
    sigaction(SIGCONT, &sa, NULL);
    atexit(at_exit);

    tcsetattr(0, TCSAFLUSH, &raw_tio);
    active = 1;
    raw_write(enter_seq, sizeof enter_seq - 1);
    fit_window();
    redraw = 1;
}

/* ---- conio --------------------------------------------------------- */

static void con_put(int c)
{
    switch (c) {
    case '\a':
        if (active) ob_put("\a");
        break;
    case '\b':
        if (cx > wl) cx--;
        break;
    case '\n':                        /* line feed only, as in Turbo C */
        cy++;
        break;
    case '\r':
        cx = wl;
        break;
    default:
        scr[cy - 1][cx - 1].ch = (unsigned char)c;
        scr[cy - 1][cx - 1].at = (unsigned char)attr;
        cx++;
    }
    if (cx > wr) {
        cx = wl;
        cy++;
    }
    if (cy > wb) {
        scroll_up();
        cy = wb;
    }
}

int tc_putch(int c)
{
    con_put(c & 255);
    return c;
}

int tc_cputs(const char *s)
{
    int c = 0;
    while (*s)
        con_put(c = (unsigned char)*s++);
    return c;
}

int tc_cprintf(const char *fmt, ...)
{
    char b[512];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    tc_cputs(b);
    return n;
}

void tc_textattr(int a)
{
    attr = a & 255;
}

void tc_gotoxy(int x, int y)
{
    int ax = wl + x - 1, ay = wt + y - 1;
    if (x < 1 || y < 1 || ax > wr || ay > wb)
        return;                       /* ignored, as in Turbo C */
    cx = ax;
    cy = ay;
}

int tc_wherex(void)
{
    return cx - wl + 1;
}

int tc_wherey(void)
{
    return cy - wt + 1;
}

void tc_window(int l, int t, int r, int b)
{
    if (l < 1 || t < 1 || r > W || b > H || l > r || t > b)
        return;
    wl = l; wt = t; wr = r; wb = b;
    cx = l;
    cy = t;
}

void tc_clrscr(void)
{
    fill(wl, wt, wr, wb);
    cx = wl;
    cy = wt;
}

/* a BIOS mode set: standard colours, full screen window, cleared */
void tc_textmode(int m)
{
    INIT();
    if (m != LASTMODE)
        mode = m;
    default_dac();
    blink = 1;
    cur_on = 1;
    cur_shape = 0x0607;
    tc_window(1, 1, W, H);
    attr = 7;
    tc_clrscr();
    redraw = 1;
}

void tc_gettextinfo(struct text_info *ti)
{
    INIT();
    ti->winleft = (unsigned char)wl;
    ti->wintop = (unsigned char)wt;
    ti->winright = (unsigned char)wr;
    ti->winbottom = (unsigned char)wb;
    ti->attribute = (unsigned char)attr;
    ti->normattr = 7;
    ti->currmode = (unsigned char)mode;
    ti->screenheight = H;
    ti->screenwidth = W;
    ti->curx = (unsigned char)tc_wherex();
    ti->cury = (unsigned char)tc_wherey();
}

int tc_gettext(int l, int t, int r, int b, void *dest)
{
    unsigned char *d = (unsigned char *)dest;
    int x, y;
    if (l < 1 || t < 1 || r > W || b > H || l > r || t > b)
        return 0;
    for (y = t; y <= b; y++)
        for (x = l; x <= r; x++) {
            *d++ = scr[y - 1][x - 1].ch;
            *d++ = scr[y - 1][x - 1].at;
        }
    return 1;
}

/* the video BIOS calls TINY makes */
int tc_int86(int intno, union REGS *in, union REGS *out)
{
    union REGS r = *in;
    INIT();
    if (intno == 0x10)
        switch (r.h.ah) {
        case 0x01:                    /* cursor shape; CH bit 5 hides it */
            cur_shape = r.x.cx;
            cur_on = !(r.h.ch & 0x20);
            break;
        case 0x03:                    /* cursor shape and place */
            r.x.cx = cur_shape;
            r.h.dh = (unsigned char)(cy - 1);
            r.h.dl = (unsigned char)(cx - 1);
            break;
        case 0x10:
            if (r.h.al == 0x10) {     /* set a colour register */
                dac[r.x.bx & 255][0] = r.h.dh & 63;
                dac[r.x.bx & 255][1] = r.h.ch & 63;
                dac[r.x.bx & 255][2] = r.h.cl & 63;
                redraw = 1;
            } else if (r.h.al == 0x03) {   /* blink or bright backgrounds */
                blink = r.h.bl != 0;
                redraw = 1;
            }
            break;
        case 0x1A:                    /* display type: VGA colour */
            if (r.h.al == 0) {
                r.h.al = 0x1A;
                r.h.bl = 8;
                r.h.bh = 0;
            }
            break;
        }
    *out = r;
    return r.x.ax;
}

/* ---- keyboard ------------------------------------------------------ */

static int keys[64], nkeys, at_eof;

static void key_add(int c)
{
    if (nkeys < 64)
        keys[nkeys++] = c;
}

static void key_ext(int scan)         /* 0, then the scan code */
{
    if (nkeys < 63) {
        keys[nkeys++] = 0;
        keys[nkeys++] = scan;
    }
}

/* one byte from the keyboard within ms milliseconds (-1: no limit) */
static int in_byte(int ms)
{
    fd_set fds;
    struct timeval tv;
    unsigned char b;
    ssize_t n;

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    if (select(1, &fds, NULL, NULL, ms < 0 ? NULL : &tv) <= 0)
        return -1;                    /* nothing, or a signal (resize) */
    n = read(0, &b, 1);
    if (n == 1)
        return b;
    if (n == 0 || (errno != EINTR && errno != EAGAIN))
        at_eof = 1;
    return -1;
}

static void escape_key(int intro, int p, int f)
{
    /* ESC [ p ~ */
    static const unsigned char tilde[25] = {
        0, 71, 82, 83, 79, 73, 81, 71, 79, 0, 0, 59, 60, 61, 62, 63,
        0, 64, 65, 66, 67, 68, 0, 133, 134
    };
    static const char fin[] = "ABCDHFPQRSZ";
    static const unsigned char scan[] = { 72, 80, 77, 75, 71, 79, 59, 60, 61, 62, 15 };
    const char *k;

    if (f == '~') {
        if (p > 0 && p < 25 && tilde[p])
            key_ext(tilde[p]);
    } else if (intro == 'O' && f == 'M') {
        key_add(13);                  /* keypad Enter */
    } else if ((k = strchr(fin, f)) != NULL) {
        key_ext(scan[k - fin]);
    }
}

/* UTF-8 typed on a Mac keyboard -> the code page 437 letter, if any */
static int utf8_key(int c)
{
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0, b, i;
    unsigned u = (unsigned)c & (0x3Fu >> n);
    if (n == 0)
        return -1;
    for (i = 0; i < n; i++) {
        b = in_byte(30);
        if (b < 0x80 || b > 0xBF)
            return -1;
        u = u << 6 | (unsigned)(b & 0x3F);
    }
    for (i = 128; i < 256; i++)
        if (unicode((unsigned char)i) == u)
            return i;
    return -1;
}

/* read one key (or none) into keys[] */
static void read_key(int ms)
{
    int c = in_byte(ms), intro, f, n = 0;
    char p[16];

    if (c < 0) {
        if (at_eof) key_add(27);      /* keyboard gone: Esc quits TINY */
        return;
    }
    if (c == 27) {
        c = in_byte(30);
        if (c < 0) {
            key_add(27);              /* Esc on its own */
            return;
        }
        if (c == '[' || c == 'O') {
            intro = c;
            while ((f = in_byte(30)) >= 0 && (f < 0x40 || f > 0x7E))
                if (n < 15) p[n++] = (char)f;
            p[n] = 0;
            if (f >= 0)
                escape_key(intro, atoi(p), f);
            return;
        }
        /* Esc and a key (Option as Meta): just the key */
    }
    if (c == 127)
        c = 8;                        /* the Mac Delete key is Backspace */
    else if (c >= 0x80)
        c = utf8_key(c);
    if (c >= 0)
        key_add(c);
}

int tc_kbhit(void)
{
    static unsigned seen = ~0u;
    INIT();
    screen_flush();
    if (nkeys == 0 && active) {
        /* a loop that does nothing but ask, like while (!kbhit()) ;,
           rests a little each time instead of using a whole core */
        read_key(delays == seen ? 10 : 0);
        seen = delays;
    }
    return nkeys > 0;
}

int tc_getch(void)
{
    int c;
    INIT();
    while (nkeys == 0) {
        screen_flush();
        if (!active) {
            c = getchar();
            return c == EOF ? 27 : c;
        }
        read_key(-1);
    }
    c = keys[0];
    memmove(keys, keys + 1, (size_t)--nkeys * sizeof keys[0]);
    return c;
}

/* ---- PC speaker ---------------------------------------------------- */

#define RATE   48000.0
#define LAG    0.05                   /* the speaker runs this far behind */
#define VOLUME 0.15
#define NEV    4096

typedef struct { double t; double hz; } SpkEvent;

static SpkEvent ev[NEV];
static _Atomic unsigned ev_in, ev_out;
static AudioComponentInstance unit;
static int audio;                     /* 0 not started, 1 on, -1 none */
static double want_hz, last_ev;

/* audio thread only */
static double a_time, a_phase, a_hz, a_lp;

/* softens the square wave's jumps so high notes do not alias */
static double blep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                       UInt32 bus, UInt32 frames, AudioBufferList *io)
{
    float *left = (float *)io->mBuffers[0].mData;
    float *right = io->mNumberBuffers > 1 ? (float *)io->mBuffers[1].mData : NULL;
    unsigned i_out = atomic_load_explicit(&ev_out, memory_order_relaxed);
    unsigned i_in = atomic_load_explicit(&ev_in, memory_order_acquire);
    double dt = 1.0 / RATE, target = now() - LAG, t, x, inc;
    UInt32 i;

    (void)ref; (void)flags; (void)ts; (void)bus;
    if (fabs(a_time - target) > 0.02)  /* keep to the program's clock */
        a_time = target;
    for (i = 0; i < frames; i++) {
        t = a_time + i * dt;
        while (i_out != i_in && ev[i_out % NEV].t <= t) {
            if (a_hz == 0 && ev[i_out % NEV].hz > 0)
                a_phase = 0;
            a_hz = ev[i_out % NEV].hz;
            i_out++;
        }
        x = 0;
        if (a_hz > 0) {
            inc = a_hz * dt;
            x = a_phase < 0.5 ? 1.0 : -1.0;
            x += blep(a_phase, inc);
            x -= blep(fmod(a_phase + 0.5, 1.0), inc);
            a_phase += inc;
            if (a_phase >= 1.0)
                a_phase -= 1.0;
        }
        a_lp += 0.6 * (x - a_lp);      /* a small speaker, no sharp edges */
        left[i] = (float)(a_lp * VOLUME);
        if (right)
            right[i] = left[i];
    }
    atomic_store_explicit(&ev_out, i_out, memory_order_release);
    a_time += frames * dt;
    return noErr;
}

static void audio_start(void)
{
    AudioComponentDescription d;
    AudioComponent comp;
    AudioStreamBasicDescription f;
    AURenderCallbackStruct cb;

    audio = -1;
    memset(&d, 0, sizeof d);
    d.componentType = kAudioUnitType_Output;
    d.componentSubType = kAudioUnitSubType_DefaultOutput;
    d.componentManufacturer = kAudioUnitManufacturer_Apple;
    comp = AudioComponentFindNext(NULL, &d);
    if (comp == NULL || AudioComponentInstanceNew(comp, &unit) != noErr)
        return;

    memset(&f, 0, sizeof f);
    f.mSampleRate = RATE;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    f.mFramesPerPacket = 1;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    f.mBytesPerFrame = 4;
    f.mBytesPerPacket = 4;
    cb.inputProc = render;
    cb.inputProcRefCon = NULL;
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) != noErr ||
        AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr ||
        AudioUnitInitialize(unit) != noErr ||
        AudioOutputUnitStart(unit) != noErr) {
        AudioComponentInstanceDispose(unit);
        return;
    }
    audio = 1;
}

static void speaker(double hz)
{
    unsigned i;
    double t;

    if (hz > 20000)
        hz = 0;
    if (hz == want_hz)
        return;
    want_hz = hz;
    if (audio == 0 && hz > 0)
        audio_start();                /* first sound: open the output */
    if (audio <= 0)
        return;
    i = atomic_load_explicit(&ev_in, memory_order_relaxed);
    if (i - atomic_load_explicit(&ev_out, memory_order_acquire) >= NEV)
        return;
    t = steady_now();
    if (t < last_ev)
        t = last_ev;
    ev[i % NEV].t = last_ev = t;
    ev[i % NEV].hz = hz;
    atomic_store_explicit(&ev_in, i + 1, memory_order_release);
}

void tc_sound(unsigned hz)
{
    INIT();
    speaker(hz);
}

void tc_nosound(void)
{
    speaker(0);
}

static void speaker_close(void)
{
    double end;
    if (audio <= 0)
        return;
    /* let the last sounds play: they run LAG behind, plus the output delay */
    end = last_ev + LAG + 0.08;
    if (end > now() + 0.5)
        end = now() + 0.5;
    sleep_until(end);
    AudioOutputUnitStop(unit);
    AudioComponentInstanceDispose(unit);
    audio = -1;
}

/* ---- TINY.BIN ------------------------------------------------------ */

/* Like DOS, TINY opens TINY.BIN in the current directory. If it is not
   there, use the one next to the program, so it also starts from Finder
   or from another directory. */
__attribute__((constructor)) static void find_tiny_bin(void)
{
    char exe[PATH_MAX], dir[PATH_MAX], bin[PATH_MAX + 16];
    uint32_t n = sizeof exe;
    char *slash;

    if (access("TINY.BIN", R_OK) == 0)
        return;
    if (_NSGetExecutablePath(exe, &n) != 0 || realpath(exe, dir) == NULL)
        return;
    slash = strrchr(dir, '/');
    if (slash == NULL)
        return;
    *slash = 0;
    snprintf(bin, sizeof bin, "%s/TINY.BIN", dir);
    if (access(bin, R_OK) == 0 && chdir(dir) != 0)
        return;
}
