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

EdgeStruct<int>* IntStruct() {
  static EdgeStruct<int>* output = new EdgeStruct<int>();
  return output;
}

EdgeVariable<int>* const buffers_to_retain =
    IntStruct()
        ->Add()
        .Name(L"buffers_to_retain")
        .Key(L"B")
        .Description(
            L"Number of buffers to retain. If more than the given number of "
            L"buffers are open, the oldest buffers will be closed, as long as "
            L"they are clean.")
        .DefaultValue(24)
        .Build();
}  // namespace afc::editor::editor_variables
