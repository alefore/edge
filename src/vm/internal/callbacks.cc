#include "../public/callbacks.h"

#include "../public/types.h"

namespace afc {
namespace vm {
const Type VMTypeMapper<void>::vmtype = types::Void{};
const Type VMTypeMapper<bool>::vmtype = types ::Bool{};
const Type VMTypeMapper<int>::vmtype = types::Int{};
const Type VMTypeMapper<double>::vmtype = types::Double{};
const Type VMTypeMapper<wstring>::vmtype = types::String{};
}  // namespace vm
}  // namespace afc
