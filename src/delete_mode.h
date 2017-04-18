#ifndef __AFC_EDITOR_DELETE_MODE_H__
#define __AFC_EDITOR_DELETE_MODE_H__

#include <memory>

#include "command.h"

namespace afc {
namespace editor {

std::unique_ptr<Command> NewDeleteCommand();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DELETE_MODE_H__
