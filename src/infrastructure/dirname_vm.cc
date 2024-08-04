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
  // TODO(2024-08-04): Change get_string to return a LazyString and remove this
  // explicit conversion.
  return Path::New(LazyString{value.get_string()});
}

/* static */ gc::Root<vm::Value> VMTypeMapper<Path>::New(gc::Pool& pool,
                                                         Path value) {
  // TODO(2024-01-05): Avoid explicit conversion to LazyString.
  return vm::Value::NewString(pool, LazyString{value.read()});
}

}  // namespace afc::vm
