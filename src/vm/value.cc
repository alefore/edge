#include "src/vm/value.h"

#include "src/language/gc_view.h"
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

Value::Value(ConstructorAccessTag, const Type& type, ValueVariant value_variant)
    : Value(ConstructorAccessTag{}, type, std::move(value_variant),
            ExpandCallback{}) {}

Value::Value(ConstructorAccessTag, const Type& type, ValueVariant value_variant,
             ExpandCallback expand_callback)
    : type_(type),
      value_(std::move(value_variant)),
      expand_callback_(std::move(expand_callback)) {}

/* static */ gc::Root<Value> Value::NewVoid(gc::Pool& pool) {
  return pool.NewRoot(MakeNonNullUnique<Value>(ConstructorAccessTag(),
                                               types::Void{}, ValueVariant()));
}

/* static */ gc::Root<Value> Value::NewBool(gc::Pool& pool, bool value) {
  return pool.NewRoot(
      MakeNonNullUnique<Value>(ConstructorAccessTag(), types::Bool{}, value));
}

/* static */ gc::Root<Value> Value::NewNumber(gc::Pool& pool, Number value) {
  return pool.NewRoot(
      MakeNonNullUnique<Value>(ConstructorAccessTag(), types::Number{}, value));
}

/* static */ gc::Root<Value> Value::NewString(gc::Pool& pool,
                                              LazyString value) {
  return pool.NewRoot(MakeNonNullUnique<Value>(
      ConstructorAccessTag(), types::String{}, std::move(value)));
}

/* static */ gc::Root<Value> Value::NewSymbol(gc::Pool& pool,
                                              Identifier value) {
  return pool.NewRoot(MakeNonNullUnique<Value>(
      ConstructorAccessTag(), types::Symbol{}, std::move(value)));
}

/* static */ gc::Root<Value> Value::NewObject(
    gc::Pool& pool, types::ObjectName name,
    NonNull<std::shared_ptr<void>> value, ExpandCallback expand_callback) {
  return pool.NewRoot(MakeNonNullUnique<Value>(
      ConstructorAccessTag(), std::move(name),
      ObjectInstance{.value = std::move(value)}, std::move(expand_callback)));
}

