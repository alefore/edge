#ifndef __AFC_EDITOR_LAZY_STRING_TRIM_H__
#define __AFC_EDITOR_LAZY_STRING_TRIM_H__

#include <memory>
#include <string>
#include "lazy_string.h"

namespace afc {
namespace editor {

// Returns a copy with all left space characters removed.
std::shared_ptr<LazyString> StringTrimLeft(std::shared_ptr<LazyString> a,
                                           wstring space_characters);

}  // namespace editor
}  // namespace afc

#endif // __AFC_EDITOR_LAZY_STRING_TRIM_H__
