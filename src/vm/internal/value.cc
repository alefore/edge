#include "../public/value.h"

#include "../public/vm.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"
#include "src/vm/public/escape.h"

namespace afc::vm {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::Success;
using language::ValueOrError;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;

/* static */ language::gc::Root<Value> Value::New(language::gc::Pool& pool,
                                                  const VMType& type) {
  return pool.NewRoot(
      MakeNonNullUnique<Value>(ConstructorAccessTag(), pool, type));
}

/* static */ gc::Root<Value> Value::NewVoid(gc::Pool& pool) {
  return New(pool, {.variant = types::Void{}});
}

/* static */ gc::Root<Value> Value::NewBool(gc::Pool& pool, bool value) {
  gc::Root<Value> output = New(pool, {.variant = types::Bool{}});
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewInt(gc::Pool& pool, int value) {
  gc::Root<Value> output = New(pool, {.variant = types::Int{}});
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewDouble(gc::Pool& pool, double value) {
  gc::Root<Value> output = New(pool, {.variant = types::Double{}});
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewString(gc::Pool& pool,
                                              std::wstring value) {
  gc::Root<Value> output = New(pool, {.variant = types::String{}});
  output.ptr()->value_ = std::move(value);
  return output;
}

/* static */ gc::Root<Value> Value::NewSymbol(gc::Pool& pool,
                                              std::wstring value) {
  gc::Root<Value> output = New(pool, {.variant = types::Symbol{}});
  output.ptr()->value_ = Symbol{.symbol_value = std::move(value)};
  return output;
}

/* static */ gc::Root<Value> Value::NewObject(
    gc::Pool& pool, VMTypeObjectTypeName name,
    NonNull<std::shared_ptr<void>> value, ExpandCallback expand_callback) {
  gc::Root<Value> output = New(
      pool,
      VMType{.variant = types::Object{.object_type_name = std::move(name)}});
  output.ptr()->value_ = ObjectInstance{.value = std::move(value)};
  output.ptr()->expand_callback = std::move(expand_callback);
  return output;
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, PurityType purity_type, std::vector<VMType> arguments,
    Value::Callback callback, ExpandCallback expand_callback) {
  CHECK(callback != nullptr);
  gc::Root<Value> output = New(
      pool,
      VMType{.variant = types::Function{.type_arguments = std::move(arguments),
                                        .function_purity = purity_type}});
  output.ptr()->value_ = std::move(callback);
  output.ptr()->expand_callback = std::move(expand_callback);
  return output;
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, PurityType purity_type, std::vector<VMType> arguments,
    std::function<gc::Root<Value>(std::vector<gc::Root<Value>>)> callback) {
  return NewFunction(
      pool, purity_type, arguments,
      [callback](std::vector<gc::Root<Value>> args, Trampoline&) {
        return futures::Past(
            Success(EvaluationOutput::New(callback(std::move(args)))));
      });
}

bool Value::IsVoid() const {
  return std::holds_alternative<types::Void>(type.variant);
}
bool Value::IsBool() const {
  return std::holds_alternative<types::Bool>(type.variant);
}
bool Value::IsInt() const {
  return std::holds_alternative<types::Int>(type.variant);
}
bool Value::IsDouble() const {
  return std::holds_alternative<types::Double>(type.variant);
}
bool Value::IsString() const {
  return std::holds_alternative<types::String>(type.variant);
}
bool Value::IsSymbol() const {
  return std::holds_alternative<types::Symbol>(type.variant);
}
bool Value::IsFunction() const {
  return std::holds_alternative<types::Function>(type.variant);
}
bool Value::IsObject() const {
  return std::holds_alternative<types::Object>(type.variant);
}

bool Value::get_bool() const {
  CHECK(IsBool());
  return std::get<bool>(value_);
}

int Value::get_int() const {
  CHECK(IsInt());
  return std::get<int>(value_);
}

double Value::get_double() const {
  CHECK(IsDouble());
  return std::get<double>(value_);
}

// TODO(easy, 2022-06-10): Embrace LazyString.
const std::wstring& Value::get_string() const {
  CHECK(IsString());
  return std::get<std::wstring>(value_);
}

const std::wstring& Value::get_symbol() const {
  CHECK(IsSymbol());
  return std::get<Symbol>(value_).symbol_value;
}

struct LockedDependencies {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> dependencies;
};

Value::Callback Value::LockCallback() {
  CHECK(IsFunction());
  gc::Root<LockedDependencies> dependencies =
      pool_.NewRoot(MakeNonNullUnique<LockedDependencies>(
          LockedDependencies{.dependencies = expand()}));
  Callback callback = std::get<Callback>(value_);
  CHECK(callback != nullptr);
  return [callback, dependencies](std::vector<gc::Root<Value>> args,
                                  Trampoline& trampoline) {
    return callback(std::move(args), trampoline);
  };
}

ValueOrError<double> Value::ToDouble() const {
  return std::visit(
      overload{[](const types::Void&) -> ValueOrError<double> {
                 return Error(L"Unable to convert to double: void");
               },
               [](const types::Bool&) -> ValueOrError<double> {
                 return Error(L"Unable to convert to double: bool");
               },
               [&](const types::Int&) -> ValueOrError<double> {
                 return Success(static_cast<double>(get_int()));
               },
               [&](const types::String&) -> ValueOrError<double> {
                 return Error(L"Unable to convert to double: string");
               },
               [&](const types::Symbol&) -> ValueOrError<double> {
                 return Error(L"Unable to convert to double: symbol");
               },
               [&](const types::Double&) -> ValueOrError<double> {
                 return Success(get_double());
               },
               [&](const types::Object& object) -> ValueOrError<double> {
                 return Error(L"Unable to convert to double: " +
                              object.object_type_name.read());
               },
               [](const types::Function&) -> ValueOrError<double> {
                 return Error(L"Unable to convert to double: function");
               }},
      type.variant);
}

std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
Value::expand() const {
  return expand_callback == nullptr
             ? std::vector<language::NonNull<
                   std::shared_ptr<language::gc::ObjectMetadata>>>()
             : expand_callback();
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  using ::operator<<;
  std::visit(
      overload{[&](const types::Void&) { os << L"<void>"; },
               [&](const types::Bool&) {
                 os << (value.get_bool() ? L"true" : L"false");
               },
               [&](const types::Int&) { os << value.get_int(); },
               [&](const types::String&) {
                 os << EscapedString::FromString(
                           NewLazyString(value.get_string()))
                           .CppRepresentation();
               },
               [&](const types::Symbol&) { os << ToString(value.type); },
               [&](const types::Double&) { os << value.get_double(); },
               [&](const types::Object&) { os << ToString(value.type); },
               [&](const types::Function&) { os << ToString(value.type); }},
      value.type.variant);
  return os;
}

namespace {
bool value_gc_tests_registration = tests::Register(
    L"ValueVMMemory",
    {{.name = L"Dependency", .callback = [] {
        using vm::Value;
        gc::Pool pool({});
        // We use `nested_weak` to validate whether all the dependencies are
        // being preserved correctly.
        std::shared_ptr<bool> nested = std::make_shared<bool>();
        std::weak_ptr<bool> nested_weak = nested;

        Value::Callback callback = [&] {
          gc::Root<Value> parent = [&] {
            gc::Root<Value> child = Value::NewFunction(
                pool, PurityType::kPure, {{.variant = types::Void{}}},
                [](std::vector<gc::Root<Value>>, Trampoline& t) {
                  return futures::Past(
                      EvaluationOutput::Return(Value::NewVoid(t.pool())));
                },
                [nested] {
                  return std::vector<
                      NonNull<std::shared_ptr<gc::ObjectMetadata>>>();
                });
            return Value::NewFunction(
                pool, PurityType::kPure, {{.variant = types::Void{}}},
                [child_ptr = child.ptr()](auto, Trampoline&) {
                  return futures::Past(Error(L"Some error."));
                },
                [child_frame = child.ptr().object_metadata()] {
                  return std::vector<
                      NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
                      {child_frame});
                });
          }();

          nested = nullptr;
          CHECK(nested_weak.lock() != nullptr);

          pool.FullCollect();
          CHECK(nested_weak.lock() != nullptr);

          return parent.ptr()->LockCallback();
        }();

        CHECK(nested_weak.lock() != nullptr);
        pool.FullCollect();

        CHECK(nested_weak.lock() != nullptr);

        callback = nullptr;
        pool.FullCollect();
        CHECK(nested_weak.lock() == nullptr);
      }}});
}
}  // namespace afc::vm
namespace afc::language::gc {
std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand(
    const afc::vm::LockedDependencies& dependencies) {
  return dependencies.dependencies;
}

std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand(
    const afc::vm::Value& value) {
  return value.expand();
}
}  // namespace afc::language::gc
