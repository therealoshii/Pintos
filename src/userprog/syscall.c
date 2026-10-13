#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

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
      f->eax = sys_write (get_arg (f, 1), (const void *) get_arg (f, 2),
                          get_arg (f, 3));
      break;

    default:
      /* Unknown or not yet implemented: terminate the process. */
      sys_exit (-1);
    }
}

/* Returns the Nth 32-bit word above the user stack pointer saved
   in F.  The caller pushed its arguments and then the system call
   number, so word 0 is the number and words 1, 2, ... are the
   arguments, each taking 4 bytes whatever its type.
   Temporary: the user address is not validated yet. */
static uint32_t
get_arg (struct intr_frame *f, int n)
{
  uint32_t *sp = f->esp;
  return sp[n];
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
