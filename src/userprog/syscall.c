#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  /* The caller pushed its arguments and then the system call
     number, so the number is the 32-bit word at the user stack
     pointer and each argument is in the next higher word.
     Temporary: these user addresses are not validated yet. */
  uint32_t *args = f->esp;

  switch (args[0])
    {
    case SYS_EXIT:
      /* Record the status so process_exit() can report it. */
      thread_current ()->exit_status = args[1];
      thread_exit ();

    case SYS_WRITE:
      {
        int fd = args[1];
        const char *buffer = (const char *) args[2];
        unsigned size = args[3];

        /* Only the console is supported for now. */
        if (fd == STDOUT_FILENO)
          {
            putbuf (buffer, size);
            f->eax = size;
          }
        else
          f->eax = -1;
        break;
      }

    default:
      printf ("system call!\n");
      thread_exit ();
    }
}
