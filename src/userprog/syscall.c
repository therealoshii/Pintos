#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

static int get_user (const uint8_t *uaddr);
static uint8_t read_user_byte (const uint8_t *uaddr);
static void check_user_buffer (const void *buffer, unsigned size);
static uint32_t get_arg (struct intr_frame *, int n);

static void sys_exit (int status) NO_RETURN;
static int sys_write (int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads the system call number and arguments from the user stack
   and hands them to the function that implements the call.  A
   return value, if any, goes back to the user program in EAX. */
static void
syscall_handler (struct intr_frame *f)
{
  switch (get_arg (f, 0))
    {
    case SYS_EXIT:
      sys_exit (get_arg (f, 1));

    case SYS_WRITE:
      {
        int fd = get_arg (f, 1);
        const void *buffer = (const void *) get_arg (f, 2);
        unsigned size = get_arg (f, 3);

        check_user_buffer (buffer, size);
        f->eax = sys_write (fd, buffer, size);
        break;
      }

    default:
      /* Unknown or not yet implemented: terminate the process. */
      sys_exit (-1);
    }
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Returns the byte at user virtual address UADDR.  Terminates the
   process if UADDR is a kernel address or is not mapped. */
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

/* Terminates the process unless all SIZE bytes of the user buffer
   at BUFFER can be read.  Mappings are per page, so it is enough
   to probe one byte in every page that the buffer touches. */
static void
check_user_buffer (const void *buffer, unsigned size)
{
  const uint8_t *start = buffer;
  const uint8_t *end = start + size - 1;
  const uint8_t *p;

  if (size == 0)
    return;

  /* Reject a buffer that wraps around the top of the address
     space or reaches into kernel memory. */
  if (end < start || !is_user_vaddr (end))
    sys_exit (-1);

  read_user_byte (start);
  for (p = pg_round_down (start) + PGSIZE; p <= end; p += PGSIZE)
    read_user_byte (p);
}

/* Returns the Nth 32-bit word above the user stack pointer saved
   in F.  The caller pushed its arguments and then the system call
   number, so word 0 is the number and words 1, 2, ... are the
   arguments, each taking 4 bytes whatever its type.
   The word is read one checked byte at a time, because the stack
   pointer itself may be bad and a word can straddle two pages. */
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

/* Terminates the current process with exit code STATUS. */
static void
sys_exit (int status)
{
  /* Record the status so process_exit() can report it. */
  thread_current ()->exit_status = status;
  thread_exit ();
}

/* Writes SIZE bytes from BUFFER to file descriptor FD.  Returns
   the number of bytes written, or -1 on failure. */
static int
sys_write (int fd, const void *buffer, unsigned size)
{
  /* Only the console is supported for now. */
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  return -1;
}
