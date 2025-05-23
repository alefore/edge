#ifndef __AFC_EDITOR_EXECUTION_CONTEXT_H__
#define __AFC_EDITOR_EXECUTION_CONTEXT_H__

#include <memory>
#include <vector>

#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
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
  const language::gc::WeakPtr<Status> status_;
  const language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue_;
  const language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>
      file_system_driver_;

 public:
  static language::gc::Root<ExecutionContext> New(
      language::gc::Ptr<vm::Environment>, language::gc::WeakPtr<Status>,
      language::NonNull<std::shared_ptr<concurrent::WorkQueue>>,
      language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>);

  ExecutionContext(
      ConstructorAccessTag, language::gc::Ptr<vm::Environment>,
      language::gc::WeakPtr<Status>,
      language::NonNull<std::shared_ptr<concurrent::WorkQueue>>,
      language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>);

  const language::gc::Ptr<vm::Environment>& environment() const;
  language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue() const;
  const language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>&
  file_system_driver() const;

  futures::ValueOrError<language::gc::Root<vm::Value>> EvaluateFile(
      infrastructure::Path path);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};
}  // namespace afc::editor
#endif
