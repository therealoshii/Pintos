# Lab 2: User Programs — Task List

Goal: let user programs run on the kernel and talk to it through system calls.
Almost all work is in `userprog/process.c` and `userprog/syscall.c`, with small
additions to `threads/thread.h`, `threads/thread.c` and `userprog/exception.c`.

Rules that apply to every task:

- Do not edit anything under `src/tests/`. The graders replace that directory with their own copy.
- Do not modify `src/filesys/`.
- Print nothing beyond what the handout requires. Leftover debug `printf`s fail tests, because output is compared exactly.
- Nothing a user program does may crash, panic or assert-fail the kernel. Only `halt` may stop Pintos.

## Build and test

Start the container from the repo root:

```bash
docker run -it --rm --name pintos --mount type=bind,source="${PWD}",target=/home/PKUOS/pintos pkuflyingpig/pintos bash
```

Inside the container:

```bash
cd /home/PKUOS/pintos/src/userprog && make
cd build
make check                              # pass/FAIL per test + summary (screenshot this)
make grade                              # weighted score, written to the file `grade`
make tests/userprog/args-none.result    # run one test
cat tests/userprog/args-none.output     # see what that test printed
```

To re-run a single test, delete its `.output` file first. There is no `make report` target.

| Test set | Tests | Weight in `make grade` |
|---|---|---|
| Functionality (`tests/userprog`) | 28 | 35% |
| Robustness (`tests/userprog`) | 34 | 25% |
| No-VM (`multi-oom`) | 1 | 10% |
| Base file system (`tests/filesys/base`) | 13 | 30% |

## Task 0: Get a program to run at all (temporary scaffolding)

Nothing passes until a program can start, print and exit. These are stopgaps that later tasks replace.

- [x] In `setup_stack()` (`process.c`), change `*esp = PHYS_BASE;` to `*esp = PHYS_BASE - 12;` so programs that ignore their arguments stop page-faulting.
- [x] Make `process_wait()` block forever (for example an infinite loop) so Pintos does not power off before the program runs.
- [x] In `syscall_handler()`, read the syscall number from `f->esp` and handle just `SYS_EXIT` and `SYS_WRITE` to fd 1 (via `putbuf()`).

Check: `args-none` now prints `(args) begin` and `(args) argc = 0` instead of `system call!`, then page-faults reading `argv[0]` (fixed by Task 1). Every run times out because of the infinite wait (fixed by Task 7).

If the kernel boot-loops with "triple fault" right after `Pintos booting with`, the build directory is stale: run `make clean && make` in `src/userprog`.

## Task 1: Argument passing

Files: `process.c` (`process_execute`, `start_process`, `load`, `setup_stack`).

- [ ] Split the command line at spaces; runs of spaces count as one. `strtok_r()` from `lib/string.h` is the suggested tool (not `strtok`).
- [ ] Use only the first word as the program name: pass it to `thread_create()` (thread names hold 15 characters) and to `filesys_open()` in `load()`.
- [ ] Build the initial stack in the page just below `PHYS_BASE`, top down:
  1. the argument strings themselves (any order);
  2. padding so the stack pointer is a multiple of 4;
  3. a null pointer (`argv[argc]`);
  4. pointers to each string, last argument first, so `argv[0]` is at the lowest address;
  5. `argv` (address of `argv[0]`);
  6. `argc`;
  7. a fake return address (0).
- [ ] Set `*esp` to the address of the fake return address, and remove the `PHYS_BASE - 12` hack.
- [ ] Decide a limit on argument size (one 4 kB page is acceptable) and fail the load cleanly if it is exceeded.

`hex_dump()` (in `<stdio.h>`) is useful for checking the stack layout.

Tests: `args-none`, `args-single`, `args-multiple`, `args-many`, `args-dbl-space`.

## Task 2: Process termination message

Files: `process.c` (`process_exit`), `threads/thread.h`.

