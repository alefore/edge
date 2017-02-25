#ifndef __AFC_EDITOR_DIRNAME_H__
#define __AFC_EDITOR_DIRNAME_H__

#include <memory>
#include <wchar.h>

#include "command.h"

namespace afc {
namespace editor {

std::wstring Dirname(std::wstring path);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DIRNAME_H__
