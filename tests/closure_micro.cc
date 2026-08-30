// closure_micro.cc：纯闭包机器开销（单线程、无队列、无锁）
#include "../base/closure.h"

#include <chrono>
#include <cstdio>
#include <memory>

namespace {
long long g_sink = 0;

struct Adder {
  void Add(long long x) { g_sink += x; }
};
} // namespace

int main() {
  constexpr long long kN = 2'000'000;
  auto permanent = std::make_shared<Adder>();

  // 形状1：lambda 绑定（创建 + 移动 + 执行）
  auto t0 = std::chrono::steady_clock::now();
  for (long long i = 0; i < kN; ++i) {
    auto c = lpa::BindOnce([](long long x) { g_sink += x; }, i);
    std::move(c).Run();
  }
  auto t1 = std::chrono::steady_clock::now();
  std::printf("lambda       : %.1f ns/个\n",
              std::chrono::duration<double, std::nano>(t1 - t0).count() / kN);

  // 形状2：PMF + shared_ptr receiver
  t0 = std::chrono::steady_clock::now();
  for (long long i = 0; i < kN; ++i) {
    auto c = lpa::BindOnce(&Adder::Add, permanent, i);
    std::move(c).Run();
  }
  t1 = std::chrono::steady_clock::now();
  std::printf("PMF+sharedptr: %.1f ns/个\n",
              std::chrono::duration<double, std::nano>(t1 - t0).count() / kN);

  // 形状3：move-only 参数（unique_ptr，额外一次堆分配）
  t0 = std::chrono::steady_clock::now();
  for (long long i = 0; i < kN; ++i) {
    auto c = lpa::BindOnce(
        [](std::unique_ptr<long long> p) { g_sink += *p; },
        std::make_unique<long long>(i));
    std::move(c).Run();
  }
  t1 = std::chrono::steady_clock::now();
  std::printf("move-only参数: %.1f ns/个\n",
              std::chrono::duration<double, std::nano>(t1 - t0).count() / kN);

  std::printf("（sink=%lld，防优化）\n", g_sink);
  return 0;
}