- [ ] Add an exit-status field to `struct thread` (inside `#ifdef USERPROG`).
- [ ] When a user process ends for any reason, print exactly `printf ("%s: exit(%d)\n", name, status);` where `name` is the program name without arguments.
- [ ] Do not print it for kernel threads that are not user processes, or when `halt` is called.
- [ ] A process killed by the kernel (bad pointer, exception) must report status `-1`. `kill()` in `exception.c` currently calls `thread_exit()` directly, so the status has to be -1 on that path.

Tests: every test checks this line, so it must be right before anything else passes.

## Task 3: Safe access to user memory

Files: `syscall.c`, and `exception.c` if you choose method B.

Every pointer from a user program may be null, unmapped, or at/above `PHYS_BASE`. Each of those must terminate the process with exit status -1 and leak nothing (no held locks, no unfreed pages).

- [ ] Choose one method:
  - **A (simpler):** validate before dereferencing, using `is_user_vaddr()` (`threads/vaddr.h`) and `pagedir_get_page()` (`userprog/pagedir.c`).
  - **B (faster):** check only that the address is below `PHYS_BASE`, then use the handout's `get_user()` / `put_user()`, and change `page_fault()` so a fault in kernel mode sets `eax` to `0xffffffff` and copies its old value into `eip`.
- [ ] Validate the stack pointer itself before reading the syscall number.
- [ ] Validate every byte of every argument, not just the first: a 4-byte argument or a buffer can straddle a page boundary.
- [ ] Validate strings up to and including their terminating `\0`.
- [ ] Validate the whole `[buffer, buffer + size)` range for `read` and `write`.

Tests: `sc-bad-sp`, `sc-bad-arg`, `sc-boundary`, `sc-boundary-2`, `*-bad-ptr`, `*-null`, `*-boundary`, `create-bound`, `bad-read`, `bad-write`, `bad-jump`, `bad-read2`, `bad-write2`, `bad-jump2`.

## Task 4: System call infrastructure

Files: `syscall.c`.

- [ ] In `syscall_handler()`, read the syscall number at `f->esp`, then the arguments at `esp + 4`, `esp + 8`, `esp + 12` (each argument is 4 bytes).
- [ ] Dispatch on the number (values in `lib/syscall-nr.h`) to one function per call.
- [ ] Put return values in `f->eax`.
- [ ] Write one shared helper for fetching arguments rather than repeating the code 13 times.
- [ ] Terminate the process on an unknown syscall number.

## Task 5: `halt`, `exit`, and console I/O

Files: `syscall.c`.

- [ ] `halt`: call `shutdown_power_off()` (`devices/shutdown.h`).
- [ ] `exit(status)`: record the status, then terminate the process (Task 2 prints the message).
- [ ] `write` to fd 1: send the buffer in one `putbuf()` call (splitting only very large buffers), and return the number of bytes written.
- [ ] `read` from fd 0: fill the buffer using `input_getc()`.

Tests: `halt`, `exit`.

## Task 6: File system calls

Files: `syscall.c`, `threads/thread.h` (per-process fd table), `process.c` (cleanup on exit).

- [ ] Create one global file-system lock and hold it around every call into `filesys/` code, including the executable load in `process.c`. Release it on every error path.
- [ ] Give each process its own fd table. fds start at 2; 0 and 1 are reserved for the console. fds are not inherited by children.
- [ ] `create(file, initial_size)`: returns true/false. Does not open the file.
- [ ] `remove(file)`: returns true/false. Removing an open file does not close it.
- [ ] `open(file)`: returns a new fd, or -1 on failure. Opening the same file twice gives two independent fds with separate positions.
- [ ] `filesize(fd)`: size in bytes.
- [ ] `read(fd, buffer, size)`: bytes read (0 at end of file), or -1 on error. Reading from fd 1 is an error.
- [ ] `write(fd, buffer, size)`: bytes actually written; files cannot grow, so this may be less than `size`. Writing to fd 0 is an error.
- [ ] `seek(fd, position)` and `tell(fd)`.
- [ ] `close(fd)`: closing an invalid fd, an already-closed fd, or fd 0/1 must not crash the kernel.
- [ ] On process exit, close every fd the process still has open.

