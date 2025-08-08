#include "src/execution_context.h"

#include "src/language/gc_view.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/once_only_function.h"
#include "src/status.h"
#include "src/vm/constant_expression.h"
#include "src/vm/function_call.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::WorkQueue;
using afc::infrastructure::FileSystemDriver;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;

namespace afc::editor {
/* static */ gc::Root<ExecutionContext> ExecutionContext::New(
    gc::Ptr<vm::Environment> environment, std::weak_ptr<Status> status,
    NonNull<std::shared_ptr<WorkQueue>> work_queue,
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver) {
  gc::Pool& pool = environment.pool();
  return pool.NewRoot(MakeNonNullUnique<ExecutionContext>(
      ConstructorAccessTag{}, std::move(environment), std::move(status),
      std::move(work_queue), std::move(file_system_driver)));
}

ExecutionContext::ExecutionContext(
    ConstructorAccessTag, gc::Ptr<vm::Environment> environment,
    std::weak_ptr<Status> status,
    NonNull<std::shared_ptr<WorkQueue>> work_queue,
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver)
    : environment_(std::move(environment)),
      status_(std::move(status)),
      work_queue_(std::move(work_queue)),
      file_system_driver_(std::move(file_system_driver)) {}

const gc::Ptr<vm::Environment>& ExecutionContext::environment() const {
  return environment_;
}

NonNull<std::shared_ptr<WorkQueue>> ExecutionContext::work_queue() const {
  return work_queue_;
}

const language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>&
ExecutionContext::file_system_driver() const {
  return file_system_driver_;
}

namespace {
Error RegisterCompilationError(std::weak_ptr<Status> weak_status,
                               LazyString details, Error error,
                               ExecutionContext::ErrorHandling error_handling) {
  LOG(INFO) << "Compilation error: " << error;
  error = AugmentError(details + LazyString{L": error: "}, std::move(error));
  if (error_handling == ExecutionContext::ErrorHandling::kLogToStatus)
    VisitPointer(
        weak_status,
        [&error](NonNull<std::shared_ptr<Status>> status) {
          status->Set(error);
        },
        [] {});
  return error;
}
}  // namespace

futures::ValueOrError<gc::Root<vm::Value>> ExecutionContext::EvaluateFile(
    infrastructure::Path path) {
  return std::visit(
      overload{[environment = environment_.ToRoot(), work_queue = work_queue_,
                path](gc::Root<vm::Expression> expression) {
                 LOG(INFO) << "Evaluating file: " << path;
                 return Evaluate(
                     expression.ptr(), environment.pool(), environment,
                     [path, work_queue](OnceOnlyFunction<void()> resume) {
                       LOG(INFO) << "Evaluation of file yields: " << path;
                       work_queue->Schedule(
                           WorkQueue::Callback{.callback = std::move(resume)});
                     });
               },
               [weak_status = status_, path](
                   Error error) -> futures::ValueOrError<gc::Root<vm::Value>> {
                 return futures::Past(RegisterCompilationError(
                     weak_status, ToLazyString(path), error,
                     ErrorHandling::kLogToStatus));
               }},
      vm::CompileFile(path, environment_.pool(), environment_.ToRoot()));
}

ExecutionContext::CompilationResult::CompilationResult(
    ConstructorAccessTag, gc::Ptr<vm::Expression> expression,
    gc::Ptr<vm::Environment> environment,
    NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue)
    : expression_(std::move(expression)),
      environment_(std::move(environment)),
      work_queue_(std::move(work_queue)) {}

/* static */ gc::Root<ExecutionContext::CompilationResult>
ExecutionContext::CompilationResult::New(
    gc::Ptr<vm::Expression> expression, gc::Ptr<vm::Environment> environment,
    NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue) {
  return environment.pool().NewRoot(
      MakeNonNullUnique<ExecutionContext::CompilationResult>(
          ConstructorAccessTag{}, std::move(expression), std::move(environment),
          std::move(work_queue)));
}

gc::Ptr<vm::Expression> ExecutionContext::CompilationResult::expression()
    const {
  return expression_;
}

futures::ValueOrError<gc::Root<vm::Value>>
ExecutionContext::CompilationResult::evaluate() const {
  return Evaluate(expression_, environment_.pool(), environment_.ToRoot(),
                  [work_queue = work_queue_](OnceOnlyFunction<void()> resume) {
                    LOG(INFO) << "Evaluation of code yields.";
                    work_queue->Schedule(
                        WorkQueue::Callback{.callback = std::move(resume)});
                  });
};

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
ExecutionContext::CompilationResult::Expand() const {
  return {expression_.object_metadata(), environment_.object_metadata()};
}

futures::ValueOrError<gc::Root<vm::Value>> ExecutionContext::EvaluateString(
    LazyString code, ErrorHandling on_compilation_error) {
  VLOG(9) << "Evaluate string: " << code;
  return std::visit(
      overload{[](Error error) -> futures::ValueOrError<gc::Root<vm::Value>> {
                 // No need to handle error; `CompileString` already does it.
                 return futures::Past(error);
               },
               [&](gc::Root<ExecutionContext::CompilationResult> result) {
                 LOG(INFO) << "Code compiled, evaluating.";
                 return result->evaluate();
               }},
      CompileString(std::move(code), on_compilation_error));
}

ValueOrError<gc::Root<ExecutionContext::CompilationResult>>
ExecutionContext::CompileString(LazyString code, ErrorHandling error_handling) {
  TRACK_OPERATION(ExecutionContext_CompileString);
  gc::Root<vm::Environment> sub_environment =
      vm::Environment::New(environment_);
  return std::visit(
      overload{[sub_environment,
                work_queue = work_queue()](gc::Root<vm::Expression> expression)
                   -> ValueOrError<gc::Root<CompilationResult>> {
                 return CompilationResult::New(std::move(expression).ptr(),
                                               sub_environment.ptr(),
                                               work_queue);
               },
               [weak_status = status_, error_handling](
                   Error error) -> ValueOrError<gc::Root<CompilationResult>> {
                 return RegisterCompilationError(
                     weak_status, LazyString{L"🐜Compilation error"}, error,
                     error_handling);
               }},
      afc::vm::CompileString(code, sub_environment.pool(), sub_environment));
}

ValueOrError<gc::Root<ExecutionContext::CompilationResult>>
ExecutionContext::FunctionCall(const vm::Identifier& function_name,
                               std::vector<gc::Ptr<vm::Value>> arguments) {
  return VisitOptional(
      [&](vm::Environment::LookupResult procedure) {
        std::vector<gc::Root<vm::Expression>> arg_roots =
            container::MaterializeVector(
                std::move(arguments) |
                std::views::transform(
                    [](gc::Ptr<vm::Value> value) -> gc::Root<vm::Expression> {
                      return vm::NewConstantExpression(value.ToRoot());
                    }));
        return Success(CompilationResult::New(
            vm::NewFunctionCall(
                vm::NewConstantExpression(
                    std::get<gc::Root<vm::Value>>(procedure.value))
                    .ptr(),
                container::MaterializeVector(std::move(arg_roots) |
                                             gc::view::Ptr))
                .ptr(),
            environment_, work_queue()));
      },
      [&function_name] {
        return Error{ToLazyString(function_name) +
                     LazyString{L": Function not found"}};
      },
      environment_->Lookup(
          vm::Namespace{}, function_name,
          vm::types::Function{
              .output = vm::Type{vm::types::Void{}},
              .inputs = container::MaterializeVector(
                  arguments |
                  std::views::transform([](const gc::Ptr<vm::Value>& arg) {
                    return arg->type();
                  }))}));
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
ExecutionContext::Expand() const {
  return {environment_.object_metadata()};
}
}  // namespace afc::editor