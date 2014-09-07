#include "vm.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <utility>

#include "string.h"

namespace afc {
namespace vm {

using std::cerr;
using std::pair;
using std::to_string;

struct UserFunction {
  string name;
  VMType type;
  vector<string> argument_names;
};

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

string VMType::ToString() const {
  switch (type) {
    case VM_VOID: return "void";
    case VM_BOOLEAN: return "bool";
    case VM_INTEGER: return "int";
    case VM_STRING: return "string";
    case VM_SYMBOL: return "symbol";
    case ENVIRONMENT: return "environment";
    case FUNCTION: return "function";
    case OBJECT_TYPE: return object_type;
  }
  return "unknown";
}

/* static */ unique_ptr<Value> Value::Void() {
  return unique_ptr<Value>(new Value(VMType::VM_VOID));
}

class ReturnExpression : public Expression {
 public:
  ReturnExpression(unique_ptr<Expression> expr) : expr_(std::move(expr)) {}

  const VMType& type() { return expr_->type(); }

  pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator, const Continuation& continuation) {
    return expr_->Evaluate(evaluator, evaluator->return_continuation());
  }
 private:
  unique_ptr<Expression> expr_;
};

class IfEvaluator : public Expression {
 public:
  IfEvaluator(unique_ptr<Expression> cond, unique_ptr<Expression> true_case,
              unique_ptr<Expression> false_case)
      : cond_(std::move(cond)),
        true_case_(std::move(true_case)),
        false_case_(std::move(false_case)) {
    assert(cond_ != nullptr);
    assert(true_case_ != nullptr);
    assert(false_case_ != nullptr);
  }

  const VMType& type() {
    return true_case_->type();
  }

  pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator, const Continuation& continuation) {
    return cond_->Evaluate(
        evaluator,
        Continuation(
            [this, evaluator, continuation](unique_ptr<Value> result) {
              return (result->boolean ? true_case_ : false_case_)
                  ->Evaluate(evaluator, continuation);
            }));
  }

 private:
  unique_ptr<Expression> cond_;
  unique_ptr<Expression> true_case_;
  unique_ptr<Expression> false_case_;
};

class AssignExpression : public Expression {
 public:
  AssignExpression(const string& symbol, unique_ptr<Expression> value)
      : symbol_(symbol), value_(std::move(value)) {}

  const VMType& type() { return value_->type(); }

  pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator, const Continuation& continuation) {
    return value_->Evaluate(
        evaluator,
        Continuation([evaluator, symbol_, continuation](unique_ptr<Value> value) {
          evaluator->environment()->Define(
              symbol_, unique_ptr<Value>(new Value(*value.get())));
          return std::move(make_pair(continuation, std::move(value)));
        }));
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

  pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator,
      const Continuation& continuation) {
    unique_ptr<Value> value(new Value(value_->type.type));
    *value = *value_;
    return make_pair(continuation, std::move(value));
  }

 private:
  unique_ptr<Value> value_;
};

class NegateExpression : public Expression {
 public:
  NegateExpression(unique_ptr<Expression> expr)
      : expr_(std::move(expr)) {}

  const VMType& type() { return expr_->type(); }

  pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator,
      const Continuation& continuation) {
    return expr_->Evaluate(
        evaluator,
        Continuation([continuation](unique_ptr<Value> value) {
          unique_ptr<Value> output(new Value(VMType::VM_BOOLEAN));
          output->boolean = !value->boolean;
          return make_pair(continuation, std::move(output));
        }));
  }

 private:
  unique_ptr<Expression> expr_;
};

class BinaryOperator : public Expression {
 public:
  BinaryOperator(unique_ptr<Expression> a, unique_ptr<Expression> b,
                 const VMType type,
                 function<void(const Value&, const Value&, Value*)> callback)
      : a_(std::move(a)), b_(std::move(b)), type_(type), operator_(callback) {}

  const VMType& type() { return type_; }

  pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator, const Continuation& continuation) {
    return a_->Evaluate(
        evaluator,
        Continuation([this, evaluator, continuation](unique_ptr<Value> a_value) {
          // TODO: Remove shared_ptr when we can correctly capture a unique_ptr.
          shared_ptr<Value> a_value_shared(a_value.release());
          return b_->Evaluate(
              evaluator,
              Continuation([this, a_value_shared, continuation](unique_ptr<Value> b_value) {
                unique_ptr<Value> output(new Value(type_));
                operator_(*a_value_shared.get(), *b_value, output.get());
                return std::move(make_pair(continuation, std::move(output)));
              }));
        }));
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
               unique_ptr<vector<unique_ptr<Expression>>> args)
      : func_(std::move(func)), args_(std::move(args)) {
    assert(func_ != nullptr);
    assert(args_ != nullptr);
  }

  const VMType& type() {
    return func_->type().type_arguments[0];
  }

  pair<Continuation, unique_ptr<Value>> Evaluate(
      Evaluator* evaluator, const Continuation& continuation) {
    return func_->Evaluate(
        evaluator,
        Continuation([this, evaluator, continuation](unique_ptr<Value> func) {
          assert(func != nullptr);
          if (args_->empty()) {
            return make_pair(continuation,
                             func->callback(vector<unique_ptr<Value>>()));
          }
          return args_->at(0)->Evaluate(evaluator, capture_args(
              evaluator, continuation, new vector<unique_ptr<Value>>,
              shared_ptr<Value>(func.release())));
        }));
  }

 private:
  Continuation capture_args(
      Evaluator* evaluator,
      Continuation continuation,
      vector<unique_ptr<Value>>* values,
      shared_ptr<Value> func) {
    return Continuation(
        [this, evaluator, continuation, func, values](unique_ptr<Value> value) {
          values->push_back(std::move(value));
          if (values->size() < args_->size()) {
            return args_->at(values->size())->Evaluate(
                evaluator, capture_args(evaluator, continuation, values, func));
          }
          // TODO: Delete values, we're memory leaking here.
          return make_pair(continuation, func->callback(std::move(*values)));
        });
  }

  unique_ptr<Expression> func_;
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

unique_ptr<Evaluator> Environment::NewEvaluator(
    Evaluator::ErrorHandler error_handler) {
  return unique_ptr<Evaluator>(new Evaluator(this, error_handler));
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

pair<Expression::Continuation, unique_ptr<Value>>
NoopContinuation(unique_ptr<Value> value) {
  return make_pair(Expression::Continuation(NoopContinuation),
                   std::move(value));
}

Evaluator::Evaluator(Environment* environment,
                     ErrorHandler error_handler)
    : base_environment_(environment),
      return_continuation_(NoopContinuation),
      environment_(base_environment_),
      error_handler_(error_handler),
      parser_(
          CppAlloc(malloc),
          [this](void* parser) {
            Cpp(parser, 0, nullptr, this);
            CppFree(parser, free);
          }) {}

void Evaluator::PushEnvironment() {
  environment_ = new Environment(environment_);
}

void Evaluator::PopEnvironment() {
  auto tmp = environment_;
  environment_ = environment_->parent_environment();
  delete tmp;
}

unique_ptr<Value> Evaluator::Evaluate(Expression* expression) {
  return Evaluate(expression, environment_);
}

unique_ptr<Value> Evaluator::Evaluate(
    Expression* expr, Environment* environment) {
  assert(expr != nullptr);
  unique_ptr<Value> result;
  bool done = false;

  Expression::Continuation stop(
      [&result, &done, &stop](unique_ptr<Value> value) {
        assert(!done);
        done = true;
        result = std::move(value);
        return make_pair(stop, unique_ptr<Value>(nullptr));
      });

  Expression::Continuation old_return_continuation = return_continuation_;
  return_continuation_ = stop;
  Environment* old_environment = environment_;
  environment_ = environment;

  pair<Expression::Continuation, unique_ptr<Value>> trampoline =
      expr->Evaluate(this, stop);
  while (!done) {
    trampoline = trampoline.first.func(std::move(trampoline.second));
  }

  return_continuation_ = old_return_continuation;
  environment_ = old_environment;
  return result;
}

void Evaluator::EvaluateFile(const string& path) {
  std::ifstream infile(path);
  std::string line;
  while (std::getline(infile, line)) {
    AppendInput(line);
  }
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
          continue;
        } else {
          token = DIVIDE;
          pos++;
        }
        break;

      case '!':
        pos++;
        if (pos < str.size() && str.at(pos) == '=') {
          pos++;
          token = NOT_EQUALS;
          break;
        }
        token = NOT;
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
          } else if (symbol == "return") {
            token = RETURN;
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
    if (token == SYMBOL || token == STRING) {
      last_token_ = input->str;
    }
    Cpp(parser_.get(), token, input, this);
  }
}

}  // namespace vm
}  // namespace afc
