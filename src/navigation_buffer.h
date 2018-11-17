#ifndef __AFC_EDITOR_NAVIGATION_BUFFER_H__
#define __AFC_EDITOR_NAVIGATION_BUFFER_H__

#include <memory>

#include "command.h"

namespace afc {
namespace editor {

std::unique_ptr<Command> NewNavigationBufferCommand();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_NAVIGATION_BUFFER_H__
