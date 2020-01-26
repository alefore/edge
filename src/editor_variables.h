#ifndef __AFC_EDITOR_EDITOR_VARIABLES_H__
#define __AFC_EDITOR_EDITOR_VARIABLES_H__

#include "src/variables.h"
#include "vm/public/value.h"

namespace afc {
namespace editor {
namespace editor_variables {

EdgeStruct<bool>* BoolStruct();
extern EdgeVariable<bool>* const multiple_buffers;

}  // namespace editor_variables
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_EDITOR_VARIABLES_H__
