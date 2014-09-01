#include "vm.h"

#include <cassert>
#include <iostream>
#include <utility>

#include "string.h"

namespace afc {
namespace vm {

using std::cerr;
using std::pair;

bool operator==(const VMType& lhs, const VMType& rhs) {
  return lhs.type == rhs.type && lhs.type_arguments == rhs.type_arguments;
}

/* static */ const VMType& VMType::Void() {
  static VMType type(VMType::VM_VOID);
  return type;
}

/* static */ const VMType& VMType::Bool() {
  static VMType type(VMType::VM_BOOLEAN);
  return type;
}

/* static */ const VMType& VMType::String() {
  static VMType type(VMType::VM_STRING);
  return type;
}

/* static */ VMType VMType::ObjectType(afc::vm::ObjectType* type) {
  return ObjectType(type->type().object_type);
}

/* static */ VMType VMType::ObjectType(const string& name) {
  VMType output(VMType::OBJECT_TYPE);
  output.object_type = name;
  return output;
}

/* static */ const VMType& VMType::integer_type() {
  static VMType type(VMType::VM_INTEGER);
  return type;
}

/* static */ unique_ptr<Value> Value::Void() {
  return unique_ptr<Value>(new Value(VMType::VM_VOID));
}

class AssignExpression : public Expression {
 public:
  AssignExpression(const string& symbol, unique_ptr<Expression> value)
      : symbol_(symbol), value_(std::move(value)) {}

  const VMType& type() { return value_->type(); }

  unique_ptr<Value> Evaluate(Environment* environment) {
    auto value = value_->Evaluate(environment);
    environment->Define(symbol_, unique_ptr<Value>(new Value(*value.get())));
    return value;
  }

 private:
  const string symbol_;
  unique_ptr<Expression> value_;
};

class ConstantExpression : public Expression {
 public:
  ConstantExpression(unique_ptr<Value> value)
      : value_(std::move(value)) {}

  const VMType& type() { return value_->type; }

  unique_ptr<Value> Evaluate(Environment* environment) {
    unique_ptr<Value> value(new Value(value_->type.type));
    *value = *value_;
    return value;
  }

 private:
  unique_ptr<Value> value_;
};

class BinaryOperator : public Expression {
 public:
  BinaryOperator(unique_ptr<Expression> a, unique_ptr<Expression> b,
                 const VMType type,
                 function<void(const Value&, const Value&, Value*)> callback)
      : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {}

  const VMType& type() { return type_; }

  unique_ptr<Value> Evaluate(Environment* environment) {
    unique_ptr<Value> output = Value::Void();
    output->type = type_;
    auto a = a_->Evaluate(environment);
    auto b = b_->Evaluate(environment);
    operator_(*a, *b, output.get());
    return std::move(output);
  }

 private:
  unique_ptr<Expression> a_;
  unique_ptr<Expression> b_;
  VMType type_;
  std::function<void(const Value&, const Value&, Value*)> operator_;
};

class FunctionCall : public Expression {
 public:
  FunctionCall(unique_ptr<Expression> func,
               vector<unique_ptr<Expression>>* args)
      : func_(std::move(func)), args_(args) {
    assert(func_ != nullptr);
    assert(args_ != nullptr);
  }

  FunctionCall(unique_ptr<Expression> func,
               unique_ptr<Expression> object,
               vector<unique_ptr<Expression>>* args)
      : func_(std::move(func)),
        object_(std::move(object)),
        args_(args) {
    assert(func_ != nullptr);
    assert(object_ != nullptr);
    assert(args_ != nullptr);
  }

  const VMType& type() {
    return func_->type().type_arguments[0];
  }

  unique_ptr<Value> Evaluate(Environment* environment) {
    auto func = std::move(func_->Evaluate(environment));
    assert(func != nullptr);
    vector<unique_ptr<Value>> values;
    if (object_ != nullptr) {
      values.push_back(object_->Evaluate(environment));
    }
    for (const auto& arg : *args_) {
      values.push_back(arg->Evaluate(environment));
    }
    return std::move(func->callback(std::move(values)));
  }

