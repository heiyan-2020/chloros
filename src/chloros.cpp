#include "chloros.h"
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include <iostream>
#include "common.h"

extern "C" {

// Assembly code to switch context from `old_context` to `new_context`.
void ContextSwitch(chloros::Context* old_context,
                   chloros::Context* new_context) __asm__("context_switch");

// Assembly entry point for a thread that is spawn. It will fetch arguments on
// the stack and put them in the right registers. Then it will call
// `ThreadEntry` for further setup.
void StartThread(void* arg) __asm__("start_thread");
}

namespace chloros {

namespace {

// Default stack size is 2 MB.
constexpr int const kStackSize{1 << 21};

constexpr int const alignment{1 << 4};

// Queue of threads that are not running.
std::vector<std::unique_ptr<Thread>> thread_queue{};

// Mutex protecting the queue, which will potentially be accessed by multiple
// kernel threads.
std::mutex queue_lock{};

// Current running thread. It is local to the kernel thread.
thread_local std::unique_ptr<Thread> current_thread{nullptr};

// Thread ID of initial thread. In extra credit phase, we want to switch back to
// the initial thread corresponding to the kernel thread. Otherwise there may be
// errors during thread cleanup. In other words, if the initial thread is
// spawned on this kernel thread, never switch it to another kernel thread!
// Think about how to achieve this in `Yield` and using this thread-local
// `initial_thread_id`.
thread_local uint64_t initial_thread_id;

thread_local int prevPos{-1};

}  // anonymous namespace

std::atomic<uint64_t> Thread::next_id;

std::mutex id_mutx;

Thread::Thread(bool create_stack)
    : id{0}, state{State::kWaiting}, context{}, stack{nullptr}, raw_stack{nullptr} {
  // FIXME: Phase 1
  static_cast<void>(create_stack);
  const std::lock_guard<std::mutex> lock(id_mutx);
  this->id = next_id++;
  this->stack = nullptr;
  if (create_stack) {
    this->raw_stack = new uint8_t[kStackSize + alignment];
    this->stack = this->raw_stack;
    while ((uintptr_t)this->stack % alignment != 0) {
      this->stack++;
    }
  }
  // These two initial values are provided for you.
  context.mxcsr = 0x1F80;
  context.x87 = 0x037F;
}

Thread::~Thread() {
  // FIXME: Phase 1
  delete this->raw_stack;
  this->raw_stack = nullptr;
}

void Thread::PrintDebug() {
  fprintf(stderr, "Thread %" PRId64 ": ", id);
  switch (state) {
    case State::kWaiting:
      fprintf(stderr, "waiting");
      break;
    case State::kReady:
      fprintf(stderr, "ready");
      break;
    case State::kRunning:
      fprintf(stderr, "running");
      break;
    case State::kZombie:
      fprintf(stderr, "zombie");
      break;
    default:
      break;
  }
  fprintf(stderr, "\n\tStack: %p\n", stack);
  fprintf(stderr, "\tRSP: 0x%" PRIx64 "\n", context.rsp);
  fprintf(stderr, "\tR15: 0x%" PRIx64 "\n", context.r15);
  fprintf(stderr, "\tR14: 0x%" PRIx64 "\n", context.r14);
  fprintf(stderr, "\tR13: 0x%" PRIx64 "\n", context.r13);
  fprintf(stderr, "\tR12: 0x%" PRIx64 "\n", context.r13);
  fprintf(stderr, "\tRBX: 0x%" PRIx64 "\n", context.rbx);
  fprintf(stderr, "\tRBP: 0x%" PRIx64 "\n", context.rbp);
  fprintf(stderr, "\tMXCSR: 0x%x\n", context.mxcsr);
  fprintf(stderr, "\tx87: 0x%x\n", context.x87);
}

void Initialize() {
  auto new_thread = std::make_unique<Thread>(false);
  new_thread->state = Thread::State::kWaiting;
  initial_thread_id = new_thread->id;
  current_thread = std::move(new_thread);
}

void Spawn(Function fn, void* arg) {
  auto new_thread = std::make_unique<Thread>(true);
  // FIXME: Phase 3
  // Set up the initial stack, and put it in `thread_queue`. Must yield to it
  // afterwards. How do we make sure it's executed right away?
  static_cast<void>(fn);
  static_cast<void>(arg);
  ASSERT((uintptr_t)new_thread->stack % alignment == 0, "stack needs to be aligned.");
  new_thread->context.rsp = (uintptr_t) new_thread->stack + kStackSize - 24;
  *reinterpret_cast<void **>(new_thread->context.rsp) = reinterpret_cast<void *>(StartThread);
  *reinterpret_cast<void **>(new_thread->context.rsp + 8) = reinterpret_cast<void *>(fn);
  *reinterpret_cast<void **>(new_thread->context.rsp + 16) = arg;
  
  new_thread->state = Thread::State::kReady;
  queue_lock.lock();
  thread_queue.emplace_back(std::move(new_thread));
  queue_lock.unlock();
  Yield();
}

bool Yield(bool only_ready) {
  // FIXME: Phase 3
  // Find a thread to yield to. If `only_ready` is true, only consider threads
  // in `kReady` state. Otherwise, also consider `kWaiting` threads. Be careful,
  // never schedule initial thread onto other kernel threads (for extra credit
  // phase)!
  static_cast<void>(only_ready);
  queue_lock.lock();
  if (thread_queue.size() == 0) {
    queue_lock.unlock();
    return false;
  }
  int i = (prevPos + 1) % thread_queue.size();
  int cnt = 0;
  while (cnt < thread_queue.size()) {
    if (thread_queue[i]->state == Thread::State::kReady || (!only_ready && thread_queue[i]->state == Thread::State::kWaiting)) {
      thread_queue[i]->state = Thread::State::kRunning;
      if (current_thread->state == Thread::State::kRunning) {
        current_thread->state = Thread::State::kReady;
      }
      current_thread.swap(thread_queue[i]);
      prevPos = i;
      queue_lock.unlock();
      ContextSwitch(&thread_queue[i]->context, &current_thread->context);
      return true;
    }
    i = (i + 1) % thread_queue.size();
    cnt++;
  }
  prevPos = i;
  queue_lock.unlock();
  return false;
}

void Wait() {
  current_thread->state = Thread::State::kWaiting;
  while (Yield(true)) {
    current_thread->state = Thread::State::kWaiting;
  }
}

void GarbageCollect() {
  // FIXME: Phase 4
}

std::pair<int, int> GetThreadCount() {
  // Please don't modify this function.
  int ready = 0;
  int zombie = 0;
  std::lock_guard<std::mutex> lock{queue_lock};
  for (auto&& i : thread_queue) {
    if (i->state == Thread::State::kZombie) {
      ++zombie;
    } else {
      ++ready;
    }
  }
  return {ready, zombie};
}

void ThreadEntry(Function fn, void* arg) {
  fn(arg);
  current_thread->state = Thread::State::kZombie;
  LOG_DEBUG("Thread %" PRId64 " exiting.", current_thread->id);
  // A thread that is spawn will always die yielding control to other threads.
  chloros::Yield();
  // Unreachable here. Why?
  ASSERT(false);
}

}  // namespace chloros
