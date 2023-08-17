#ifndef __AFC_EDITOR_OPERATION_SCOPE_H__
#define __AFC_EDITOR_OPERATION_SCOPE_H__

#include <map>

#include "src/concurrent/protected.h"
#include "src/line_column.h"

namespace afc::editor {
class OpenBuffer;

// Freezes information about buffers in the scope of an operation. This makes
// the operation repeatable: if the information changes in the buffers, those
// changes won't affect repeated applications of the operation.
//
// This is used for PageUp/PageDown. If the screen sizes, we still scroll by the
// original screen size.
struct OperationScopeBufferInformation {
  LineNumberDelta screen_lines;
};

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
