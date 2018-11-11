#include "../public/vm.h"

#include <cassert>
#include <fstream>
#include <istream>
#include <iostream>
#include <utility>
#include <sstream>  
#include <string>

#include <libgen.h>

#include <glog/logging.h>

#include "../public/environment.h"
#include "../public/value.h"
#include "append_expression.h"
#include "assign_expression.h"
#include "binary_operator.h"
#include "compilation.h"
#include "../public/constant_expression.h"
#include "evaluation.h"
#include "../public/function_call.h"
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

void CompileLine(Compilation* compilation, void* parser, const wstring& str);

string CppDirname(string path) {
  char* directory_c_str = strdup(path.c_str());
  string output = dirname(directory_c_str);
  free(directory_c_str);
  return output;
}

void CompileStream(std::wistream& stream, Compilation* compilation,
                   void* parser) {
  std::wstring line;
  while (std::getline(stream, line)) {
    VLOG(4) << "Compiling line: [" << line << "] (" << line.size() << ")";
    CompileLine(compilation, parser, line);
  }
}

void CompileFile(const string& path, Compilation* compilation, void* parser) {
  VLOG(3) << "Compiling file: [" << path << "]";

  std::wifstream infile(path);
  infile.imbue(std::locale(""));
  if (infile.fail()) {
    compilation->AddError(FromByteString(path) + L": open failed");
    return;
  }

  CompileStream(infile, compilation, parser);
}

void HandleInclude(Compilation* compilation, void* parser, const wstring& str,
                   size_t* pos_output) {
  VLOG(6) << "Processing #include directive.";
  size_t pos = *pos_output;
  while (pos < str.size() && str[pos] == ' ') {
    pos++;
  }
  if (pos >= str.size() || (str[pos] != '\"' && str[pos] != '<')) {
    VLOG(5) << "Processing #include failed: Expected opening delimiter";
    compilation->AddError(
        L"#include expects \"FILENAME\" or <FILENAME>; in line: "
        + str);
    return;
  }
  wchar_t delimiter = str[pos] == L'<' ? L'>' : L'\"';
  pos++;
  size_t start = pos;
  while (pos < str.size() && str[pos] != delimiter) {
    pos++;
  }
  if (pos >= str.size()) {
    VLOG(5) << "Processing #include failed: Expected closing delimiter";
    compilation->AddError(
        L"#include expects \"FILENAME\" or <FILENAME>, failed to find closing "
        L"character; in line: "
        + str);
    return;
  }
  wstring error_description;
  wstring path = str.substr(start, pos - start);
  string low_level_path = ToByteString(path);

  if (delimiter == '\"') {
    low_level_path = compilation->directory + "/" + low_level_path;
  }

  const string old_directory = compilation->directory;
  compilation->directory = CppDirname(low_level_path);

  CompileFile(low_level_path, compilation, parser);

  compilation->directory = old_directory;

  *pos_output = pos + 1;

  VLOG(5) << path << ": Done compiling.";
}

