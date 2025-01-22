#include <kernel/console.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/uart.h>

#define INPUT_BUF 128
#define BACKSPACE 0x100

struct console cons;

void console_init()
{
    /* (Final) TODO BEGIN */
    init_spinlock(&cons.lock);
    init_sem(&cons.sem, 0);
    /* (Final) TODO END */
}

void consputc(int c)
{
    if (c == BACKSPACE) {
        uart_put_char('\b');
        uart_put_char(' ');
        uart_put_char('\b');
    } else
        uart_put_char(c);
}

/**
 * console_write - write to uart from the console buffer.
 * @ip: the pointer to the inode
 * @buf: the buffer
 * @n: number of bytes to write
 */
isize console_write(Inode *ip, char *buf, isize n)
{
    /* (Final) TODO BEGIN */
    if (ip) {
    }
    acquire_spinlock(&cons.lock);
    for (int i = 0; i < n; i++) {
        uart_put_char(buf[i]);
    }
    release_spinlock(&cons.lock);
    return n;
    /* (Final) TODO END */
}

/**
 * console_read - read to the destination from the buffer
 * @ip: the pointer to the inode
 * @dst: the destination
 * @n: number of bytes to read
 */
isize console_read(Inode *ip, char *dst, isize n)
{
    /* (Final) TODO BEGIN */
    if (ip) {
    }
    isize i = n;
    acquire_spinlock(&cons.lock);
    while (i) {
        if (cons.write_idx == cons.read_idx) {
            release_spinlock(&cons.lock);
            if (!wait_sem(&cons.sem)) {
                return -1;
            }
            acquire_spinlock(&cons.lock);
        }
        cons.read_idx = (cons.read_idx + 1) % INPUT_BUF;
        if (cons.buf[cons.read_idx] == C('D')) {
            if (i < n) {
                cons.read_idx = (cons.read_idx - 1) % INPUT_BUF;
            }
            break;
        }
        *(dst++) = cons.buf[cons.read_idx];
        i--;
        if (cons.buf[cons.read_idx] == '\n')
            break;
    }
    release_spinlock(&cons.lock);
    return n - i;
    /* (Final) TODO END */
}

void console_intr(char c)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&cons.lock);
    switch (c) {
    case C('U'): // Kill line.
        while (cons.edit_idx != cons.write_idx &&
               cons.buf[(cons.edit_idx - 1) % INPUT_BUF] != '\n') {
            cons.edit_idx = (cons.edit_idx - 1) % INPUT_BUF;
            consputc(BACKSPACE);
        }
        break;
    case '\x7f': // Backspace
        if (cons.edit_idx != cons.write_idx) {
            cons.edit_idx = (cons.edit_idx - 1) % INPUT_BUF;
            consputc(BACKSPACE);
        }
        break;
    default:
        if (c != 0 && cons.edit_idx - cons.read_idx < INPUT_BUF) {
            c = (c == '\r') ? '\n' : c;
            cons.buf[++cons.edit_idx % INPUT_BUF] = c;
            consputc(c);
            if (c == '\n' || c == C('D')) {
                cons.write_idx = cons.edit_idx;
                post_sem(&cons.sem);
            }
        }
        break;
    }
    release_spinlock(&cons.lock);
    /* (Final) TODO END */
}
