#ifndef __AFC_EDITOR_NOOP_COMMAND_H__
#define __AFC_EDITOR_NOOP_COMMAND_H__

#include <memory>

#include "src/command.h"

namespace afc {
namespace editor {

class Command;

std::shared_ptr<Command> NoopCommand();

}  // namespace editor
}  // namespace afc

#endif
