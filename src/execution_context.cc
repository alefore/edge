#include "src/execution_context.h"

#include "src/language/once_only_function.h"
#include "src/status.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;

using afc::concurrent::ThreadPoolWithWorkQueue;
using afc::concurrent::WorkQueue;
using afc::language::Error;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::VisitOptional;

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

futures::ValueOrError<gc::Root<vm::Value>> ExecutionContext::EvaluateFile(
    infrastructure::Path path) {
  return std::visit(
      overload{
          [environment = environment_, work_queue = thread_pool_->work_queue(),
           path](NonNull<std::unique_ptr<vm::Expression>> expression) {
            LOG(INFO) << "Evaluating file: " << path;
            return Evaluate(
                std::move(expression), environment.pool(), environment,
                [path, work_queue](OnceOnlyFunction<void()> resume) {
                  LOG(INFO) << "Evaluation of file yields: " << path;
                  work_queue->Schedule(
                      WorkQueue::Callback{.callback = std::move(resume)});
                });
          },
          [weak_status = status_](
              Error error) -> futures::ValueOrError<gc::Root<vm::Value>> {
            LOG(INFO) << "Compilation error: " << error;
            VisitOptional(
                [&error](gc::Root<Status> status) { status.ptr()->Set(error); },
                [] {}, weak_status.Lock());

            return futures::Past(error);
          }},
      vm::CompileFile(path, environment_.pool(), environment()));
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
ExecutionContext::Expand() const {
  return {};
}
}  // namespace afc::editor