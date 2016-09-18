#ifndef __AFC_EDITOR_LOWERCASE_H__
#define __AFC_EDITOR_LOWERCASE_H__

#include <memory>

#include "lazy_string.h"

namespace afc {
namespace editor {

using std::shared_ptr;

shared_ptr<LazyString> LowerCase(shared_ptr<LazyString> input);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LOWERCASE_H__
