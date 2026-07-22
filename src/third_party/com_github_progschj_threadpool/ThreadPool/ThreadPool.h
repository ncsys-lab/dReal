#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

class ThreadPool {
 public:
  ThreadPool(size_t);
  template <class F, class... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<typename std::result_of<F(Args...)>::type>;
  ~ThreadPool();

  // Returns this thread's POOL-LOCAL id: 0 for any thread that is not a pool
  // worker (in dReal, the IcpParallel main thread, which doubles as worker 0),
  // and i+1 for the pool's i-th worker, assigned once at thread start. Ids are
  // therefore always in [0, pool_size) for the pool driving the work — stable
  // across pool construction/destruction and across pools of differing widths
  // in one process. The previous design (a process-global monotone counter,
  // reset in ~ThreadPool) let a later/smaller pool's workers claim ids beyond
  // their own pool's width, crashing PerThread's range guard whenever one
  // process mixed --jobs widths (dReal Debug-gate finding, 2026-07-21), and
  // could alias two coexisting pools' workers to one id (silent slot sharing).
  // Modified by dReal (originally added by Soonho Kong).
  static int get_thread_id() { return tid_; }

 private:
  // need to keep track of threads so we can join them
  std::vector<std::thread> workers;
  // the task queue
  std::queue<std::function<void()> > tasks;

  // synchronization
  std::mutex queue_mutex;
  std::condition_variable condition;

  bool stop;

  // Pool-local worker id; defaults to 0 for non-worker threads.
  static thread_local int tid_;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads) : stop(false) {
  for (size_t i = 0; i < threads; ++i)
    workers.emplace_back([this, i] {
      tid_ = static_cast<int>(i) + 1;  // pool-local: worker i is id i+1
      for (;;) {
        std::function<void()> task;

        {
          std::unique_lock<std::mutex> lock(this->queue_mutex);
          this->condition.wait(
              lock, [this] { return this->stop || !this->tasks.empty(); });
          if (this->stop && this->tasks.empty()) return;
          task = std::move(this->tasks.front());
          this->tasks.pop();
        }

        task();
      }
    });
}

// add new work item to the pool
template <class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared<std::packaged_task<return_type()> >(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    // don't allow enqueueing after stopping the pool
    if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");

    tasks.emplace([task]() { (*task)(); });
  }
  condition.notify_one();
  return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
  }
  condition.notify_all();
  for (std::thread& worker : workers) worker.join();
}

#endif
