#include "src/vm/value.h"

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::ToLazyString;
using afc::math::numbers::Number;

size_t constexpr kDefaultPrecision = 5ul;

namespace afc::vm {

/* static */ language::gc::Root<Value> Value::New(language::gc::Pool& pool,
                                                  const Type& type) {
  return pool.NewRoot(
      MakeNonNullUnique<Value>(ConstructorAccessTag(), pool, type));
}

/* static */ gc::Root<Value> Value::NewVoid(gc::Pool& pool) {
  return New(pool, types::Void{});
}

/* static */ gc::Root<Value> Value::NewBool(gc::Pool& pool, bool value) {
  gc::Root<Value> output = New(pool, types::Bool{});
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewNumber(gc::Pool& pool, Number value) {
  gc::Root<Value> output = New(pool, types::Number{});
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewString(gc::Pool& pool,
                                              LazyString value) {
  gc::Root<Value> output = New(pool, types::String{});
  output.ptr()->value_ = std::move(value);
  return output;
}

/* static */ gc::Root<Value> Value::NewSymbol(gc::Pool& pool,
                                              Identifier value) {
  gc::Root<Value> output = New(pool, types::Symbol{});
  output.ptr()->value_ = std::move(value);
  return output;
}

/* static */ gc::Root<Value> Value::NewObject(
    gc::Pool& pool, types::ObjectName name,
    NonNull<std::shared_ptr<void>> value, ExpandCallback expand_callback) {
  gc::Root<Value> output = New(pool, std::move(name));
  output.ptr()->value_ = ObjectInstance{.value = std::move(value)};
  output.ptr()->expand_callback_ = std::move(expand_callback);
  return output;
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, PurityType purity_type, Type type_output,
    std::vector<Type> type_inputs, Value::Callback callback,
    ExpandCallback expand_callback) {
  CHECK(callback != nullptr);
  gc::Root<Value> output =
      New(pool, types::Function{.output = std::move(type_output),
                                .inputs = std::move(type_inputs),
                                .function_purity = purity_type});
  output.ptr()->value_ = std::move(callback);
  output.ptr()->expand_callback_ = std::move(expand_callback);
  return output;
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, PurityType purity_type, Type output,
    std::vector<Type> inputs,
    std::function<gc::Root<Value>(std::vector<gc::Root<Value>>)> callback) {
  return NewFunction(
      pool, purity_type, std::move(output), std::move(inputs),
      [callback](std::vector<gc::Root<Value>> args, Trampoline&) {
        return futures::Past(Success(callback(std::move(args))));
      });
}

const Type& Value::type() const { return type_; }

bool Value::IsVoid() const {
  return std::holds_alternative<types::Void>(type_);
}
bool Value::IsBool() const {
  return std::holds_alternative<types::Bool>(type_);
}
bool Value::IsNumber() const {
  return std::holds_alternative<types::Number>(type_);
}
bool Value::IsString() const {
  return std::holds_alternative<types::String>(type_);
}
bool Value::IsSymbol() const {
  return std::holds_alternative<types::Symbol>(type_);
}
bool Value::IsFunction() const {
  return std::holds_alternative<types::Function>(type_);
}
bool Value::IsObject() const {
  return std::holds_alternative<types::ObjectName>(type_);
}

bool Value::get_bool() const {
  CHECK(IsBool());
  return std::get<bool>(value_);
}

language::ValueOrError<int32_t> Value::get_int32() const {
  return get_number().ToInt32();
}

language::ValueOrError<int64_t> Value::get_int() const {
  return get_number().ToInt64();
}

const math::numbers::Number& Value::get_number() const {
  CHECK(IsNumber());
  return std::get<Number>(value_);
}

const LazyString& Value::get_string() const {
  CHECK(IsString());
  return std::get<LazyString>(value_);
}

const Identifier& Value::get_symbol() const {
  CHECK(IsSymbol());
  return std::get<Identifier>(value_);
}

futures::ValueOrError<language::gc::Root<Value>> Value::RunFunction(
    std::vector<language::gc::Root<Value>> arguments, Trampoline& trampoline) {
  return std::get<Callback>(value_)(std::move(arguments), trampoline);
}

ValueOrError<double> Value::ToDouble() const {
  return std::visit(
      overload{
          [](const types::Void&) -> ValueOrError<double> {
            return Error{LazyString{L"Unable to convert to double: void"}};
          },
          [](const types::Bool&) -> ValueOrError<double> {
            return Error{LazyString{L"Unable to convert to double: bool"}};
          },
          [&](const types::Number&) -> ValueOrError<double> {
            return get_number().ToDouble();
          },
          [&](const types::String&) -> ValueOrError<double> {
            return Error{LazyString{L"Unable to convert to double: string"}};
          },
          [&](const types::Symbol&) -> ValueOrError<double> {
            return Error{LazyString{L"Unable to convert to double: symbol"}};
          },
          [&](const types::ObjectName& object) -> ValueOrError<double> {
            return Error{LazyString{L"Unable to convert to double: "} +
                         ToSingleLine(object)};
          },
          [](const types::Function&) -> ValueOrError<double> {
            return Error{LazyString{L"Unable to convert to double: function"}};
          }},
      type_);
}

std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
Value::Expand() const {
  return expand_callback_ == nullptr
             ? std::vector<language::NonNull<
                   std::shared_ptr<language::gc::ObjectMetadata>>>()
             : expand_callback_();
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  using ::operator<<;
  std::visit(
      overload{
          [&](const types::Void&) { os << L"<void>"; },
          [&](const types::Bool&) {
            os << (value.get_bool() ? L"true" : L"false");
          },
          [&](const types::Number&) {
            os << value.get_number().ToString(kDefaultPrecision);
          },
          [&](const types::String&) {
            os << EscapedString::FromString(LazyString{value.get_string()})
                      .CppRepresentation()
                      .read()
                      .read();
          },
          [&](const types::Symbol&) { os << ToSingleLine(value.type()); },
          [&](const types::ObjectName&) { os << ToSingleLine(value.type()); },
          [&](const types::Function&) { os << ToSingleLine(value.type()); }},
      value.type());
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

        std::optional<gc::Root<Value>> callback = std::invoke([&] {
          gc::Root<Value> parent = std::invoke([&] {
            gc::Root<Value> child = Value::NewFunction(
                pool, kPurityTypePure, types::Void{}, {},
                [&pool](std::vector<gc::Root<Value>>, Trampoline&) {
                  return futures::Past(Value::NewVoid(pool));
                },
                [nested] {
                  return std::vector<
                      NonNull<std::shared_ptr<gc::ObjectMetadata>>>();
                });
            return Value::NewFunction(
                pool, kPurityTypePure, types::Void{}, {},
                [child_ptr = child.ptr()](std::vector<gc::Root<Value>>,
                                          Trampoline&) {
                  return futures::Past(Error{LazyString{L"Some error."}});
                },
                [child_frame = child.ptr().object_metadata()] {
                  return std::vector<
                      NonNull<std::shared_ptr<gc::ObjectMetadata>>>(
                      {child_frame});
                });
          });

          nested = nullptr;
          CHECK(nested_weak.lock() != nullptr);

          pool.FullCollect();
          CHECK(nested_weak.lock() != nullptr);

          return parent;
        });

        CHECK(nested_weak.lock() != nullptr);
        pool.FullCollect();

        CHECK(nested_weak.lock() != nullptr);

        callback = std::nullopt;
        pool.FullCollect();
        CHECK(nested_weak.lock() == nullptr);
      }}});
}  // namespace
}  // namespace afc::vm
