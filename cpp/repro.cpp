// Stress reproducer: destroy SyncCallbacks on background threads while the
// runtime is torn down (collection cleared, then runtime freed), as RN does.
#include <hermes/hermes.h>
#include <react/bridging/Bridging.h>
#include <pthread.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace facebook;
using namespace facebook::react;

// Thread-safe invoker that never runs work (the JS thread is gone).
struct DeadInvoker : CallInvoker {
  std::mutex m;
  std::vector<CallFunc> q;
  void invokeAsync(CallFunc &&f) noexcept override {
    std::scoped_lock l(m);
    q.push_back(std::move(f));
  }
  void invokeSync(CallFunc &&) override {}
};

int main(int argc, char **argv) {
  int iters = argc > 1 ? atoi(argv[1]) : 200;
  int threads = argc > 2 ? atoi(argv[2]) : 32;
  int perThread = argc > 3 ? atoi(argv[3]) : 200;
  for (int it = 0; it < iters; it++) {
    std::shared_ptr<jsi::Runtime> rt = facebook::hermes::makeHermesRuntime();
    auto invoker = std::make_shared<DeadInvoker>();
    auto fn = rt->global()
                  .getPropertyAsFunction(*rt, "eval")
                  .call(*rt, "(function(){})")
                  .getObject(*rt)
                  .getFunction(*rt);
    std::vector<std::vector<std::unique_ptr<SyncCallback<void()>>>> cbs(threads);
    for (auto &v : cbs)
      for (int i = 0; i < perThread; i++)
        v.push_back(std::make_unique<SyncCallback<void()>>(
            *rt, jsi::Function(fn.getFunction(*rt)), invoker));
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; t++)
      ts.emplace_back([&, t] {
        pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
        ready++;
        while (!go) { std::this_thread::yield(); }
        for (auto &cb : cbs[t]) cb.reset();
      });
    while (ready < threads) {}
    go = true;
    std::this_thread::yield();
    { jsi::Function tmp = std::move(fn); }
    LongLivedObjectCollection::get(*rt).clear(); // ~TurboModuleBinding
    rt.reset(); // Hermes frees PointerValue chunks
    for (auto &t : ts) t.join();
  }
  printf("done %d iterations without a crash\n", iters);
}
