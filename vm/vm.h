#ifndef __AFC_VM_VM_H__
#define __AFC_VM_VM_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace vm {

using std::function;
using std::map;
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

  VMType(const Type& t) : type(t) {}

  static const VMType& Void();
  static const VMType& Bool();
  static const VMType& integer_type();
  static const VMType& String();

  static VMType ObjectType(afc::vm::ObjectType* type);
  static VMType ObjectType(const string& name);

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

class Expression {
 public:
  virtual ~Expression() {}
  virtual const VMType& type() = 0;
  virtual unique_ptr<Value> Evaluate(Environment* environment) = 0;
};

class ObjectType {
 public:
  ObjectType(const string& name)
      : name_(name),
        fields_(new map<string, unique_ptr<Value>>) {}

  const string& name() const { return name_; }

  void AddField(const string& name, unique_ptr<Value> field) {
    auto it = fields_->insert(make_pair(name, nullptr));
    it.first->second = std::move(field);
  }

  Value* LookupField(const string& name) {
    auto it = fields_->find(name);
    return it == fields_->end() ? nullptr : it->second.get();
  }

 private:
  string name_;
  map<string, unique_ptr<Value>>* fields_;
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

  static Environment* DefaultEnvironment();

  ObjectType* LookupType(const string& symbol);
  void DefineType(const string& name, unique_ptr<ObjectType> value);

  Value* Lookup(const string& symbol);
  void Define(const string& symbol, unique_ptr<Value> value);

 private:
  map<string, unique_ptr<Value>>* table_;
  map<string, unique_ptr<ObjectType>>* object_types_;
  Environment* parent_environment_;
};

class Evaluator {
 public:
  Evaluator(unique_ptr<Environment> environment);

  void Define(const string& name, unique_ptr<Value> value);

  void AppendInput(const string& str);

 private:
  unique_ptr<Environment> environment_;
  unique_ptr<void, function<void(void*)>> parser_;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VM_H__