 private:
  unique_ptr<Expression> func_;
  unique_ptr<Expression> object_;
  unique_ptr<vector<unique_ptr<Expression>>> args_;
};

/* static */ Environment* Environment::DefaultEnvironment() {
  static Environment* environment = nullptr;
  if (environment != nullptr) { return environment; }
  environment = new Environment();
  RegisterStringType(environment);

  environment->DefineType(
      "bool", unique_ptr<ObjectType>(new ObjectType(VMType::Bool())));

  environment->DefineType(
      "int", unique_ptr<ObjectType>(new ObjectType(VMType::integer_type())));

  return environment;
}

const ObjectType* Environment::LookupObjectType(const string& symbol) {
  auto it = object_types_->find(symbol);
  if (it != object_types_->end()) {
    return it->second.get();
  }
  if (parent_environment_ != nullptr) {
    return parent_environment_->LookupObjectType(symbol);
  }
  return nullptr;
}

const VMType* Environment::LookupType(const string& symbol) {
  if (symbol == "void") {
    return &VMType::Void();
  } else if (symbol == "bool") {
    return &VMType::Bool();
  } else if (symbol == "int") {
    return &VMType::integer_type();
  } else if (symbol == "string") {
    return &VMType::String();
  }

  auto object_type = LookupObjectType(symbol);
  return object_type == nullptr ? nullptr : &object_type->type();
}

void Environment::DefineType(
    const string& name, unique_ptr<ObjectType> value) {
  auto it = object_types_->insert(make_pair(name, nullptr));
  it.first->second = std::move(value);
}

Value* Environment::Lookup(const string& symbol) {
  auto it = table_->find(symbol);
  if (it != table_->end()) {
    return it->second.get();
  }
  if (parent_environment_ != nullptr) {
    return parent_environment_->Lookup(symbol);
  }

  return nullptr;
}

void Environment::Define(const string& symbol, unique_ptr<Value> value) {
  auto it = table_->insert(make_pair(symbol, nullptr));
  it.first->second = std::move(value);
}

void ValueDestructor(Value* value) {
  delete value;
}

#include "cpp.h"
#include "cpp.c"

Evaluator::Evaluator(unique_ptr<Environment> environment)
    : environment_(std::move(environment)),
      parser_(
          CppAlloc(malloc),
          [this](void* parser) {
            Cpp(parser, 0, nullptr, environment_.get());
            CppFree(parser, free);
          }) {}

void Evaluator::Define(const string& name, unique_ptr<Value> value) {
  environment_->Define(name, std::move(value));
}

void Evaluator::AppendInput(const string& str) {
  size_t pos = 0;
  int token;
  while (pos < str.size()) {
    Value* input = nullptr;
    switch (str.at(pos)) {
      case '/':
        if (pos + 1 < str.size() && str.at(pos + 1) == '/') {
          pos = str.size();
        } else {
          token = DIVIDE;
          pos++;
        }
        break;

      case '=':
        pos++;
        if (pos < str.size() && str.at(pos) == '=') {
          pos++;
          token = EQUALS;
          break;
        }
        token = EQ;
        break;

      case '&':
        pos++;
        if (pos < str.size() && str.at(pos) == '&') {
          pos++;
          token = AND;
          break;
        }
        //token = AMPERSAND;
        break;

      case '|':
        pos++;
        if (pos < str.size() && str.at(pos) == '|') {
          pos++;
          token = OR;
          break;
        }
        //token = PIPE;
        break;

      case '<':
        token = LESS_THAN;
        pos++;
        break;

      case '>':
        token = GREATER_THAN;
        pos++;
        break;

      case ';':
        token = SEMICOLON;
        pos++;
        break;

      case '.':
        token = DOT;
        pos++;
        break;

      case ',':
        token = COMMA;
        pos++;
        break;

      case '+':
        token = PLUS;
        pos++;
        break;

      case '-':
        token = MINUS;
        pos++;
        break;

      case '*':
        token = TIMES;
        pos++;
        break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        token = INTEGER;
        input = new Value(VMType::VM_INTEGER);
        input->integer = 0;
        while (pos < str.size() && isdigit(str.at(pos))) {
          input->integer = input->integer * 10 + str.at(pos) - '0';
          pos++;
        }
        break;

      case '"':
        {
          token = STRING;
          input = new Value(VMType::VM_STRING);
          pos++;
          input->str = "";
          for (; pos < str.size(); pos++) {
            if (str.at(pos) == '"') {
              break;
            }
            if (str.at(pos) != '\\') {
              input->str.push_back(str.at(pos));;
              continue;
            }
            pos++;
            if (pos >= str.size()) { continue; }
            switch (str.at(pos)) {
              case 'n':
                input->str.push_back('\n');
                break;
              case 't':
                input->str.push_back('\t');
                break;
              case '"':
                input->str.push_back('"');
                break;
              default:
                input->str.push_back(str.at(pos));
            }
          }
          pos++;
        }
        break;

      case ' ':
      case '\n':
      case '\t':
        pos++;
        continue;

      case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
      case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
      case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
      case 'V': case 'W': case 'X': case 'Y': case 'Z':
      case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
      case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
      case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
      case 'v': case 'w': case 'x': case 'y': case 'z': case '_':
        {
          size_t start = pos;
          while (pos < str.size()
                 && (isalnum(str.at(pos)) || str.at(pos) == '_')) {
            pos++;
          }
          string symbol = str.substr(start, pos - start);
          if (symbol == "true") {
            token = BOOL;
            input = new Value(VMType::VM_BOOLEAN);
            input->boolean = true;
          } else if (symbol == "false") {
            token = BOOL;
            input = new Value(VMType::VM_BOOLEAN);
            input->boolean = false;
          } else if (symbol == "while") {
            token = WHILE;
          } else if (symbol == "if") {
            token = IF;
          } else if (symbol == "else") {
            token = ELSE;
          } else {
            token = SYMBOL;
            input = new Value(VMType::VM_SYMBOL);
            input->str = symbol;
          }
        }
        break;

      case '(':
        token = LPAREN;
        pos++;
        break;

      case ')':
        token = RPAREN;
        pos++;
        break;

      case '{':
        token = LBRACKET;
        pos++;
        break;

      case '}':
        token = RBRACKET;
        pos++;
        break;

      default:
        cerr << "Unhandled character at position " << pos << ": " << str;
        exit(54);
    }
    Cpp(parser_.get(), token, input, environment_.get());
  }
}

}  // namespace vm
}  // namespace afc
