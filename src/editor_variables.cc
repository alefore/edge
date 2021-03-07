#include "src/editor_variables.h"

namespace afc::editor::editor_variables {

EdgeStruct<wstring>* StringStruct() {
  static EdgeStruct<wstring>* output = new EdgeStruct<wstring>();
  return output;
}

EdgeVariable<wstring>* const buffer_sort_order =
    StringStruct()
        ->Add()
        .Name(L"buffer_sort_order")
        .Description(L"Set the sort order for the buffers.")
        .DefaultValue(L"last_visit")
        .Build();

EdgeStruct<bool>* BoolStruct() {
  static EdgeStruct<bool>* output = new EdgeStruct<bool>();
  return output;
}

EdgeVariable<bool>* const multiple_buffers =
    BoolStruct()
        ->Add()
        .Name(L"multiple_buffers")
        .Key(L"b")
        .Description(L"Should all visible buffers be considered as active?")
        .Build();

EdgeVariable<bool>* const focus =
    BoolStruct()
        ->Add()
        .Name(L"focus")
        .Key(L"F")
        .Description(L"Should we focus on a single file?")
        .Build();
}  // namespace afc::editor::editor_variables
