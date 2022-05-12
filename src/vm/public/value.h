#ifndef __AFC_VM_PUBLIC_VALUE_H__
#define __AFC_VM_PUBLIC_VALUE_H__

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/vm/public/types.h"
#include "src/vm/public/vm.h"

namespace afc::vm {

using std::function;
using std::shared_ptr;
using std::string;
using std::vector;

class Trampoline;
struct EvaluationOutput;

std::wstring CppEscapeString(std::wstring input);
std::optional<std::wstring> CppUnescapeString(std::wstring input);

struct Value {
  using Ptr = std::unique_ptr<Value>;

  explicit Value(const VMType::Type& t) : type(t) {}
  explicit Value(const VMType& t) : type(t) {}

  using Callback = std::function<futures::ValueOrError<EvaluationOutput>(
      std::vector<language::NonNull<Ptr>>, Trampoline&)>;
  using DependenciesCallback =
      std::function<void(std::vector<language::NonNull<
                             std::shared_ptr<language::gc::ControlFrame>>>&)>;

  static language::NonNull<std::unique_ptr<Value>> NewVoid(
      language::gc::Pool& pool);
  static language::NonNull<std::unique_ptr<Value>> NewBool(
      language::gc::Pool& pool, bool value);
  static language::NonNull<std::unique_ptr<Value>> NewInteger(
      language::gc::Pool& pool, int value);
  static language::NonNull<std::unique_ptr<Value>> NewDouble(
      language::gc::Pool& pool, double value);
  static language::NonNull<std::unique_ptr<Value>> NewString(
      language::gc::Pool& pool, wstring value);
  static language::NonNull<std::unique_ptr<Value>> NewObject(
      language::gc::Pool& pool, std::wstring name, std::shared_ptr<void> value);
  static language::NonNull<std::unique_ptr<Value>> NewFunction(
      language::gc::Pool& pool, std::vector<VMType> arguments,
      Callback callback);
  // Convenience wrapper.
  static language::NonNull<std::unique_ptr<Value>> NewFunction(
      language::gc::Pool& pool, std::vector<VMType> arguments,
      std::function<language::NonNull<Ptr>(std::vector<language::NonNull<Ptr>>)>
          callback);

  bool IsVoid() const { return type.type == VMType::VM_VOID; };
  bool IsBool() const { return type.type == VMType::VM_BOOLEAN; };
  bool IsInteger() const { return type.type == VMType::VM_INTEGER; };
  bool IsDouble() const { return type.type == VMType::VM_DOUBLE; };
  bool IsString() const { return type.type == VMType::VM_STRING; };
  bool IsSymbol() const { return type.type == VMType::VM_SYMBOL; };
  bool IsFunction() const { return type.type == VMType::FUNCTION; };
  bool IsObject() const { return type.type == VMType::OBJECT_TYPE; };

  VMType type;

  bool boolean;
  int integer;
  double double_value;
  std::wstring str;
  std::shared_ptr<void> user_value;

  Callback LockCallback();

 private:
  Callback callback;
  DependenciesCallback dependencies_callback;
};

std::ostream& operator<<(std::ostream& os, const Value& value);

}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_VALUE_H__
