#include "../public/vm.h"

#include <cassert>
#include <fstream>
#include <istream>
#include <iostream>
#include <utility>
#include <sstream>  
#include <string>

#include "../public/environment.h"
#include "../public/value.h"
#include "append_expression.h"
#include "assign_expression.h"
#include "binary_operator.h"
#include "compilation.h"
#include "constant_expression.h"
#include "evaluation.h"
#include "function_call.h"
#include "if_expression.h"
#include "logical_expression.h"
#include "negate_expression.h"
#include "return_expression.h"
#include "string.h"
#include "variable_lookup.h"
#include "while_expression.h"

namespace afc {
namespace vm {

using std::cerr;
using std::pair;
using std::to_string;

namespace {

struct UserFunction {
  string name;
  VMType type;
  vector<string> argument_names;
};

extern "C" {
#include "cpp.h"
#include "cpp.c"
}

void CompileLine(Compilation* compilation, void* parser, const string& str) {
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

      case ':':
        token = COLON;
        pos++;
        break;

      case '?':
        token = QUESTION_MARK;
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
      compilation->last_token = input->str;
    }
    Cpp(parser, token, input, compilation);
  }
}

}  // namespace

unique_ptr<Expression> CompileStream(
    Environment* environment, std::istream& stream, string* error_description,
    const VMType& return_type) {
  void* parser = CppAlloc(malloc);

  Compilation compilation;
  compilation.expr = nullptr;
  compilation.environment = environment;
  compilation.return_types = { return_type };

  std::string line;
  while (std::getline(stream, line)) {
    CompileLine(&compilation, parser, line);
  }

  Cpp(parser, 0, nullptr, &compilation);
  CppFree(parser, free);

  if (!compilation.errors.empty()) {
    *error_description = *compilation.errors.rbegin();
    return nullptr;
  }
  return std::move(compilation.expr);
}

unique_ptr<Expression> CompileFile(
    const string& path, Environment* environment, string* error_description) {
  std::ifstream infile(path);
  if (infile.fail()) { return nullptr; }
  return CompileStream(environment, infile, error_description, VMType::Void());
}

unique_ptr<Expression> CompileString(
    const string& str, Environment* environment, string* error_description) {
  return CompileString(str, environment, error_description, VMType::Void());
}

unique_ptr<Expression> CompileString(
    const string& str, Environment* environment, string* error_description,
    const VMType& return_type) {
  std::istream* instr = new std::stringstream(str, std::ios_base::in);
  return CompileStream(environment, *instr, error_description, return_type);
}

unique_ptr<Value> Evaluate(Expression* expr, Environment* environment) {
  assert(expr != nullptr);
  unique_ptr<Value> result;
  bool done = false;

  Evaluation evaluation;
  evaluation.return_continuation = Expression::Continuation(
      [&result, &done, &evaluation](unique_ptr<Value> value) {
        assert(!done);
        done = true;
        result = std::move(value);
        return make_pair(evaluation.return_continuation,
                         unique_ptr<Value>(nullptr));
      });
  evaluation.continuation = evaluation.return_continuation;
  evaluation.environment = environment;

  pair<Expression::Continuation, unique_ptr<Value>> trampoline =
      expr->Evaluate(evaluation);
  while (!done) {
    trampoline = trampoline.first.func(std::move(trampoline.second));
  }

  return result;
}

}  // namespace vm
}  // namespace afc
