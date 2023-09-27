#ifndef __AFC_TESTS_CONCURRENT_INTERFACES_H__
#define __AFC_TESTS_CONCURRENT_INTERFACES_H__

#include <functional>
#include <mutex>
#include <string>

namespace afc::tests::concurrent {
class Handler {
 public:
  virtual ~Handler() = default;

  virtual void Lock(const std::mutex& lock) = 0;
  virtual void Unlock(const std::mutex& lock) = 0;

  virtual std::function<void()> Wrap(std::function<void()> work) = 0;
};

// May return nullptr.
Handler* GetGlobalHandler();
void SetGlobalHandler(Handler* handler);
}  // namespace afc::tests::concurrent
#endif
