#ifndef __AFC_EDITOR_OPERATION_SCOPE_H__
#define __AFC_EDITOR_OPERATION_SCOPE_H__

#include <map>

#include "src/concurrent/protected.h"
#include "src/language/text/line_column.h"
#include "src/operation_scope_buffer_information.h"

namespace afc::editor {
class OpenBuffer;

class OperationScope {
 public:
  OperationScope() = default;
  OperationScope(OperationScope&&) = default;
  OperationScope(const OperationScope&) = delete;

  OperationScopeBufferInformation get(const OpenBuffer& buffer) const;

 private:
  using Map = std::map<const OpenBuffer*, OperationScopeBufferInformation>;
  mutable concurrent::Protected<Map> data_;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_OPERATION_SCOPE_H__
