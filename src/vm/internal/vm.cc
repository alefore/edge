#include "../public/vm.h"

#include <cassert>
#include <fstream>
#include <istream>
#include <iostream>
#include <utility>
#include <sstream>  
#include <string>

#include <glog/logging.h>

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
#include "wstring.h"

namespace afc {
namespace vm {

using std::cerr;
using std::pair;
using std::to_wstring;

namespace {

struct UserFunction {
  wstring name;
  VMType type;
  vector<wstring> argument_names;
};

extern "C" {
#include "cpp.h"
#include "cpp.c"
}

void CompileLine(Compilation* compilation, void* parser, const wstring& str) {
  size_t pos = 0;
  int token;
  while (pos < str.size()) {
    VLOG(5) << "Compiling from character: " << str.at(pos);
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
          input->str = L"";
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
          if (pos == str.size()) {
            compilation->AddError(L"Missing terminating \" character.");
            return;
          }
          pos++;
        }
        break;

      case 0:
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
          wstring symbol = str.substr(start, pos - start);
          if (symbol == L"true") {
            token = BOOL;
            input = new Value(VMType::VM_BOOLEAN);
            input->boolean = true;
          } else if (symbol == L"false") {
            token = BOOL;
            input = new Value(VMType::VM_BOOLEAN);
            input->boolean = false;
          } else if (symbol == L"while") {
            token = WHILE;
          } else if (symbol == L"if") {
            token = IF;
          } else if (symbol == L"else") {
            token = ELSE;
          } else if (symbol == L"return") {
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
        // TODO: Add str.
        cerr << "Unhandled character at position " << pos << "\n";
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
    Environment* environment, std::wistream& stream, wstring* error_description,
    const VMType& return_type) {
  void* parser = CppAlloc(malloc);

  Compilation compilation;
  compilation.expr = nullptr;
  compilation.environment = environment;
  compilation.return_types = { return_type };

  std::wstring line;
  while (std::getline(stream, line)) {
    VLOG(4) << "Compiling line: [" << line << "] (" << line.size() << ")";
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
    const string& path, Environment* environment, wstring* error_description) {
  VLOG(3) << "Compiling file: [" << path << "]";
  std::wifstream infile(path);
  infile.imbue(std::locale(""));
  if (infile.fail()) {
    *error_description = L"open failed";
    return nullptr;
  }
  return CompileStream(environment, infile, error_description, VMType::Void());
}

unique_ptr<Expression> CompileString(
    const wstring& str, Environment* environment, wstring* error_description) {
  return CompileString(str, environment, error_description, VMType::Void());
}

unique_ptr<Expression> CompileString(
    const wstring& str, Environment* environment, wstring* error_description,
    const VMType& return_type) {
  std::wistream* instr = new std::wstringstream(str, std::ios_base::in);
  return CompileStream(environment, *instr, error_description, return_type);
}

unique_ptr<Value> Evaluate(Expression* expr, Environment* environment) {
  assert(expr != nullptr);
  unique_ptr<Value> result;
  bool done = false;

  OngoingEvaluation evaluation;
  evaluation.return_advancer =
      [&result, &done](OngoingEvaluation* evaluation) {
        DVLOG(4) << "Evaluation done.";
        DVLOG(5) << "Result: " << *evaluation->value;
        assert(!done);
        done = true;
        result = std::move(evaluation->value);
      };
  evaluation.advancer = evaluation.return_advancer;
  evaluation.environment = environment;

  expr->Evaluate(&evaluation);
  while (!done) {
    DVLOG(7) << "Jumping in the evaluation trampoline...";
    auto advancer = evaluation.advancer;
    advancer(&evaluation);
    DVLOG(10) << "Landed in the evaluation trampoline...";
  }

  return result;
}

}  // namespace vm
}  // namespace afc
