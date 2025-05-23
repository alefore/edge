#include "src/execution_context.h"

#include "src/language/once_only_function.h"
#include "src/status.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;

using afc::concurrent::WorkQueue;
using afc::language::Error;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::VisitOptional;

namespace afc::editor {
/* static */ gc::Root<ExecutionContext> ExecutionContext::New(
    gc::Ptr<vm::Environment> environment, gc::WeakPtr<Status> status,
    NonNull<std::shared_ptr<WorkQueue>> work_queue) {
  gc::Pool& pool = environment.pool();
  return pool.NewRoot(MakeNonNullUnique<ExecutionContext>(
      ConstructorAccessTag{}, std::move(environment), std::move(status),
      std::move(work_queue)));
}

ExecutionContext::ExecutionContext(
    ConstructorAccessTag, gc::Ptr<vm::Environment> environment,
    gc::WeakPtr<Status> status, NonNull<std::shared_ptr<WorkQueue>> work_queue)
    : environment_(std::move(environment)),
      status_(std::move(status)),
      work_queue_(std::move(work_queue)) {}

const gc::Ptr<vm::Environment>& ExecutionContext::environment() const {
  return environment_;
}

NonNull<std::shared_ptr<WorkQueue>> ExecutionContext::work_queue() const {
  return work_queue_;
}

futures::ValueOrError<gc::Root<vm::Value>> ExecutionContext::EvaluateFile(
    infrastructure::Path path) {
  return std::visit(
      overload{[environment = environment_.ToRoot(), work_queue = work_queue_,
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
                     [&error](gc::Root<Status> status) {
                       status.ptr()->Set(error);
                     },
                     [] {}, weak_status.Lock());

                 return futures::Past(error);
               }},
      vm::CompileFile(path, environment_.pool(), environment_.ToRoot()));
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
ExecutionContext::Expand() const {
  return {environment_.object_metadata()};
}
}  // namespace afc::editor