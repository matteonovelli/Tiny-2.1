/* dos.h - the Turbo C 2.0 DOS functions that TINY.C uses, for macOS
   (see tcmac.c). int86() understands the few video BIOS calls TINY makes. */
#ifndef TC_DOS_H
#define TC_DOS_H

/* 16-bit registers, laid out so that x.ax and h.al/h.ah overlap as on a PC
   (every Mac is little-endian, like the 8086). */
struct WORDREGS { unsigned short ax, bx, cx, dx, si, di, cflag, flags; };
struct BYTEREGS { unsigned char al, ah, bl, bh, cl, ch, dl, dh; };
union REGS { struct WORDREGS x; struct BYTEREGS h; };

struct time { unsigned char ti_min, ti_hour, ti_hund, ti_sec; };

#define delay   tc_delay
#define gettime tc_gettime
#define int86   tc_int86
#define nosound tc_nosound
#define sound   tc_sound

void tc_delay(unsigned ms);
void tc_gettime(struct time *t);
int  tc_int86(int intno, union REGS *in, union REGS *out);
void tc_nosound(void);
void tc_sound(unsigned hz);

#endif
