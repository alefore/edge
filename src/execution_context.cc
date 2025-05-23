#include "src/execution_context.h"

namespace gc = afc::language::gc;

using afc::concurrent::ThreadPoolWithWorkQueue;
using afc::language::NonNull;

namespace afc::editor {
ExecutionContext::ExecutionContext(
    gc::Root<vm::Environment> environment, gc::WeakPtr<Status> status,
    NonNull<std::shared_ptr<ThreadPoolWithWorkQueue>> thread_pool)
    : environment_(std::move(environment)),
      status_(std::move(status)),
      thread_pool_(std::move(thread_pool)) {}

gc::Root<vm::Environment> ExecutionContext::environment() const {
  return environment_;
}

ThreadPoolWithWorkQueue& ExecutionContext::thread_pool() const {
  return thread_pool_.value();
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
ExecutionContext::Expand() const {
  return {};
}
}  // namespace afc::editor