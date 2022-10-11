#ifndef MULTITHREAD_H
#define MULTITHREAD_H

typedef struct _MTJob MTJob;
typedef void* MTJobFunc(void* data);

void MTGlobalInit();
void MTGlobalFree();
size_t MTNumThreads();

// queue a job that will run func, passing data to it.
// MTResult will then return what func returns once the job completes.
// NOTE: !!!this should only be called from 1 thread!!!
// this tries to block as little as possible
MTJob* MTStart(MTJobFunc* func, void* data);

int MTDone(MTJob* j); // check if job is done. does not block. can be called concurrently

// !! NOTE: these funcs are only valid if MTDone returns non-zero!!!
void* MTResult(MTJob* j); // returns what func from MTStart returned
void MTFree(MTJob* j);

// short sleep to let other threads do stuff
// n is a counter that should be initialized to zero
void MTYield(size_t* n);

#endif
#if defined(MULTITHREAD_IMPLEMENTATION) && !defined(MULTITHREAD_UNIT)
#define MULTITHREAD_UNIT

#include <sched.h>

#ifndef NO_MULTITHREAD
#include <emscripten/wasm_worker.h>
#else
// any type of sleep in the main thread does not let anything do any work
#define emscripten_wasm_worker_sleep(x) sched_yield()
#endif

void MTYield(size_t* n) {
  if (*n < 4) {
    // just spin
  } else if (*n < 32 || (*n & 1)) {
    sched_yield(); // yield every other attempt, sleep otherwise
  }
  else {
#ifdef __EMSCRIPTEN__
    emscripten_wasm_worker_sleep(100);
#else
    struct timespec rqtp;
    rqtp.tv_sec  = 0;
    rqtp.tv_nsec = 100;
    nanosleep(&rqtp, 0);
#endif
  }
  ++*n;
}

#ifdef NO_MULTITHREAD
void MTGlobalInit() {

}

void MTGlobalFree() {

}

size_t MTNumThreads() {
  return 0;
}

MTJob* MTStart(MTJobFunc* func, void* data) {
  return (MTJob*)func(data);
}

int MTDone(MTJob* j) {
  return 1;
}

void* MTResult(MTJob* j) {
  return j;
}

void MTFree(MTJob* j) {

}

#else
// NOTE: I intentionally don't use atomics and lock-free because that would require
// platform specific code at the moment since mingw and msvc don't support C11 threads

#include "utils.c"
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(int, js_numThreads, (), {
  return window.navigator.hardwareConcurrency;
});

size_t MTNumThreads() {
  return js_numThreads();
}
#else
#define NUM_THREADS_FALLBACK 4

size_t MTNumThreads() {
  size_t count = 0;
#ifdef _GNU_SOURCE
  cpu_set_t cs;
  CPU_ZERO(&cs);

  pthread_attr_t attr;
  pthread_getattr_np(pthread_self(), &attr);
  int res = pthread_attr_getaffinity_np(&attr, sizeof(cs), &cs);
  pthread_attr_destroy(&attr);
  if (res) {
    perror("pthread_sched_getaffinity_np");
    return NUM_THREADS_FALLBACK;
  }

  RangeBefore(CPU_SETSIZE, i) {
    if (CPU_ISSET(i, &cs)) {
      ++count;
    }
  }
#endif
  return count ? count : NUM_THREADS_FALLBACK;
}
#endif

//
// to avoid headaches with the ABA problem, I have this setup to ensure minimal blocking
// when queueing and checking job completion.
//
// main thread pushes to a single consumer, single producer lockless stack. this is a lot
// simpler to implement and reason about than a many consumers version.
//
// queue thread dequeues from this queue, locks a mutex and pushes to the many consumers
// stack for the workers. it then signals the worker cond. this adds latency but at least
// it doesn't block the main thread.
// the double stack, besides being simple, reverses the order of jobs twice effectively making
// if like a queue
//
// workers wait on the mutex and cond and execute work. they then atomically set the job
// done flag
//
// main thread atomically checks the job done flag
//
// this means that the actual number of threads we spawn is MTNumThreads() + 1 which is fine
// since the queue thread doesn't use much cpu
//

typedef struct _MTJob {
  MTJobFunc* func;
  void* data;
  void* result;
  int terminate;
  atomic_int done;
  struct _MTJob* next; // used in the many consumers queue
} MTJob;

// locked queue: many producers, many consumers
// high latency, potentially lots of blocking when queueing due to workers spamming the mutex
static pthread_mutex_t workerMutex;
static pthread_cond_t workerCond;
MTJob* mtQueue;

// many consumers of the locked queue
pthread_t* workers;

#ifdef MULTITHREAD_DEBUG
static intmax_t workerId() {
  pthread_t t = pthread_self();
  BufEach(pthread_t, workers, x) {
    if (pthread_equal(*x, t)) {
      return x - workers;
    }
  }
  return -1;
}

