// closure.h 冒烟测试：覆盖每条调用路径。
#include "../base/closure.h"

#include <cassert>
#include <cstdio>
#include <memory>

int FreeAdd(int a, int b) { return a + b; }

struct Counter {
  int total = 0;
  void Add(int x) { total += x; }
  int Get() const { return total; }
};

void UseRaw(Counter *c, int x) { c->Add(x); }
void TakeShared(std::shared_ptr<Counter> c) { c->Add(1); }

int main() {
  // 1. 自由函数 + 部分绑定
  auto c1 = lpa::BindOnce(&FreeAdd, 40);
  assert(std::move(c1).Run(2) == 42);
  std::puts("1. 自由函数 + 部分绑定            OK");

  // 2. lambda + Then 链
  auto c2 = lpa::BindOnce([](int x) { return x * 2; }, 21);
  auto c3 = std::move(c2).Then([](int r) { return r + 1; });
  assert(std::move(c3).Run() == 43);
  std::puts("2. lambda + Then 链                 OK");

  // 3. PMF + shared_ptr receiver（对象随 closure 存活）
  auto sp = std::make_shared<Counter>();
  auto c4 = lpa::BindRepeating(&Counter::Add, sp);
  c4.Run(5);
  c4.Run(7);
  assert(sp->total == 12);
  std::puts("3. PMF + shared_ptr receiver        OK");

  // 4. PMF + weak_ptr receiver：活着执行，死了静默跳过
  std::weak_ptr<Counter> wp;
  lpa::RepeatingClosure<void(int)> c5;
  {
    auto sp2 = std::make_shared<Counter>();
    wp = sp2;
    c5 = lpa::BindRepeating(&Counter::Add, wp);
    c5.Run(1);
    assert(sp2->total == 1);
  }
  assert(wp.expired());
  c5.Run(100); // 对象已死 → 静默跳过，不崩
  std::puts("4. PMF + weak_ptr receiver(活/死)   OK");

  // 5. PMF + reference_wrapper receiver（不转移所有权）
  Counter obj;
  auto c6 = lpa::BindRepeating(&Counter::Add, std::ref(obj));
  c6.Run(3);
  assert(obj.total == 3);
  std::puts("5. PMF + reference_wrapper receiver OK");

  // 6. move-only 绑定参数（Once 专属；Repeating 会 static_assert）
  auto c7 = lpa::BindOnce([](std::unique_ptr<int> p) { return *p; },
                           std::make_unique<int>(9));
  assert(std::move(c7).Run() == 9);
  std::puts("6. move-only 绑定参数               OK");

  // 7. Repeating → Once 转换
  lpa::OnceClosure<void(int)> c8 = std::move(c4);
  std::move(c8).Run(1);
  assert(sp->total == 13);
  std::puts("7. Repeating → Once 转换            OK");

  // 8. shared_ptr 作普通参数：参数是裸指针 → 解包 get()
  auto sp3 = std::make_shared<Counter>();
  auto c9 = lpa::BindOnce(&UseRaw, sp3, 10);
  std::move(c9).Run();
  assert(sp3->total == 10);
  std::puts("8. shared_ptr 解包为裸指针参数      OK");

  // 9. shared_ptr 作普通参数：参数本身就是 shared_ptr → 原样透传
  auto c10 = lpa::BindOnce(&TakeShared, sp3);
  std::move(c10).Run();
  assert(sp3->total == 11);
  std::puts("9. shared_ptr 原样透传              OK");

  // 10. Then 的 void 分支（此前从未被测过）
  int seq = 0;
  auto c11 = lpa::BindOnce([&seq] { seq = 1; })
                 .Then([&seq] { seq = 2; })
                 .Then([&seq] { seq = 3; });
  std::move(c11).Run();
  assert(seq == 3);
  std::puts("10. Then 的 void 分支 ×2 链          OK");

  // 11. 带 & 引用限定 operator() 的仿函数作 Then 后续
  //     （修复前：invoke_result 按 rvalue 推导会错误拒绝它）
  struct LvalueOnlyFn {
    int operator()(int x) & { return x + 100; }
  };
  auto c12 = lpa::BindOnce([](int x) { return x; }, 1)
                 .Then(LvalueOnlyFn{});
  assert(std::move(c12).Run() == 101);
  std::puts("11. &-限定 operator() 的 Then 后续   OK");

  std::puts("--- 全部通过 ---");
  return 0;
}
