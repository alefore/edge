#ifndef __AFC_EDITOR_VM_STACK_H__
#define __AFC_EDITOR_VM_STACK_H__

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/types.h"

namespace afc::vm {
// TODO(trivial, 2025-06-01): Use a ghost type for the index-in-stack values.
class StackFrameHeader {
  const std::unordered_map<Identifier, std::pair<size_t, Type>> arguments_;

 public:
  StackFrameHeader(std::vector<std::pair<Identifier, Type>> arguments);

  // If `identifier` was one of the identifiers given to the constructor,
  // returns its corresponding data.
  std::optional<std::pair<size_t, Type>> Find(const Identifier&);
};

class StackFrame {
  struct ConstructorAccessTag {};

  std::vector<language::gc::Ptr<Value>> arguments_;

 public:
  static language::gc::Root<StackFrame> New(
      language::gc::Pool& pool,
      std::vector<language::gc::Ptr<Value>> arguments);

  StackFrame(ConstructorAccessTag,
             std::vector<language::gc::Ptr<Value>> arguments);

  language::gc::Ptr<Value>& get(size_t index);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};

class Stack {
  struct ConstructorAccessTag {};

  std::vector<language::gc::Ptr<StackFrame>> stack_;

 public:
  static language::gc::Root<Stack> New(language::gc::Pool& pool);

  Stack(ConstructorAccessTag);

  StackFrame& current_frame();
  void Push(language::gc::Ptr<StackFrame> frame);
  void Pop();

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};
}  // namespace afc::vm
#endif
