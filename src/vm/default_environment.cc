#include "src/vm/default_environment.h"

#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/math/numbers.h"
#include "src/vm/callbacks.h"
#include "src/vm/container.h"
#include "src/vm/numbers.h"
#include "src/vm/string.h"
#include "src/vm/time.h"

namespace gc = afc::language::gc;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::PossibleError;
using afc::math::numbers::Number;

namespace afc::vm {
language::gc::Root<Environment> NewDefaultEnvironment(
    language::gc::Pool& pool) {
  gc::Root<Environment> environment = Environment::New(pool);
  Environment& environment_value = environment.ptr().value();
  RegisterStringType(pool, environment_value);
  RegisterNumberFunctions(pool, environment_value);
  RegisterTimeType(pool, environment_value);
  gc::Root<ObjectType> bool_type = ObjectType::New(pool, types::Bool{});
  bool_type.ptr()->AddField(
      L"tostring", NewCallback(pool, PurityType::kPure,
                               std::function<std::wstring(bool)>([](bool v) {
                                 return v ? L"true" : L"false";
                               }))
                       .ptr());
  environment_value.DefineType(bool_type.ptr());

  gc::Root<ObjectType> number_type = ObjectType::New(pool, types::Number{});
  number_type.ptr()->AddField(
      L"tostring",
      NewCallback(pool, PurityType::kPure,
                  /*std::function<std::wstring(Number)>*/ ([](Number value) {
                    return futures::Past(ToString(value, 5));
                  }))
          .ptr());
  environment_value.DefineType(number_type.ptr());

  environment_value.Define(
      L"Error",
      NewCallback(pool, PurityType::kPure, [](std::wstring description) {
        return futures::Past(PossibleError(Error(description)));
      }));

  container::Export<std::vector<int>>(pool, environment_value);
  container::Export<std::set<int>>(pool, environment_value);
  return environment;
}
}  // namespace afc::vm
