#include "../public/value.h"

#include "../public/vm.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"
#include "src/vm/public/escape.h"

namespace afc::vm {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
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
  return New(pool, VMType::Void());
}

/* static */ gc::Root<Value> Value::NewBool(gc::Pool& pool, bool value) {
  gc::Root<Value> output = New(pool, VMType::Bool());
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewInt(gc::Pool& pool, int value) {
  gc::Root<Value> output = New(pool, VMType::Int());
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewDouble(gc::Pool& pool, double value) {
  gc::Root<Value> output = New(pool, VMType::Double());
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewString(gc::Pool& pool,
                                              std::wstring value) {
  gc::Root<Value> output = New(pool, VMType::String());
  output.ptr()->value_ = std::move(value);
  return output;
}

/* static */ gc::Root<Value> Value::NewSymbol(gc::Pool& pool,
                                              std::wstring value) {
  gc::Root<Value> output = New(pool, VMType::Symbol());
  output.ptr()->value_ = Symbol{.symbol_value = std::move(value)};
  return output;
}

/* static */ gc::Root<Value> Value::NewObject(
    gc::Pool& pool, VMTypeObjectTypeName name,
    NonNull<std::shared_ptr<void>> value, ExpandCallback expand_callback) {
  gc::Root<Value> output = New(pool, VMType::ObjectType(std::move(name)));
  output.ptr()->value_ = ObjectInstance{.value = std::move(value)};
  output.ptr()->expand_callback = std::move(expand_callback);
  return output;
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, PurityType purity_type, std::vector<VMType> arguments,
    Value::Callback callback, ExpandCallback expand_callback) {
  CHECK(callback != nullptr);
  gc::Root<Value> output =
      New(pool, VMType::Function(std::move(arguments), purity_type));
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

bool Value::IsVoid() const { return type == VMType::Void(); };
bool Value::IsInt() const { return type == VMType::Int(); };
bool Value::IsString() const { return type == VMType::String(); };
bool Value::IsSymbol() const { return type == VMType::Symbol(); };
bool Value::IsFunction() const { return type.type == VMType::Type::kFunction; };
bool Value::IsObject() const { return type.type == VMType::Type::kObject; };

bool Value::get_bool() const {
  CHECK_EQ(type, VMType::Bool());
  return std::get<bool>(value_);
}

int Value::get_int() const {
  CHECK_EQ(type, VMType::Int());
  return std::get<int>(value_);
}

double Value::get_double() const {
  CHECK_EQ(type, VMType::Double());
  return std::get<double>(value_);
}

// TODO(easy, 2022-06-10): Embrace LazyString.
const std::wstring& Value::get_string() const {
  CHECK_EQ(type, VMType::String());
  return std::get<std::wstring>(value_);
}

const std::wstring& Value::get_symbol() const {
  CHECK_EQ(type, VMType::Symbol());
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
  switch (type.type) {
    case VMType::Type::kInt:
      return Success(static_cast<double>(get_int()));
    case VMType::Type::kDouble:
      return Success(get_double());
    default:
      return Error(L"Unexpected value of type: " + type.ToString());
  }
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
  switch (value.type.type) {
    case VMType::Type::kInt:
      os << value.get_int();
      break;
    case VMType::Type::kString:
      os << EscapedString::FromString(NewLazyString(value.get_string()))
                .CppRepresentation();
      break;
    case VMType::Type::kBool:
      os << (value.get_bool() ? "true" : "false");
      break;
    case VMType::Type::kDouble:
      os << value.get_double();
      break;
    default:
      os << value.type.ToString();
  }
  return os;
}

namespace {
bool value_gc_tests_registration = tests::Register(
    L"ValueVMMemory",
    {{.name = L"Dependency", .callback = [] {
        using vm::Value;
        gc::Pool pool;
        // We use `nested_weak` to validate whether all the dependencies are
        // being preserved correctly.
        std::shared_ptr<bool> nested = std::make_shared<bool>();
        std::weak_ptr<bool> nested_weak = nested;

        Value::Callback callback = [&] {
          gc::Root<Value> parent = [&] {
            gc::Root<Value> child = Value::NewFunction(
                pool, PurityType::kPure, {VMType::Void()},
                [](std::vector<gc::Root<Value>>, Trampoline& t) {
                  return futures::Past(
                      EvaluationOutput::Return(Value::NewVoid(t.pool())));
                },
                [nested] {
                  return std::vector<
                      NonNull<std::shared_ptr<gc::ObjectMetadata>>>();
                });
            return Value::NewFunction(
                pool, PurityType::kPure, {VMType::Void()},
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
