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

namespace afc::editor {
class Status;

class ExecutionContext {
  const language::gc::Root<vm::Environment> environment_;
  const language::gc::WeakPtr<Status> status_;
  const language::NonNull<std::shared_ptr<concurrent::ThreadPoolWithWorkQueue>>
      thread_pool_;

 public:
  ExecutionContext(
      language::gc::Root<vm::Environment>, language::gc::WeakPtr<Status>,
      language::NonNull<std::shared_ptr<concurrent::ThreadPoolWithWorkQueue>>);

  language::gc::Root<vm::Environment> environment() const;
  concurrent::ThreadPoolWithWorkQueue& thread_pool() const;

  futures::ValueOrError<language::gc::Root<vm::Value>> EvaluateFile(
      infrastructure::Path path);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};
}  // namespace afc::editor
#endif
