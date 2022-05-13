#ifndef __AFC_VM_PUBLIC_VALUE_H__
#define __AFC_VM_PUBLIC_VALUE_H__

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
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
 private:
  class ConstructorAccessTag {
    ConstructorAccessTag(){};
    friend Value;
  };

 public:
  using Ptr = std::unique_ptr<Value>;

  explicit Value(ConstructorAccessTag, const VMType& t) : type(t) {}

  using Callback = std::function<futures::ValueOrError<EvaluationOutput>(
      std::vector<language::gc::Root<Value>>, Trampoline&)>;
  using DependenciesCallback =
      std::function<void(std::vector<language::NonNull<
                             std::shared_ptr<language::gc::ControlFrame>>>&)>;

  static language::gc::Root<Value> New(language::gc::Pool& pool, const VMType&);
  static language::gc::Root<Value> NewVoid(language::gc::Pool& pool);
  static language::gc::Root<Value> NewBool(language::gc::Pool& pool,
                                           bool value);
  static language::gc::Root<Value> NewInt(language::gc::Pool& pool, int value);
  static language::gc::Root<Value> NewDouble(language::gc::Pool& pool,
                                             double value);
  static language::gc::Root<Value> NewString(language::gc::Pool& pool,
                                             wstring value);
  static language::gc::Root<Value> NewSymbol(language::gc::Pool& pool,
                                             wstring value);
  static language::gc::Root<Value> NewObject(language::gc::Pool& pool,
                                             std::wstring name,
                                             std::shared_ptr<void> value);
  static language::gc::Root<Value> NewFunction(
      language::gc::Pool& pool, std::vector<VMType> arguments,
      Callback callback,
      DependenciesCallback dependencies_callback =
          [](std::vector<language::NonNull<
                 std::shared_ptr<language::gc::ControlFrame>>>&) {});

  // Convenience wrapper.
  static language::gc::Root<Value> NewFunction(
      language::gc::Pool& pool, std::vector<VMType> arguments,
      std::function<
          language::gc::Root<Value>(std::vector<language::gc::Root<Value>>)>
          callback);

  bool IsVoid() const;
  bool IsInt() const;
  bool IsString() const;
  bool IsSymbol() const;
  bool IsFunction() const;
  bool IsObject() const;

  VMType type;

  bool get_bool() const;
  int get_int() const;
  double get_double() const;
  const std::wstring& get_string() const;
  const std::wstring& get_symbol() const;
  // TODO(easy, 2022-05-13): Make these private; provide accessors that check
  // type.
  std::shared_ptr<void> user_value;

  Callback LockCallback();

  // This is similar to `get_double`, but can deal with type conversion from
  // integer.
  language::ValueOrError<double> ToDouble() const;

 private:
  struct Symbol {
    std::wstring symbol_value;
  };
  std::variant<bool, int, double, std::wstring, Symbol> value_;

  Callback callback;
  DependenciesCallback dependencies_callback;
};

std::ostream& operator<<(std::ostream& os, const Value& value);

}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_VALUE_H__
