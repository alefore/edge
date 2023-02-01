#include "../public/callbacks.h"

#include "../public/types.h"

namespace afc {
namespace vm {
const VMType VMTypeMapper<void>::vmtype = {types::Void{}};
const VMType VMTypeMapper<bool>::vmtype = {types ::Bool{}};
const VMType VMTypeMapper<int>::vmtype = {types::Int{}};
const VMType VMTypeMapper<double>::vmtype = {types::Double{}};
const VMType VMTypeMapper<wstring>::vmtype = {types::String{}};
}  // namespace vm
}  // namespace afc
