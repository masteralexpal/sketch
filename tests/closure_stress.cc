/**
 * @file closure_stress.cc
 * @brief closure.h 压测台：多线程生产/消费闭包 + 对象生灭竞态。
 *
 * 场景矩阵：
 *   A. lambda 绑定，跨线程移动后执行（精确对账）
 *   B. PMF + shared_ptr receiver，跨线程（精确对账）
 *   C. PMF + weak_ptr receiver，与"搅碎机"线程的对象销毁赛跑
 *      （执行次数不确定，不参与对账，专喂 TSan）
 *   D. Then 链（两段各记一笔）
 *   E. 构造后不执行直接销毁（专喂 LSan：绑定状态必须释放）
 *   F. move-only 绑定参数
 *   G. Repeating 复制 + 转 Once 后执行 3 次（对账 ×3）
 */

#include "../base/closure.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr int kProducers = 4;
constexpr int kConsumers = 4;
constexpr long long kPerProducer = 200'000;

std::atomic<long long> g_sum{0};     // 实际执行总额
std::atomic<long long> g_weak_hit{0}; // weak receiver 实际执行次数（不确定）

struct Adder {
  void Add(long long x) { g_sum.fetch_add(x, std::memory_order_relaxed); }
  void AddWeak(long long) { g_weak_hit.fetch_add(1, std::memory_order_relaxed); }
};

// ─── 队列 + 停止协议（改状态 → 叫醒 → join）─────────────────────────
std::mutex g_mtx;
std::deque<lpa::OnceClosure<void()>> g_queue;
std::atomic<bool> g_producers_done{false};

// ─── 搅碎机：不断更换全局 Adder，让 weak receiver 持续扑空/命中 ──────
std::mutex g_current_mtx;
std::shared_ptr<Adder> g_current = std::make_shared<Adder>();
std::atomic<bool> g_churn_stop{false};

void Churner() {
  while (!g_churn_stop.load(std::memory_order_relaxed)) {
    auto fresh = std::make_shared<Adder>();
    {
      std::lock_guard lk(g_current_mtx);
      g_current = std::move(fresh); // 旧的在这里销毁
    }
  }
}

// ─── 生产者：轮转制造 7 种形状的闭包，本地记账 ──────────────────────
void Producer(int seed, long long &local_expected) {
  const long long base_v = seed * 1'000'000;
  for (long long i = 0; i < kPerProducer; ++i) {
    const long long v = base_v + i; // 每个闭包的记账值唯一（便于排查）
    lpa::OnceClosure<void()> task;
    switch (i % 7) {
    case 0: // A. lambda
      task = lpa::BindOnce([](long long x) { g_sum.fetch_add(x); }, v);
      local_expected += v;
      break;
    case 1: { // B. PMF + shared_ptr（这个对象全程活着 → 精确对账）
      static auto permanent = std::make_shared<Adder>();
      task = lpa::BindOnce(&Adder::Add, permanent, v);
      local_expected += v;
      break;
    }
    case 2: { // C. PMF + weak_ptr（与销毁赛跑，不计账）
      std::shared_ptr<Adder> sp;
      {
        std::lock_guard lk(g_current_mtx);
        sp = g_current;
      }
      task = lpa::BindOnce(&Adder::AddWeak, std::weak_ptr<Adder>(sp), v);
      break;
    }
    case 3: { // D. Then 链，两段各记一笔
      auto first = lpa::BindOnce([](long long x) { g_sum.fetch_add(x); }, v);
      task = std::move(first).Then([v] { g_sum.fetch_add(v); });
      local_expected += 2 * v;
      break;
    }
    case 4: { // E. 构造后直接销毁，不进队列（喂 LSan）
      auto dead = lpa::BindOnce([](std::unique_ptr<long long>) {},
                                 std::make_unique<long long>(v));
      (void)dead;
      continue; // 不入队
    }
    case 5: // F. move-only 绑定参数
      task = lpa::BindOnce(
          [](std::unique_ptr<long long> p) { g_sum.fetch_add(*p); },
          std::make_unique<long long>(v));
      local_expected += v;
      break;
    case 6: { // G. Repeating 转 Once，执行 3 次
      auto rep = lpa::BindRepeating([](long long x) { g_sum.fetch_add(x); }, v);
      task = lpa::BindOnce(
          [](lpa::RepeatingClosure<void()> r) mutable {
            r.Run();
            r.Run();
            r.Run();
          },
          std::move(rep));
      local_expected += 3 * v;
      break;
    }
    }
    {
      std::lock_guard lk(g_mtx);
      g_queue.push_back(std::move(task));
    }
  }
}

// ─── 消费者：取一个执行一个 ─────────────────────────────────────────
void Consumer() {
  for (;;) {
    lpa::OnceClosure<void()> task;
    {
      std::lock_guard lk(g_mtx);
      if (!g_queue.empty()) {
        task = std::move(g_queue.front());
        g_queue.pop_front();
      } else if (g_producers_done.load(std::memory_order_relaxed)) {
        return;
      }
    }
    if (task) {
      std::move(task).Run();
    } else {
      std::this_thread::yield();
    }
  }
}

} // namespace

int main() {
  auto t0 = std::chrono::steady_clock::now();

  std::thread churn(Churner);
  std::vector<std::thread> consumers;
  for (int i = 0; i < kConsumers; ++i)
    consumers.emplace_back(Consumer);

  std::vector<std::thread> producers;
  std::vector<long long> expected(kProducers, 0);
  for (int i = 0; i < kProducers; ++i)
    producers.emplace_back(Producer, i, std::ref(expected[i]));

  for (auto &t : producers)
    t.join();
  g_producers_done.store(true, std::memory_order_relaxed);
  for (auto &t : consumers)
    t.join();
  g_churn_stop.store(true);
  churn.join();

  auto t1 = std::chrono::steady_clock::now();

  long long total_expected = 0;
  for (auto e : expected)
    total_expected += e;

  long long total = kProducers * kPerProducer;
  std::printf("闭包总数: %lld（含场景 C/E 不入账部分）\n", total);
  std::printf("weak receiver 实际执行: %lld 次（与销毁赛跑，次数不确定）\n",
              g_weak_hit.load());
  std::printf("对账: 期望 %lld，实际 %lld → %s\n", total_expected, g_sum.load(),
              total_expected == g_sum.load() ? "✅ 分毫不差" : "❌ 有错！");
  std::printf("队列残留: %zu（应为 0）\n", g_queue.size());
  std::printf("耗时: %.2f s\n",
              std::chrono::duration<double>(t1 - t0).count());
  return total_expected == g_sum.load() && g_queue.empty() ? 0 : 1;
}
