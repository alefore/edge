#ifndef __AFC_EDITOR_EXECUTION_CONTEXT_H__
#define __AFC_EDITOR_EXECUTION_CONTEXT_H__

#include <memory>
#include <vector>

#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"

namespace afc::infrastructure {
class FileSystemDriver;
}
namespace afc::editor {
class Status;

class ExecutionContext {
  struct ConstructorAccessTag {};

  const language::gc::Ptr<vm::Environment> environment_;
  const std::weak_ptr<Status> status_;
  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue_;
  const language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>
      file_system_driver_;

 public:
  class CompilationResult {
    struct ConstructorAccessTag {};

    language::gc::Ptr<vm::Expression> expression_;
    language::gc::Ptr<vm::Environment> environment_;
    language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue_;

   public:
    CompilationResult(
        ConstructorAccessTag, language::gc::Ptr<vm::Expression> expression,
        language::gc::Ptr<vm::Environment> environment,
        language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue);

    static language::gc::Root<CompilationResult> New(
        language::gc::Ptr<vm::Expression> expression,
        language::gc::Ptr<vm::Environment> environment,
        language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue);

    language::gc::Ptr<vm::Expression> expression() const;

    futures::ValueOrError<language::gc::Root<vm::Value>> evaluate() const;

    std::vector<
        language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
    Expand() const;
  };

  static language::gc::Root<ExecutionContext> New(
      language::gc::Ptr<vm::Environment>, std::weak_ptr<Status>,
      language::NonNull<std::shared_ptr<concurrent::WorkQueue>>,
      language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>);

  ExecutionContext(
      ConstructorAccessTag, language::gc::Ptr<vm::Environment>,
      std::weak_ptr<Status>,
      language::NonNull<std::shared_ptr<concurrent::WorkQueue>>,
      language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>);

  const language::gc::Ptr<vm::Environment>& environment() const;
  language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue() const;
  const language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>&
  file_system_driver() const;

  futures::ValueOrError<language::gc::Root<vm::Value>> EvaluateFile(
      infrastructure::Path path);

  enum ErrorHandling { kIgnore, kLogToStatus };

  futures::ValueOrError<language::gc::Root<vm::Value>> EvaluateString(
      language::lazy_string::LazyString code,
      ErrorHandling on_compilation_error = kLogToStatus);

  language::ValueOrError<language::gc::Root<CompilationResult>> CompileString(
      language::lazy_string::LazyString,
      ErrorHandling error_handling = kLogToStatus);

  // Returns an function that, when run, is equivalent to running a given vm
  // function with some arguments.
  language::ValueOrError<language::gc::Root<CompilationResult>> FunctionCall(
      const vm::Identifier& function_name,
      std::vector<language::gc::Ptr<vm::Value>> arguments);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};
}  // namespace afc::editor
#endif
