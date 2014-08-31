#include "vm.h"

#include <cassert>
#include <iostream>

namespace afc {
namespace vm {

using std::cerr;

bool operator==(const VMType& lhs, const VMType& rhs) {
  return lhs.type == rhs.type && lhs.type_arguments == rhs.type_arguments;
}

/* static */ const VMType& VMType::Void() {
  static VMType type(VMType::VM_VOID);
  return type;
}

/* static */ const VMType& VMType::String() {
  static VMType type(VMType::VM_STRING);
  return type;
}

/* static */ const VMType& VMType::integer_type() {
  static VMType type(VMType::VM_INTEGER);
  return type;
}

/* static */ unique_ptr<Value> Value::Void() {
  return unique_ptr<Value>(new Value(VMType::VM_VOID));
}

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

Value* Environment::Lookup(const string& symbol) {
  auto it = table_.find(symbol);
  if (it != table_.end()) {
    return it->second.get();
  }
  if (parent_environment_ != nullptr) {
    return parent_environment_->Lookup(symbol);
  }

  return nullptr;
}

void Environment::Define(const string& symbol, unique_ptr<Value> value) {
  auto it = table_.insert(make_pair(symbol, nullptr));
  it.first->second = std::move(value);
}

void ValueDestructor(Value* value) {
  delete value;
}

#include "cpp.h"
#include "cpp.c"

Evaluator::Evaluator()
    : parser_(CppAlloc(malloc), [](void* parser) { CppFree(parser, free); }) {}

void Evaluator::Define(const string& name, unique_ptr<Value> value) {
  environment_.Define(name, std::move(value));
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
        }
        break;

      case '=':
        token = EQ;
        pos++;
        break;

      case ';':
        token = SEMICOLON;
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
          size_t start = ++pos;
          while (pos < str.size() && str.at(pos) != '"') {
            pos++;
          }
          input->str.reserve(pos - start);
          for (size_t i = start; i < pos; i++) {
            if (str.at(i) != '\\') {
              input->str.push_back(str.at(i));;
              continue;
            }
            i++;
            if (i >= pos) { continue; }
            switch (str.at(i)) {
              case 'n':
                input->str.push_back('\n');
                break;
              case 't':
                input->str.push_back('\t');
                break;
              default:
                input->str.push_back(str.at(i));
            }
          }
          pos++;
        }
        break;

      case ' ':
        pos++;
        continue;

      case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
      case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
      case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
      case 'V': case 'W': case 'X': case 'Y': case 'Z':
      case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
      case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
      case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
      case 'v': case 'w': case 'x': case 'y': case 'z':
        {
          size_t start = pos;
          while (pos < str.size() && isalnum(str.at(pos))) {
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
          } else if (symbol == "if") {
            token = IF;
          } else if (symbol == "string") {
            token = STRING_SYMBOL;
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
    Cpp(parser_.get(), token, input, &environment_);
  }
}

}  // namespace vm
}  // namespace afc
