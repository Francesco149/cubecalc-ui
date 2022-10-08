#ifndef MULTITHREAD_H
#define MULTITHREAD_H

typedef struct _MTJob MTJob;
typedef void* MTJobFunc(void* data);

void MTGlobalInit();
void MTGlobalFree();
size_t MTNumThreads();

// queue a job that will run func, passing data to it.
// MTJoin will then return what func returns once the job completes.
MTJob* MTStart(MTJobFunc* func, void* data);

void* MTJoin(MTJob* j); // wait for job do be done
int MTDone(MTJob* j); // check if job is done
void MTFree(MTJob* j);

#endif
#if defined(MULTITHREAD_IMPLEMENTATION) && !defined(MULTITHREAD_UNIT)
#define MULTITHREAD_UNIT

// NOTE: I intentionally don't use atomics and lock-free because that would require
// platform specific code at the moment since mingw and msvc don't support C11 threads

#include "utils.c"
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

#ifdef _GNU_SOURCE
size_t MTNumThreads() {
  cpu_set_t cs;
  CPU_ZERO(&cs);

  if (pthread_sched_getaffinity_np(pthread_self(), sizeof(cs), &cs)) {
    perror("pthread_sched_getaffinity_np");
    return NUM_THREADS_FALLBACK;
  }

  size_t count = 0;
  RangeUntil(CPU_SETSIZE, i) {
    if (CPU_ISSET(i, &cs)) {
      ++count;
    }
  }

  return count ? count : NUM_THREADS_FALLBACK;
}
#else
size_t MTNumThreads() {
  return NUM_THREADS_FALLBACK;
}
#endif
#endif

typedef struct _MTJob {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  MTJobFunc* func;
  void* data;
  void* result;
  int terminate;
  int done;
  struct _MTJob* next;
} MTJob;

pthread_t* workers;
static pthread_mutex_t workerMutex;
static pthread_cond_t workerCond;
MTJob* mtQueue;
MTJob* mtQueueLast;

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
  pthread_mutex_lock(&workerMutex);
  while (1) {
    mtdbg("waiting for work");
    // this unlocks workerMutex and goes to sleep until signaled. relocks once signaled
    // not signaled unless there's any work. this way we don't spam this loop
    pthread_cond_wait(&workerCond, &workerMutex);
    MTJob* j = mtQueue;
    if (j) {
      // there should be no need to lock j->mutex because terminate is only set on start
      if (!j->terminate) {
        mtQueue = mtQueue->next;
        if (!mtQueue) mtQueueLast = 0;
      }
    }
    if (j) {
      mtdbg("working on %p\n", &j->cond);
      if (!j->terminate) {
        j->result = j->func ? j->func(j->data) : 0;
      }
      mtdbg("work complete");
      pthread_mutex_lock(&j->mutex);
      j->done = 1;
      pthread_mutex_unlock(&j->mutex);

      mtdbg("signaling %p", &j->cond);
      pthread_cond_signal(&j->cond);

      if (j->terminate) {
        mtdbg("terminating worker");
        break;
      }
    }
  }
  pthread_mutex_unlock(&workerMutex);
  return 0;
}

static
MTJob* _MTStart(int t, MTJobFunc* func, void* data) {
  MTJob* j = malloc(sizeof(MTJob));
  MemZero(j);
  j->func = func;
  j->data = data;
  j->terminate = t;
  pthread_mutex_init(&j->mutex, 0);
  pthread_cond_init(&j->cond, 0);
  mtdbg("creating %p", &j->cond);
  pthread_mutex_lock(&workerMutex);
  if (!mtQueueLast) {
    mtQueue = mtQueueLast = j;
  } else {
    mtQueueLast->next = j;
    mtQueueLast = j;
  }
  pthread_mutex_unlock(&workerMutex);
  pthread_cond_signal(&workerCond);
  return j;
}

MTJob* MTStart(MTJobFunc* func, void* data) {
  return _MTStart(0, func, data);
}

void* MTJoin(MTJob* j) {
  pthread_mutex_lock(&j->mutex);
  while (!j->done) {
    mtdbg("waiting %p", &j->cond);
    pthread_cond_wait(&j->cond, &j->mutex);
  }
  pthread_mutex_unlock(&j->mutex);
  return j->result;
}

int MTDone(MTJob* j) {
  // TODO: make lockless version for platforms that support it.
  // polling on this mutex might delay job completion a lot
  pthread_mutex_lock(&j->mutex);
  int done = j->done;
  pthread_mutex_unlock(&j->mutex);
  return done;
}

void MTFree(MTJob* j) {
  pthread_mutex_destroy(&j->mutex);
  pthread_cond_destroy(&j->cond);
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
}

void MTGlobalFree() {
  mtdbg("terminating");

  // this is a special job that is not removed from the queue and terminates workers
  MTJob* j = _MTStart(1, 0, 0);
  pthread_cond_broadcast(&workerCond);
  MTJoin(j);

  BufEach(pthread_t, workers, t) {
    pthread_join(*t, 0);
  }
  pthread_mutex_destroy(&workerMutex);
  pthread_cond_destroy(&workerCond);
  BufFree(&workers);
}

#endif
