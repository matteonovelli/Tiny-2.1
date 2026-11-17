/* conio.h - the Turbo C 2.0 text screen functions that TINY.C uses,
   for a macOS terminal (see tcmac.c). */
#ifndef TC_CONIO_H
#define TC_CONIO_H

enum text_modes { LASTMODE = -1, BW40 = 0, C40, BW80, C80, MONO = 7 };

struct text_info {
    unsigned char winleft, wintop, winright, winbottom;
    unsigned char attribute, normattr, currmode;
    unsigned char screenheight, screenwidth;
    unsigned char curx, cury;
};

/* tc_ names, so nothing clashes with the macOS libraries */
#define clrscr      tc_clrscr
#define cprintf     tc_cprintf
#define cputs       tc_cputs
#define getch       tc_getch
#define gettext     tc_gettext
#define gettextinfo tc_gettextinfo
#define gotoxy      tc_gotoxy
#define kbhit       tc_kbhit
#define putch       tc_putch
#define textattr    tc_textattr
#define textmode    tc_textmode
#define wherex      tc_wherex
#define wherey      tc_wherey
#define window      tc_window

void tc_clrscr(void);
int  tc_cprintf(const char *fmt, ...);
int  tc_cputs(const char *s);
int  tc_getch(void);
int  tc_gettext(int left, int top, int right, int bottom, void *dest);
void tc_gettextinfo(struct text_info *ti);
void tc_gotoxy(int x, int y);
int  tc_kbhit(void);
int  tc_putch(int c);
void tc_textattr(int attr);
void tc_textmode(int mode);
int  tc_wherex(void);
int  tc_wherey(void);
void tc_window(int left, int top, int right, int bottom);

#endif
