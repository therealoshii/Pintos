#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static uint8_t read_user_byte (const uint8_t *uaddr);
static void check_user_byte (const uint8_t *uaddr, bool writable);
static void check_user_buffer (const void *buffer, unsigned size,
                               bool writable);
static uint32_t get_arg (struct intr_frame *, int n);

static void sys_halt (void) NO_RETURN;
static void sys_exit (int status) NO_RETURN;
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* System call dispatcher */
static void
syscall_handler (struct intr_frame *f)
{
  switch (get_arg (f, 0))
    {
    case SYS_HALT:
      sys_halt ();

    case SYS_EXIT:
      sys_exit (get_arg (f, 1));

    case SYS_READ:
      {
        int fd = get_arg (f, 1);
        void *buffer = (void *) get_arg (f, 2);
        unsigned size = get_arg (f, 3);

        /* Buffer must be writable for read */
        check_user_buffer (buffer, size, true);
        f->eax = sys_read (fd, buffer, size);
        break;
      }

    case SYS_WRITE:
      {
        int fd = get_arg (f, 1);
        const void *buffer = (const void *) get_arg (f, 2);
        unsigned size = get_arg (f, 3);

        check_user_buffer (buffer, size, false);
        f->eax = sys_write (fd, buffer, size);
        break;
      }

    default:
      sys_exit (-1);
    }
}

/* Read a byte from user address UADDR. Returns -1 on fault. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Write a byte to user address UDST. Returns false on fault. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/* Read a byte from user memory; exit(-1) on invalid address */
static uint8_t
read_user_byte (const uint8_t *uaddr)
{
  int byte;

  if (!is_user_vaddr (uaddr))
    sys_exit (-1);
  byte = get_user (uaddr);
  if (byte == -1)
    sys_exit (-1);
  return byte;
}

/* Check readability, and if writable verify by writing back the same byte */
static void
check_user_byte (const uint8_t *uaddr, bool writable)
{
  uint8_t byte = read_user_byte (uaddr);

  if (writable && !put_user ((uint8_t *) uaddr, byte))
    sys_exit (-1);
}

/* Check that the user buffer is valid across each page it touches */
static void
check_user_buffer (const void *buffer, unsigned size, bool writable)
{
  const uint8_t *start = buffer;
  const uint8_t *end = start + size - 1;
  const uint8_t *p;

  if (size == 0)
    return;

  /* Check for overflow or address in kernel space */
  if (end < start || !is_user_vaddr (end))
    sys_exit (-1);

  check_user_byte (start, writable);
  for (p = pg_round_down (start) + PGSIZE; p <= end; p += PGSIZE)
    check_user_byte (p, writable);
}

/* Read argument from user stack, byte-by-byte in case it straddles a page */
static uint32_t
get_arg (struct intr_frame *f, int n)
{
  const uint8_t *p = (const uint8_t *) f->esp + n * sizeof (uint32_t);
  uint32_t word = 0;
  size_t i;

  for (i = 0; i < sizeof word; i++)
    word |= (uint32_t) read_user_byte (p + i) << (i * 8);
  return word;
}

static void
sys_halt (void)
{
  shutdown_power_off ();
}

static void
sys_exit (int status)
{
  thread_current ()->exit_status = status;
  thread_exit ();
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  /* Keyboard read */
  if (fd == STDIN_FILENO)
    {
      uint8_t *dst = buffer;
      unsigned i;

      for (i = 0; i < size; i++)
        dst[i] = input_getc ();
      return size;
    }
  return -1;
}

static int
sys_write (int fd, const void *buffer, unsigned size)
{
  /* Console write */
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  return -1;
}