#define mtdbg(...) \
  fprintf(stderr, "[worker %jd] ", workerId()); \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n")
#else
#define mtdbg(...)
#endif

static
void* MTWorker(void* ptr) {
  int terminate = 0;
  pthread_mutex_lock(&workerMutex);
  while (!terminate) {
    mtdbg("waiting for work");
    // this unlocks workerMutex and goes to sleep until signaled. relocks once signaled
    // not signaled unless there's any work. this way we don't spam this loop
    pthread_cond_wait(&workerCond, &workerMutex);
    MTJob* j = mtQueue;
    if (j) {
      if (!j->terminate) {
        mtQueue = mtQueue->next;
      } else {
        // make sure all other workers wake up to the termination job
        pthread_cond_broadcast(&workerCond);
      }
    }
    if (j) {
      mtdbg("working on %p", j);
      terminate |= j->terminate;
      if (!j->terminate) {
        j->result = j->func ? j->func(j->data) : 0;
      }
      mtdbg("work complete");
      atomic_fetch_add(&j->done, 1);
    }
  }
  mtdbg("terminating worker");
  pthread_mutex_unlock(&workerMutex);
  return 0;
}

// lockless stack: single producer, single consumer
// very little blocking when queueing since we just have to spin until no concurrent pop
// interrupts us

_Atomic(MTJob*) pending;
pthread_t pendingWorker;

static
void* MTPendingWorker(void* p) {
  int terminate = 0;
  while (!terminate) {
    MTJob* j;
    size_t n = 0;
    mtdbg("waiting for pending work");
tryAgain:
    MTYield(&n);
    // remember: there's only 1 producer and 1 consumer.
    // there is no concurrent access to head->next.
    // head->next is set once by the producer and read once by the consumer.
    // the head ptr is atomically swapped concurrently
    if (!(j = atomic_load(&pending))) {
      // no work
      goto tryAgain;
    }
    if (!atomic_compare_exchange_weak(&pending, &j, j->next)) {
      // pending didn't match j.
      // which means a new job was pushed while we were trying to pop so try again
      goto tryAgain;
    }
    terminate |= j->terminate;
    mtdbg("dispatching %p", j);
    pthread_mutex_lock(&workerMutex);
    j->next = mtQueue;
    mtQueue = j;
    // NOTE: the worker must be in cond_wait when we signal otherwise it's gonna miss it
    // so we must have the mutex locked when we signal
    pthread_cond_signal(&workerCond);
    pthread_mutex_unlock(&workerMutex);
  }
  mtdbg("queue worker terminating");
  return 0;
}

static
MTJob* _MTStart(int t, MTJobFunc* func, void* data) {
  MTJob* j = malloc(sizeof(MTJob));
  MTJob* headCopy;
  if (j) {
    MemZero(j);
    j->func = func;
    j->data = data;
    j->terminate = t;
    atomic_init(&j->done, 0);
    mtdbg("creating %p", j);
tryAgain:
    // no Yield here because we want as little latency as possible
    headCopy = atomic_load(&pending);
    j->next = headCopy;
    if (!atomic_compare_exchange_weak(&pending, &headCopy, j)) {
      // pending didn't match headCopy.
      // which means a job was popped while we were trying to push so try again
      goto tryAgain;
    }
  } else {
    perror("malloc");
    mtdbg("failed to create job for func %p data %p", func, data);
  }
  return j;
}

MTJob* MTStart(MTJobFunc* func, void* data) {
  return _MTStart(0, func, data);
}

int MTDone(MTJob* j) {
  return atomic_load(&j->done);
}

void* MTResult(MTJob* j) {
  return j->result;
}

void MTFree(MTJob* j) {
  free(j);
}

void MTGlobalInit() {
  mtdbg("%zu threads\n", MTNumThreads());
  pthread_mutex_init(&workerMutex, 0);
  pthread_cond_init(&workerCond, 0);
  BufReserve(&workers, MTNumThreads());
  BufEach(pthread_t, workers, t) {
    pthread_create(t, 0, MTWorker, 0);
  }
  pthread_create(&pendingWorker, 0, MTPendingWorker, 0);
}

void MTGlobalFree() {
  mtdbg("terminating");

  // this is a special job that is not removed from the queue and terminates workers
  MTJob* j = _MTStart(1, 0, 0);

  size_t n = 0;
  while (!MTDone(j)) {
    MTYield(&n);
  }

  BufEach(pthread_t, workers, t) {
    pthread_join(*t, 0);
  }
  pthread_join(pendingWorker, 0);
  pthread_mutex_destroy(&workerMutex);
  pthread_cond_destroy(&workerCond);
  BufFree(&workers);
}
#endif

#endif
