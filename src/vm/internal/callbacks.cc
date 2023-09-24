#include "../public/callbacks.h"

#include "../public/types.h"

using afc::language::numbers::Number;
namespace afc::vm {
const Type VMTypeMapper<void>::vmtype = types::Void{};
const Type VMTypeMapper<bool>::vmtype = types ::Bool{};
const Type VMTypeMapper<Number>::vmtype = types::Number{};
const Type VMTypeMapper<int>::vmtype = types::Number{};
const Type VMTypeMapper<size_t>::vmtype = types::Number{};
const Type VMTypeMapper<double>::vmtype = types::Number{};
const Type VMTypeMapper<wstring>::vmtype = types::String{};
}  // namespace afc::vm
