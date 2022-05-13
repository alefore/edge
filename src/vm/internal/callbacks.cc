#include "../public/callbacks.h"

#include "../public/types.h"

namespace afc {
namespace vm {

const VMType VMTypeMapper<void>::vmtype = VMType::Void();
const VMType VMTypeMapper<bool>::vmtype = VMType::Bool();
const VMType VMTypeMapper<int>::vmtype = VMType::Int();
const VMType VMTypeMapper<double>::vmtype = VMType::Double();
const VMType VMTypeMapper<wstring>::vmtype = VMType::String();

}  // namespace vm
}  // namespace afc
