#ifndef __AFC_EDITOR_EDITOR_VARIABLES_H__
#define __AFC_EDITOR_EDITOR_VARIABLES_H__

#include "src/variables.h"
#include "vm/public/value.h"

namespace afc::editor::editor_variables {

EdgeStruct<wstring>* StringStruct();
extern EdgeVariable<wstring>* const buffer_sort_order;

EdgeStruct<bool>* BoolStruct();
extern EdgeVariable<bool>* const multiple_buffers;
extern EdgeVariable<bool>* const focus;

EdgeStruct<int>* IntStruct();
extern EdgeVariable<int>* const buffers_to_retain;

}  // namespace afc::editor::editor_variables

#endif  // __AFC_EDITOR_EDITOR_VARIABLES_H__
