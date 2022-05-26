#ifndef __AFC_EDITOR_EDITOR_VARIABLES_H__
#define __AFC_EDITOR_EDITOR_VARIABLES_H__

#include "src/variables.h"
#include "vm/public/value.h"

namespace afc::editor::editor_variables {

EdgeStruct<std::wstring>* StringStruct();
extern EdgeVariable<std::wstring>* const buffer_sort_order;

EdgeStruct<bool>* BoolStruct();
extern EdgeVariable<bool>* const multiple_buffers;

EdgeStruct<int>* IntStruct();
extern EdgeVariable<int>* const buffers_to_retain;
extern EdgeVariable<int>* const buffers_to_show;
extern EdgeVariable<int>* const numbers_column_padding;

EdgeStruct<double>* DoubleStruct();
extern EdgeVariable<double>* const volume;

}  // namespace afc::editor::editor_variables

#endif  // __AFC_EDITOR_EDITOR_VARIABLES_H__
