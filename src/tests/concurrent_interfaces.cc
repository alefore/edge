#include "src/tests/concurrent_interfaces.h"

#include <glog/logging.h>

namespace afc::tests::concurrent {
namespace {
Handler* global_handler = nullptr;
}

Handler* GetGlobalHandler() { return global_handler; }

void SetGlobalHandler(Handler* handler) {
  CHECK((global_handler == nullptr) != (handler == nullptr));
  global_handler = handler;
}

}  // namespace afc::tests::concurrent