Tests: `create-*`, `open-*`, `read-*`, `write-*`, `close-*`, `multi-child-fd`, and all 13 `tests/filesys/base` tests (`sm-*`, `lg-*`, `syn-read`, `syn-write`, `syn-remove`).

## Task 7: `exec` and `wait` (the hardest part)

Files: `process.c`, `syscall.c`, `threads/thread.h`, `threads/thread.c`.

- [ ] Define a per-child status record, allocated on the heap, holding at least: the child's tid, its exit status, whether it has exited, whether it has been waited on, and the synchronisation needed to wait for it.
- [ ] Give each process a list of these records for its children.
- [ ] Create and initialise the record before the child can possibly finish; pass what the child needs through the `aux` argument of `thread_create()`.
- [ ] `exec(cmd_line)`: the parent must block until the child's `load()` has succeeded or failed. Return the pid on success and -1 if the program cannot load or run.
- [ ] `process_wait(tid)`: replace the Task 0 infinite loop. Block until that child exits, then return its exit status.
- [ ] `wait` returns -1 immediately if the pid is not a direct child of the caller, or if the caller already waited on it.
- [ ] `wait` returns -1 if the child was killed by the kernel rather than calling `exit`.
- [ ] `wait` must work when the child has already exited before the parent calls it.
- [ ] Implement the `wait` system call by calling `process_wait()`.
- [ ] Free everything in both orders: parent exits first, or child exits first. Whoever goes last frees the shared record. A parent may exit without waiting for some or all children.
- [ ] Pintos must not power off until the initial process exits (`main()` in `threads/init.c` relies on `process_wait()` for this).

Tests: `exec-once`, `exec-multiple`, `exec-arg`, `exec-missing`, `exec-bad-ptr`, `wait-simple`, `wait-twice`, `wait-bad-pid`, `wait-killed`, `multi-recurse` (15 points).

## Task 8: Deny writes to running executables

Files: `process.c`, `threads/thread.h`.

The upstream handout calls this extra credit, but the three `rox-*` tests are in this repo's functionality rubric (9 points), so it is needed for full marks.

- [ ] In `load()`, call `file_deny_write()` on the executable and keep the file open instead of closing it at the end of `load()`.
- [ ] Store the open file in the process, and close it in `process_exit()` (closing re-enables writes).

Tests: `rox-simple`, `rox-child`, `rox-multichild`.

## Task 9: Resource cleanup under memory pressure

- [ ] Check every `malloc()` and `palloc_get_page()` for failure and fail the operation cleanly.
- [ ] Confirm that a process which dies at any point (failed load, bad pointer, normal exit) frees its pages, its fd table, its child records and its executable file, and releases the file-system lock.

Test: `multi-oom` (worth 10% of the grade on its own). It repeatedly spawns processes until memory runs out and fails if any memory is leaked.

## Task 10: Design document

- [ ] Get the project 2 template (`userprog.tmpl`); it is not in this repo.
- [ ] Copy it to `src/userprog/DESIGNDOC` and fill in every section: data structures, algorithms, synchronisation and rationale for argument passing and for system calls.

## Task 11: Submission

- [ ] Remove all debug output.
- [ ] Run `make check` from a clean build and screenshot the per-test results and the final summary.
- [ ] Submit: the completed `DESIGNDOC`, the Pintos source with your changes, and the screenshots.

## Suggested order

Task 0 → 1 → 2 → 4 → 3 → 5 → 6 → 7 → 8 → 9 → 10 → 11.
Tasks 1 to 5 can be developed side by side; Task 7 depends on all of them.
