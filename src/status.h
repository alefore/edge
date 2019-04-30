#ifndef __AFC_EDITOR_STATUS_H__
#define __AFC_EDITOR_STATUS_H__

#include <memory>

#include "lazy_string.h"

namespace afc {
namespace editor {

using std::shared_ptr;

enum class OverflowBehavior { kModulo, kMaximum };
wstring ProgressString(size_t counter, OverflowBehavior overflow_behavior);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_STATUS_H__
