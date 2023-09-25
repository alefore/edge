#include "src/infrastructure/dirname_vm.h"

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"

namespace gc = afc::language::gc;
using afc::infrastructure::Path;
using afc::language::ValueOrError;

namespace afc::vm {
const Type VMTypeMapper<Path>::vmtype = types::String{};

ValueOrError<Path> vm::VMTypeMapper<Path>::get(Value& value) {
  return Path::FromString(value.get_string());
}

/* static */ gc::Root<vm::Value> VMTypeMapper<Path>::New(gc::Pool& pool,
                                                         Path value) {
  return vm::Value::NewString(pool, value.read());
}

}  // namespace afc::vm
