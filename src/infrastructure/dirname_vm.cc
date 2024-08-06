#include "src/infrastructure/dirname_vm.h"

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"

namespace gc = afc::language::gc;
using afc::infrastructure::Path;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
const Type VMTypeMapper<Path>::vmtype = types::String{};

ValueOrError<Path> vm::VMTypeMapper<Path>::get(Value& value) {
  return Path::New(value.get_string());
}

/* static */ gc::Root<vm::Value> VMTypeMapper<Path>::New(gc::Pool& pool,
                                                         Path value) {
  return vm::Value::NewString(pool, value.read());
}

}  // namespace afc::vm
