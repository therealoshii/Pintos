#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

struct lock filesys_lock;

/* One entry in a process's open file table */
struct open_file
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

static void syscall_handler (struct intr_frame *);

static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static uint8_t read_user_byte (const uint8_t *uaddr);
static void check_user_byte (const uint8_t *uaddr, bool writable);
static void check_user_buffer (const void *buffer, unsigned size,
                               bool writable);
static void check_user_string (const char *str);
static uint32_t get_arg (struct intr_frame *, int n);

static void sys_halt (void) NO_RETURN;
static void sys_exit (int status) NO_RETURN;
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

static struct open_file *lookup_fd (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
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

    case SYS_CREATE:
      {
        const char *file = (const char *) get_arg (f, 1);
        unsigned initial_size = get_arg (f, 2);

        check_user_string (file);
        f->eax = sys_create (file, initial_size);
        break;
      }

    case SYS_REMOVE:
      {
        const char *file = (const char *) get_arg (f, 1);

        check_user_string (file);
        f->eax = sys_remove (file);
        break;
      }

    case SYS_OPEN:
      {
        const char *file = (const char *) get_arg (f, 1);

        check_user_string (file);
        f->eax = sys_open (file);
        break;
      }

    case SYS_FILESIZE:
      f->eax = sys_filesize (get_arg (f, 1));
      break;

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

    case SYS_SEEK:
      sys_seek (get_arg (f, 1), get_arg (f, 2));
      break;

    case SYS_TELL:
      f->eax = sys_tell (get_arg (f, 1));
      break;

    case SYS_CLOSE:
      sys_close (get_arg (f, 1));
      break;

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

/* Check every byte of a user string up to and including the null */
static void
check_user_string (const char *str)
{
  const uint8_t *p = (const uint8_t *) str;

  while (read_user_byte (p) != '\0')
    p++;
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
  else
    {
      struct open_file *of = lookup_fd (fd);
      int bytes;

      if (of == NULL)
        return -1;
      lock_acquire (&filesys_lock);
      bytes = file_read (of->file, buffer, size);
      lock_release (&filesys_lock);
      return bytes;
    }
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
  else
    {
      struct open_file *of = lookup_fd (fd);
      int bytes;

      if (of == NULL)
        return -1;
      lock_acquire (&filesys_lock);
      bytes = file_write (of->file, buffer, size);
      lock_release (&filesys_lock);
      return bytes;
    }
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  bool ok;

  lock_acquire (&filesys_lock);
  ok = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return ok;
}

static bool
sys_remove (const char *file)
{
  bool ok;

  lock_acquire (&filesys_lock);
  ok = filesys_remove (file);
  lock_release (&filesys_lock);
  return ok;
}

static int
sys_open (const char *file)
{
  struct thread *cur = thread_current ();
  struct open_file *of;
  struct file *f;

  lock_acquire (&filesys_lock);
  f = filesys_open (file);
  lock_release (&filesys_lock);
  if (f == NULL)
    return -1;

  of = malloc (sizeof *of);
  if (of == NULL)
    {
      lock_acquire (&filesys_lock);
      file_close (f);
      lock_release (&filesys_lock);
      return -1;
    }
  of->fd = cur->next_fd++;
  of->file = f;
  list_push_back (&cur->open_files, &of->elem);
  return of->fd;
}

static int
sys_filesize (int fd)
{
  struct open_file *of = lookup_fd (fd);
  int size;

  if (of == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  size = file_length (of->file);
  lock_release (&filesys_lock);
  return size;
}

static void
sys_seek (int fd, unsigned position)
{
  struct open_file *of = lookup_fd (fd);

  if (of == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_seek (of->file, position);
  lock_release (&filesys_lock);
}

static unsigned
sys_tell (int fd)
{
  struct open_file *of = lookup_fd (fd);
  unsigned pos;

  if (of == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  pos = file_tell (of->file);
  lock_release (&filesys_lock);
  return pos;
}

static void
sys_close (int fd)
{
  struct open_file *of = lookup_fd (fd);

  /* Bad fd, already closed, or the console: nothing to do */
  if (of == NULL)
    return;
  list_remove (&of->elem);
  lock_acquire (&filesys_lock);
  file_close (of->file);
  lock_release (&filesys_lock);
  free (of);
}

/* Find FD in the current process's open file table, or NULL */
static struct open_file *
lookup_fd (int fd)
{
  struct list *files = &thread_current ()->open_files;
  struct list_elem *e;

  for (e = list_begin (files); e != list_end (files); e = list_next (e))
    {
      struct open_file *of = list_entry (e, struct open_file, elem);
      if (of->fd == fd)
        return of;
    }
  return NULL;
}

/* Close every file the current process has open. Called on exit. */
void
syscall_close_files (void)
{
  struct list *files = &thread_current ()->open_files;

  while (!list_empty (files))
    {
      struct open_file *of = list_entry (list_pop_front (files),
                                         struct open_file, elem);
      lock_acquire (&filesys_lock);
      file_close (of->file);
      lock_release (&filesys_lock);
      free (of);
    }
}
