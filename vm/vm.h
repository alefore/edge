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
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

class ObjectType;

struct VMType {
  enum Type {
    VM_VOID,
    VM_BOOLEAN,
    VM_INTEGER,
    VM_STRING,
    VM_SYMBOL,
    ENVIRONMENT,
    FUNCTION,
    OBJECT_TYPE,
  };

  VMType() : type(VM_VOID) {}
  VMType(const Type& t) : type(t) {}

  static const VMType& Void();
  static const VMType& Bool();
  static const VMType& integer_type();
  static const VMType& String();

  static VMType ObjectType(afc::vm::ObjectType* type);
  static VMType ObjectType(const string& name);

  string ToString() const;

  Type type;
  vector<VMType> type_arguments;
  string object_type;
};

bool operator==(const VMType& lhs, const VMType& rhs);

class Environment;

struct Value {
  Value(const VMType::Type& t) : type(t) {}
  Value(const VMType& t) : type(t) {}

  static unique_ptr<Value> Void();

  static unique_ptr<Value> NewBool(bool value) {
    unique_ptr<Value> output(new Value(VMType::Bool()));
    output->boolean = value;
    return std::move(output);
  }

  static unique_ptr<Value> NewInteger(int value) {
    unique_ptr<Value> output(new Value(VMType::integer_type()));
    output->integer = value;
    return std::move(output);
  }

  static unique_ptr<Value> NewString(const string& value) {
    unique_ptr<Value> output(new Value(VMType::String()));
    output->str = value;
    return std::move(output);
  }

  static unique_ptr<Value> NewObject(const string& name,
                                     const shared_ptr<void>& value) {
    unique_ptr<Value> output(new Value(VMType::ObjectType(name)));
    output->user_value = value;
    return std::move(output);
  }

  VMType type;

  bool boolean;
  int integer;
  string str;
  Environment* environment;
  function<unique_ptr<Value>(vector<unique_ptr<Value>>)> callback;
  shared_ptr<void> user_value;
};

class Evaluator;

template<typename A>
struct RecursiveHelper
{
  typedef function<pair<RecursiveHelper, A>(A)> type;
  RecursiveHelper(type f) : func(f) {}
  operator type() { return func; }
  type func;
};

class Expression {
 public:
  // We use a continuation trampoline: to evaluate an expression, we must pass
  // the continuation that wants to receive the value.  The expression will
  // return a pair with a continuation and a value, and the runner must feed the
  // value to the continuation and keep doing that on the returned value, until
  // it can verify that the original continuation has run.
  typedef RecursiveHelper<unique_ptr<Value>> Continuation;

  virtual ~Expression() {}
  virtual const VMType& type() = 0;
  virtual pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator, const Continuation& continuation) = 0;
};

class ObjectType {
 public:
  ObjectType(const VMType& type)
      : type_(type),
        fields_(new map<string, unique_ptr<Value>>) {}

  ObjectType(const string& type_name)
      : type_(VMType::ObjectType(type_name)),
        fields_(new map<string, unique_ptr<Value>>) {}

  const VMType& type() const { return type_; }
  string ToString() const { return type_.ToString(); }

  void AddField(const string& name, unique_ptr<Value> field) {
    auto it = fields_->insert(make_pair(name, nullptr));
    it.first->second = std::move(field);
  }

  Value* LookupField(const string& name) const {
    auto it = fields_->find(name);
    return it == fields_->end() ? nullptr : it->second.get();
  }

 private:
  VMType type_;
  map<string, unique_ptr<Value>>* fields_;
};

class Environment;

class Evaluator {
 public:
  typedef function<void(const string&)> ErrorHandler;

  Evaluator(Environment* environment, ErrorHandler error_handler);

  void PushEnvironment();
  void PopEnvironment();

  void EvaluateFile(const string& path);

  void AppendInput(const string& str);

  unique_ptr<Value> Evaluate(Expression* expression);
  unique_ptr<Value> Evaluate(Expression* expression, Environment* environment);

  const Expression::Continuation& return_continuation() const {
    return return_continuation_;
  }

  Environment* environment() const { return environment_; }

  const ErrorHandler& error_handler() const { return error_handler_; }

  const string last_token() const { return last_token_; }

 private:
  Environment* base_environment_;

  Expression::Continuation return_continuation_;
  Environment* environment_;

  ErrorHandler error_handler_;

  string last_token_;
  unique_ptr<void, function<void(void*)>> parser_;
};

class Environment {
 public:
  Environment()
      : table_(new map<string, unique_ptr<Value>>),
        object_types_(new map<string, unique_ptr<ObjectType>>),
        parent_environment_(nullptr) {}

  Environment(Environment* parent_environment)
      : table_(new map<string, unique_ptr<Value>>),
        object_types_(new map<string, unique_ptr<ObjectType>>),
        parent_environment_(parent_environment) {}

  Environment* parent_environment() const { return parent_environment_; }

  static Environment* DefaultEnvironment();

  // Returns a new Evaluator instance bound to the current environment.
  unique_ptr<Evaluator> NewEvaluator(Evaluator::ErrorHandler error_handler);

  const ObjectType* LookupObjectType(const string& symbol);
  const VMType* LookupType(const string& symbol);
  void DefineType(const string& name, unique_ptr<ObjectType> value);

  Value* Lookup(const string& symbol);
  void Define(const string& symbol, unique_ptr<Value> value);

 private:
  map<string, unique_ptr<Value>>* table_;
  map<string, unique_ptr<ObjectType>>* object_types_;
  Environment* parent_environment_;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