/* static */ language::gc::Root<Value> Value::NewFunction(
    language::gc::Pool& pool, PurityType purity_type, Type type_output,
    std::vector<Type> inputs, Callback callback) {
  return NewFunction(
      pool, purity_type, type_output, std::move(inputs),
      Value::CallbackWithDependencies::New(pool, std::move(callback), {})
          .ptr());
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, PurityType purity_type, Type type_output,
    std::vector<Type> type_inputs,
    gc::Ptr<Value::CallbackWithDependencies> callback) {
  return pool.NewRoot(
      MakeNonNullUnique<Value>(ConstructorAccessTag(),
                               types::Function{.output = std::move(type_output),
                                               .inputs = std::move(type_inputs),
                                               .function_purity = purity_type},
                               std::move(callback)));
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, PurityType purity_type, Type output,
    std::vector<Type> inputs,
    std::function<gc::Root<Value>(std::vector<gc::Root<Value>>)> callback) {
  return NewFunction(
      pool, purity_type, std::move(output), std::move(inputs),
      [callback](std::vector<gc::Root<Value>> args, Trampoline&) {
        return callback(std::move(args));
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
  if (!std::holds_alternative<types::Function>(type_)) return false;
  CHECK(std::holds_alternative<gc::Ptr<CallbackWithDependencies>>(value_));
  return true;
}
bool Value::IsObject() const {
  return std::holds_alternative<types::ObjectName>(type_);
}
bool Value::IsObjectType(const types::ObjectName& expected_type) const {
  auto* object_name = std::get_if<types::ObjectName>(&type_);
  return object_name && *object_name == expected_type;
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
  return std::get<gc::Ptr<CallbackWithDependencies>>(value_)->value()(
      std::move(arguments), trampoline);
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
  if (const gc::Ptr<CallbackWithDependencies>* value =
          std::get_if<gc::Ptr<CallbackWithDependencies>>(&value_);
      value) {
    CHECK(!expand_callback_);
    return {value->object_metadata()};
  }
  return expand_callback_
             ? expand_callback_()
             : std::vector<language::NonNull<
                   std::shared_ptr<language::gc::ObjectMetadata>>>{};
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
struct Node {
  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const {
    return {};
  }
};

bool value_gc_tests_registration = tests::Register(
    L"ValueVMMemory",
    {{.name = L"CallbackWithDependency",
      .callback =
          [] {
            using vm::Value;
            gc::Pool pool({});
            std::optional<gc::Root<Node>> node =
                pool.NewRoot(MakeNonNullUnique<Node>());
            // We use `node_weak` to validate whether all the dependencies are
            // preserved correctly.
            gc::WeakPtr<Node> node_weak = node->ptr().ToWeakPtr();

            std::optional<gc::Root<Value>> callback = std::invoke([&] {
              gc::Root<Value> parent = std::invoke([&] {
                gc::Root<Value> child = Value::NewFunction(
                    pool, kPurityTypePure, types::Void{}, {},
                    Value::CallbackWithDependencies::New(
                        pool,
                        [&pool](std::vector<gc::Root<Value>>, Trampoline&) {
                          return Value::NewVoid(pool);
                        },
                        std::vector{node->ptr().object_metadata()})
                        .ptr());
                return Value::NewFunction(
                    pool, kPurityTypePure, types::Void{}, {},
                    Value::CallbackWithDependencies::New(
                        child.pool(),
                        [child_ptr = child.ptr()](std::vector<gc::Root<Value>>,
                                                  Trampoline&) {
                          return Error(L"Some error.");
                        },
                        {child.ptr().object_metadata()})
                        .ptr());
              });

              node = std::nullopt;
              CHECK(node_weak.Lock());

              pool.FullCollect();
              pool.BlockUntilDone();
              CHECK(node_weak.Lock());

              return parent;
            });

            CHECK(node_weak.Lock());
            pool.FullCollect();
            pool.BlockUntilDone();

            CHECK(node_weak.Lock());

            callback = std::nullopt;
            pool.FullCollect();
            pool.BlockUntilDone();
            CHECK(!node_weak.Lock());
          }},
     {.name = L"ObjectWithDependency", .callback = [] {
        using vm::Value;
        gc::Pool pool({});
        std::optional<gc::Root<Node>> node =
            pool.NewRoot(MakeNonNullUnique<Node>());

        // We use `node_weak` to validate whether all the dependencies are
        // preserved correctly.
        gc::WeakPtr<Node> node_weak = node->ptr().ToWeakPtr();

        std::optional<gc::Root<Value>> object = std::invoke([&] {
          gc::Root<Value> intermediate = Value::NewFunction(
              pool, kPurityTypePure, vm::types::Void{}, {},
              Value::CallbackWithDependencies::New(
                  pool,
                  [&pool](std::vector<gc::Root<Value>>, Trampoline&) {
                    return Value::NewVoid(pool);
                  },
                  {node->ptr().object_metadata()})
                  .ptr());
          node = std::nullopt;

          return Value::NewObject(
              pool, types::ObjectName{IDENTIFIER_CONSTANT(L"TestNode")},
              NonNull<std::unique_ptr<Node>>(),
              [intermediate_ptr = intermediate.ptr()]
              -> std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> {
                return {intermediate_ptr.object_metadata()};
              });
        });

        CHECK(node_weak.Lock());

        pool.FullCollect();
        pool.BlockUntilDone();
        CHECK(node_weak.Lock());

        object = std::nullopt;

        pool.FullCollect();
        pool.BlockUntilDone();
        CHECK(!node_weak.Lock());
      }}});

}  // namespace
}  // namespace afc::vm
