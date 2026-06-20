#include <stdio.h>

__asm(".global __use_no_semihosting\n");
__asm(".global __ARM_use_no_argv\n");

FILE __stdout;
FILE __stdin;
FILE __stderr;

int fputc(int ch, FILE *f)
{
    (void)f;
    return ch;
}

int fgetc(FILE *f)
{
    (void)f;
    return EOF;
}

int ferror(FILE *f)
{
    (void)f;
    return EOF;
}

void _ttywrch(int ch)
{
    (void)ch;
}

void _sys_exit(int return_code)
{
    (void)return_code;
    while (1) {
    }
}
