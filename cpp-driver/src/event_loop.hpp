/*
  Copyright (c) DataStax, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef __CASS_THREAD_HPP_INCLUDED__
#define __CASS_THREAD_HPP_INCLUDED__

#include "async.hpp"
#include "atomic.hpp"
#include "cassconfig.hpp"
#include "deque.hpp"
#include "logger.hpp"
#include "macros.hpp"
#include "prepare.hpp"
#include "scoped_lock.hpp"
#include "timer.hpp"
#include "timerfd.hpp"
#include "utils.hpp"

#include <assert.h>
#include <uv.h>

namespace cass {

class EventLoop;

/**
 * A task executed on an event loop thread.
 */
class Task {
public:
  virtual ~Task() { }
  virtual void run(EventLoop* event_loop) = 0;
};

/**
 * An event loop thread. Use tasks to run logic on an event loop.
 */
class EventLoop {
public:
  typedef cass::Callback<void, EventLoop*> TimerCallback;

  EventLoop();

  virtual ~EventLoop();

  uv_loop_t* loop() { return &loop_; }

  /**
   * Initialize the event loop. This creates/initializes libuv objects that can
   * potentially fail.
   *
   * @param thread_name (WINDOWS DEBUG ONLY) Names thread for debugger (optional)
   * @return Returns 0 if successful, otherwise an error occurred.
   */
  int init(const String& thread_name = "");

  /**
   * Start the event loop thread.
   *
   * @return Returns 0 if successful, otherwise an error occurred.
   */
  int run();

  /**
   * Closes the libuv handles (thread-safe).
   */
  void close_handles();

  /**
   * Waits for the event loop thread to exit (thread-safe).
   */
  void join();

  /**
   * Queue a task to be run on the event loop thread (thread-safe).
   *
   * @param task A task to run on the event loop.
   */
  void add(Task* task);

  /**
   * Start the loop timer.
   *
   * @param timeout_us
   */
  void start_timer(uint64_t timeout_us, const TimerCallback& callback);

  /**
   * Stop the loop timer.
   */
  void stop_timer();

  /**
   * Determine if the timer is running.
   *
   * @return Returns true if the timer is running.
   */
  bool is_timer_running();

  /**
   * Start the IO time (if not started; 0)
   */
  void maybe_start_io_time();

  /**
   * Get the elapsed time for the processing of IO
   *
   * @return Elapsed IO processing time (in milliseconds)
   */
  uint64_t io_time_elapsed() const { return io_time_elapsed_; }

  /**
   * Get the event loop name; useful for debugging
   *
   * @return Name of the event loop
   */
  const String& name() const { return name_; }

protected:
  /**
   * A callback that's run before the event loop is run.
   */
  virtual void on_run();

  /**
   * A callback that's run after the event loop exits.
   */
  virtual void on_after_run() { }

private:
  class TaskQueue {
  public:
    TaskQueue();
    ~TaskQueue();

    bool enqueue(Task* task);
    bool dequeue(Task*& task);
    bool is_empty();

  private:
    uv_mutex_t lock_;
    Deque<Task*> queue_;
  };

private:
  static void internal_on_run(void* arg);
  void handle_run();

#ifdef HAVE_TIMERFD
  void on_timer(TimerFd* timer);
#else
  void on_timer(Timer* timer);
#endif

  void on_task(Async* async);

  uv_loop_t loop_;
  bool is_loop_initialized_;

#if defined(HAVE_SIGTIMEDWAIT) && !defined(HAVE_NOSIGPIPE)
  static void on_prepare(uv_prepare_t *prepare);

  uv_prepare_t prepare_;
#endif

  uv_thread_t thread_;
  bool is_joinable_;
  Async async_;
  TaskQueue tasks_;

#ifdef HAVE_TIMERFD
  TimerFd timer_;
#else
  uint64_t timeout_;
  Timer timer_;
#endif
  TimerCallback timer_callback_;

  Atomic<bool> is_closing_;

  uint64_t io_time_start_;
  uint64_t io_time_elapsed_;

  String name_;
};

/**
 * A generic group of event loop threads.
 */
class EventLoopGroup {
public:
  virtual ~EventLoopGroup() { }

  /**
   * Queue a task on any available event loop thread.
   * @param task The task to be run on an event loop.
   * @return The event loop that will run the task.
   */
  virtual EventLoop* add(Task* task) = 0;

  /**
   * Get a specific event loop by index.
   *
   * @param index The index of an event loop that must be less than the number of
   * event loops.
   * @return The event loop at index.
   */
  virtual EventLoop* get(size_t index) = 0;

  /**
   * Get the number of event loops in this group.
   *
   * @return The number of event loops.
   */
  virtual size_t size() const = 0;
};

/**
 * A groups of event loops where tasks are assigned to a specific event loop
 * using round-robin.
 */
class RoundRobinEventLoopGroup : public EventLoopGroup {
public:
  RoundRobinEventLoopGroup(size_t num_threads)
    : current_(0)
    , threads_(num_threads) { }

  int init(const String& thread_name = "");
  int run();
  void close_handles();
  void join();

  virtual EventLoop* add(Task* task);
  virtual EventLoop* get(size_t index) { return &threads_[index]; }
  virtual size_t size() const { return threads_.size(); }

private:
  Atomic<size_t> current_;
  DynamicArray<EventLoop> threads_;
};

} // namespace cass

#endif
