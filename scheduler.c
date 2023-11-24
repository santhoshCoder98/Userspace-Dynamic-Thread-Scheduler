/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

#include <setjmp.h>
#include "system.h"
#include "scheduler.h"
#include <signal.h>
#include <unistd.h>

#define STACK_SIZE (64 * 1024)
int id = 0;

struct thread
{
  jmp_buf name;
  void *arg;

  void (*entry)(void *arg);

  int tid;
  enum
  {
    status_thread_init,
    status_thread_running,
    status_thread_sleeping,
    status_thread_terminate
  } status;
  struct
  {
    void *memory_;
    void *memory;
  } stack;
  struct thread *link;
};

static struct
{
  struct thread *head;
  struct thread *current_thread;
  jmp_buf context;
} state;

void scheduler_destroy(void)
{
  struct thread *thread = state.head->link;

  for (; thread != state.head;)
  {
    struct thread *next = thread->link;
    free(thread->stack.memory_);
    free(thread);
    thread = next;
  }

  free(state.head->stack.memory_);
  free(state.head);

  state.head = NULL;
  state.current_thread = NULL;
}

int scheduler_create(scheduler_fnc_t fnc, void *arg)
{
  struct thread *new_thread = (struct thread *)malloc(sizeof(struct thread));
  size_t PAGE_SIZE = page_size();

  if (new_thread == NULL)
  {
    TRACE("Failed to allocate memory for a new thread");
    exit(0);
  }

  new_thread->arg = arg;
  new_thread->entry = fnc;
  new_thread->tid = id++;
  new_thread->status = status_thread_init;
  new_thread->stack.memory_ = malloc(STACK_SIZE + PAGE_SIZE);
  new_thread->stack.memory = memory_align(new_thread->stack.memory_, PAGE_SIZE);

  if (state.head == NULL)
  {
    state.head = new_thread;
    new_thread->link = new_thread;
    state.current_thread = new_thread;
  }
  else
  {
    new_thread->link = state.head->link;
    state.head->link = new_thread;
  }
  return 0;
}

struct thread *scheduler_thread_candidate(void)
{
  struct thread *thread, *next;

  for (thread = state.current_thread->link; thread != state.current_thread; thread = next)
  {
    next = thread->link;
    if (thread->status != status_thread_terminate)
    {
      return thread;
    }
  }

  return NULL;
}

void scheduler_change(void)
{
  uint64_t rsp;
  struct thread *thread = scheduler_thread_candidate();

  if (thread == NULL)
  {
    longjmp(state.context, 1);
  }

  state.current_thread = thread;
  if (state.current_thread->status == status_thread_init)
  {
    rsp = (uint64_t)state.current_thread->stack.memory + STACK_SIZE;
    __asm__ volatile("mov %[rs], %%rsp"
                     : [rs] "+r"(rsp)::);

    state.current_thread->entry(state.current_thread->arg);
    state.current_thread->status = status_thread_terminate;
    longjmp(state.context, 1);
  }
  else
  {
    state.current_thread->status = status_thread_running;
    longjmp(thread->name, 1);
  }
}

void scheduler_yield()
{
  if (setjmp(state.current_thread->name) == 0)
  {
    state.current_thread->status = status_thread_sleeping;
    longjmp(state.context, 1);
  }
}

void scheduler_execute(void)
{
  if (setjmp(state.context) == 0)
  {
    signal(SIGALRM, (__sighandler_t)scheduler_yield);
    alarm(1);
    scheduler_change();
  }
  else
  {
    if (scheduler_thread_candidate() != NULL)
    {
      signal(SIGALRM, (__sighandler_t)scheduler_yield);
      alarm(1);
      scheduler_change();
    }
  }
  scheduler_destroy();
}