void CompileLine(Compilation* compilation, void* parser, const wstring& str) {
  CHECK(compilation != nullptr);
  size_t pos = 0;
  int token;
  while (pos < str.size()) {
    VLOG(5) << "Compiling from character: " << str.at(pos);
    std::unique_ptr<Value> input;
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
        compilation->AddError(L"Unhandled character: &");
        return;

      case '|':
        pos++;
        if (pos < str.size() && str.at(pos) == '|') {
          pos++;
          token = OR;
          break;
        }
        compilation->AddError(L"Unhandled character: |");
        return;

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

      case '#':
        pos++;
        {
          size_t start = pos;
          while (pos < str.size()
                 && (isalnum(str.at(pos)) || str.at(pos) == '_')) {
            pos++;
          }
          wstring symbol = str.substr(start, pos - start);
          if (symbol == L"include") {
            HandleInclude(compilation, parser, str, &pos);
          } else {
            compilation->AddError(
                L"Invalid preprocessing directive #" + symbol);
          }
          continue;
        }
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
        {
          int decimal = 0;
          while (pos < str.size() && isdigit(str.at(pos))) {
            decimal = decimal * 10 + str.at(pos) - '0';
            pos++;
          }
          if (pos < str.size() && str.at(pos) == '.') {
            pos++;
            double value = decimal;
            double current_fraction = 1;
            while (pos < str.size() && isdigit(str.at(pos))) {
              current_fraction /= 10;
              value += current_fraction * (str.at(pos) - '0');
              pos++;
            }
            token = DOUBLE;
            input = Value::NewDouble(value);
            input->double_value = decimal;
          } else {
            token = INTEGER;
            input = Value::NewInteger(decimal);
          }
        }
        break;

      case '"':
        {
          token = STRING;
          input = std::make_unique<Value>(VMType::VM_STRING);
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
      case 'v': case 'w': case 'x': case 'y': case 'z':
      case '_': case '~':
        {
          size_t start = pos;
          while (pos < str.size()
                 && (isalnum(str.at(pos))
                     || str.at(pos) == '_'
                     || str.at(pos) == '~')) {
            pos++;
          }
          wstring symbol = str.substr(start, pos - start);
          if (symbol == L"true") {
            token = BOOL;
            input = Value::NewBool(true);
          } else if (symbol == L"false") {
            token = BOOL;
            input = Value::NewBool(false);
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
            input = std::make_unique<Value>(VMType::VM_SYMBOL);
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
        compilation->AddError(
            L"Unhandled character at position: " + to_wstring(pos)
            + L" in line: " + str);
        return;
    }
    if (token == SYMBOL || token == STRING) {
      CHECK(input != nullptr) << "No input with token: " << token;
      CHECK(input->type == VMType::VM_SYMBOL
            || input->type == VMType::VM_STRING);
      compilation->last_token = input->str;
    }
    Cpp(parser, token, input.release(), compilation);
  }
}

unique_ptr<void, std::function<void(void*)>> GetParser(
    Compilation* compilation) {
  return unique_ptr<void, std::function<void(void*)>>(
      CppAlloc(malloc),
      [compilation](void* parser) {
        Cpp(parser, 0, nullptr, compilation);
        CppFree(parser, free);
      });
}

unique_ptr<Expression> ResultsFromCompilation(Compilation* compilation,
                                              wstring* error_description) {
  if (!compilation->errors.empty()) {
    if (error_description != nullptr) {
      wstring separator = L"";
      for (auto& error : compilation->errors) {
        *error_description += separator + error;
        separator = L"\n  ";
      }
    }
    return nullptr;
  }
  return std::move(compilation->expr);
}

}  // namespace

unique_ptr<Expression> CompileFile(
    const string& path, Environment* environment, wstring* error_description) {
  Compilation compilation;
  compilation.directory = CppDirname(path);
  compilation.expr = nullptr;
  compilation.environment = environment;
  compilation.return_types = { VMType::Void() };

  CompileFile(path, &compilation, GetParser(&compilation).get());

  return ResultsFromCompilation(&compilation, error_description);
}

unique_ptr<Expression> CompileString(
    const wstring& str, Environment* environment, wstring* error_description) {
  return CompileString(str, environment, error_description, VMType::Void());
}

unique_ptr<Expression> CompileString(
    const wstring& str, Environment* environment, wstring* error_description,
    const VMType& return_type) {
  std::wstringstream instr(str, std::ios_base::in);
  Compilation compilation;
  compilation.directory = ".";
  compilation.expr = nullptr;
  compilation.environment = environment;
  compilation.return_types = { return_type };

  CompileStream(instr, &compilation, GetParser(&compilation).get());

  return ResultsFromCompilation(&compilation, error_description);
}

Trampoline::Trampoline(
    Environment* environment,
    Continuation final_continuation)
    : environment_(environment),
      return_continuation_(std::move(final_continuation)),
      continuation_(return_continuation_) {}

void Trampoline::Enter(Expression* start_expression) {
  CHECK(start_expression != nullptr);
  expression_ = start_expression;
  while (expression_) {
    DVLOG(7) << "Jumping in the evaluation trampoline...";
    auto current_expression = expression_;
    expression_ = nullptr;
    current_expression->Evaluate(this);
    DVLOG(10) << "Landed in the evaluation trampoline...";
  }
  DVLOG(4) << "Leaving evaluation trampoline...";
}

std::function<void(Value::Ptr)> Trampoline::Interrupt() {
  DVLOG(5) << "Interrupting trampoline.";
  CHECK(expression_ == nullptr);
  auto state = Save();
  return [state](Value::Ptr value) {
    DVLOG(5) << "Resuming trampoline.";
    Trampoline trampoline(nullptr, nullptr);
    state(&trampoline);
    trampoline.Continue(std::move(value));
    if (trampoline.expression_) {
      trampoline.Enter(trampoline.expression_);
    }
  };
}

void Trampoline::Bounce(Expression* new_expression,
    Continuation new_continuation) {
  DVLOG(6) << "Bouncing in the trampoline.";
  CHECK(expression_ == nullptr);
  Continuation original_continuation = std::move(continuation_);
  expression_ = new_expression;
  continuation_ = [original_continuation, new_continuation](
      Value::Ptr value, Trampoline* trampoline) {
    // We do this copy because the assignment below may delete us.
    auto new_continuation_copy = std::move(new_continuation);
    trampoline->continuation_ = std::move(original_continuation);
    new_continuation_copy(std::move(value), trampoline);
  };
}

void Trampoline::Continue(std::unique_ptr<Value> value) {
  continuation_(std::move(value), this);
}

void Trampoline::Return(std::unique_ptr<Value> value) {
  return_continuation_(std::move(value), this);
}

std::function<void(Trampoline*)> Trampoline::Save() {
  DVLOG(5) << "Saving trampoline state.";
  auto original_environment = environment_;
  auto original_continuation = continuation_;
  auto original_return_continuation = return_continuation_;
  CHECK(original_environment != nullptr);
  CHECK(original_continuation != nullptr);
  CHECK(original_return_continuation != nullptr);
  return [original_environment, original_continuation,
          original_return_continuation](
      Trampoline* trampoline) {
    DVLOG(5) << "Restoring trampoline state.";
    // Make copies before overriding the continuations, since it may delete us.
    auto continuation_copy = original_continuation;
    auto return_continuation_copy = original_return_continuation;

    trampoline->environment_ = original_environment;
    trampoline->return_continuation_ = return_continuation_copy;
    trampoline->continuation_ = continuation_copy;
    CHECK(trampoline->environment_ != nullptr);
    CHECK(trampoline->continuation_ != nullptr);
    CHECK(trampoline->return_continuation_ != nullptr);
  };
}

void Trampoline::SetEnvironment(Environment* environment) {
  environment_ = environment;
}

Environment* Trampoline::environment() const { return environment_; }

void Trampoline::SetReturnContinuation(Continuation continuation) {
  return_continuation_ = continuation;
}

Trampoline::Continuation Trampoline::return_continuation() const {
  return return_continuation_;
}

void Trampoline::SetContinuation(Continuation continuation) {
  continuation_ = continuation;
}

void Evaluate(Expression* expr, Environment* environment,
              std::function<void(std::unique_ptr<Value>)> consumer) {
  CHECK(expr != nullptr);
  Trampoline(environment,
             [consumer](std::unique_ptr<Value> value, Trampoline*) {
               DVLOG(4) << "Evaluation done.";
               DVLOG(5) << "Result: " << *value;
               consumer(std::move(value));
             })
      .Enter(expr);
}

}  // namespace vm
}  // namespace afc
