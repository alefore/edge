#ifndef __AFC_VM_VM_H__
#define __AFC_VM_VM_H__

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace afc {
namespace vm {

using std::function;
using std::map;
using std::pair;
using std::string;
using std::unique_ptr;
using std::vector;

struct VMType {
  enum Type {
    VM_VOID,
    VM_BOOLEAN,
    VM_INTEGER,
    VM_STRING,
    VM_SYMBOL,
    ENVIRONMENT,
    FUNCTION,
  };

  VMType(const Type& t) : type(t) {}

  static const VMType& Void();
  static const VMType& integer_type();
  static const VMType& String();

  Type type;
  vector<VMType> type_arguments;
};

bool operator==(const VMType& lhs, const VMType& rhs);

class Environment;

struct Value {
  Value(const VMType::Type& t) : type(t) {}

  static unique_ptr<Value> Void();

  VMType type;

  bool boolean;
  int integer;
  string str;
  Environment* environment;
  function<unique_ptr<Value>(vector<unique_ptr<Value>>)> callback;
};

class Expression {
 public:
  virtual ~Expression() {}
  virtual const VMType& type() = 0;
  virtual unique_ptr<Value> Evaluate(Environment* environment) = 0;
};

struct Environment {
  Environment()
      : table(make_pair(map<string, unique_ptr<Value>>(), nullptr)) {}

  Value* Lookup(const string& symbol);
  void Define(const string& symbol, unique_ptr<Value> value);

  pair<map<string, unique_ptr<Value>>, Environment*> table;
};

class Evaluator {
 public:
  Evaluator();

  void Define(const string& name, unique_ptr<Value> value);

  void AppendInput(const string& str);

 private:
  unique_ptr<void, function<void(void*)>> parser_;
  Environment environment_;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
