/*
   Copyright 2017 Toyota Research Institute

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
#include "dreal/util/per_thread.h"

#include "ThreadPool/ThreadPool.h"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace dreal {
namespace {

TEST(PerThreadTest, SizeReflectsConstruction) {
  const PerThread<int> pt{4};
  EXPECT_EQ(pt.size(), 4);
}

// On a single thread, the factory runs exactly once and every call returns the
// same instance.
TEST(PerThreadTest, BuildsOncePerThreadAndReturnsSameInstance) {
  const PerThread<int> pt{1024};  // generous: the calling thread's id fits
  int build_count{0};
  const auto make = [&build_count]() {
    ++build_count;
    return std::make_unique<int>(42);
  };
  int& a{pt.GetOrCreate(make)};
  int& b{pt.GetOrCreate(make)};
  EXPECT_EQ(&a, &b) << "the same thread must get the same instance";
  EXPECT_EQ(build_count, 1) << "the factory must run exactly once per thread";
  EXPECT_EQ(a, 42);
}

// Different pool worker threads get distinct instances (the whole point of
// the table). Uses ThreadPool workers — the table's actual contract — not raw
// std::threads: only the owning thread (id 0) and pool workers (ids 1..N-1)
// may drive a PerThread cell.
TEST(PerThreadTest, DistinctInstancesAcrossThreads) {
  const PerThread<int> pt{3};
  std::mutex m;
  std::vector<int*> ptrs;
  std::condition_variable cv;
  int arrived{0};
  const auto worker = [&]() {
    int& v{pt.GetOrCreate([]() { return std::make_unique<int>(7); })};
    std::unique_lock<std::mutex> lk{m};
    ptrs.push_back(&v);
    ++arrived;
    cv.notify_all();
    // Hold the worker until both have run, so the two tasks land on the two
    // distinct pool threads rather than one worker draining the queue.
    cv.wait(lk, [&] { return arrived == 2; });
  };
  {
    ThreadPool pool{2};
    auto f1 = pool.enqueue(worker);
    auto f2 = pool.enqueue(worker);
    f1.get();
    f2.get();
  }
  ASSERT_EQ(ptrs.size(), 2u);
  EXPECT_NE(ptrs[0], ptrs[1])
      << "different pool workers must drive different instances";
}

// Worker thread ids are POOL-LOCAL and stable: pool workers occupy ids
// 1..N-1 regardless of how many pools existed before or coexist. Under the
// old process-global monotone id counter, a second pool's workers claimed ids
// beyond the first pool's (e.g. 4 here), so a PerThread cell sized for a small
// pool was driven by an out-of-range thread id — the loud per_thread guard
// crash seen when one process mixes --jobs widths (Debug-gate 2026-07-21).
TEST(PerThreadTest, CoexistingPoolsKeepIdsInRange) {
  std::mutex m;
  std::condition_variable cv;
  int arrived{0};
  std::vector<int> pool_a_ids;
  ThreadPool pool_a{3};
  const auto hold_and_record = [&]() {
    const int id{ThreadPool::get_thread_id()};
    std::unique_lock<std::mutex> lk{m};
    pool_a_ids.push_back(id);
    ++arrived;
    cv.notify_all();
    cv.wait(lk, [&] { return arrived == 3; });
  };
  auto a1 = pool_a.enqueue(hold_and_record);
  auto a2 = pool_a.enqueue(hold_and_record);
  auto a3 = pool_a.enqueue(hold_and_record);
  a1.get();
  a2.get();
  a3.get();
  // While pool A is still alive, a second, smaller pool's worker must see an
  // id inside ITS OWN range [0, 2), not a globally-advanced one.
  ThreadPool pool_b{1};
  const int b_id{pool_b.enqueue([]() {
                          return ThreadPool::get_thread_id();
                        }).get()};
  std::sort(pool_a_ids.begin(), pool_a_ids.end());
  EXPECT_EQ(pool_a_ids, (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(b_id, 1) << "a 1-worker pool's worker must have id 1; got a "
                        "globally-advanced id (per-pool ids are broken)";
}

// An out-of-range thread id fails loud (always-on throw), rather than the
// silent heap out-of-bounds the previous hand-rolled dispatchers risked. A
// zero-slot table makes every thread id (>= 0) out of range.
TEST(PerThreadTest, OutOfRangeThreadIdThrows) {
  const PerThread<int> pt{0};
  EXPECT_THROW(pt.GetOrCreate([]() { return std::make_unique<int>(0); }),
               std::runtime_error);
}

}  // namespace
}  // namespace dreal
