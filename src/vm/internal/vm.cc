#include "../public/vm.h"

#include <glog/logging.h>
#include <libgen.h>

#include <fstream>
#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>

#include "../public/constant_expression.h"
#include "../public/environment.h"
#include "../public/function_call.h"
#include "../public/value.h"
#include "append_expression.h"
#include "assign_expression.h"
#include "binary_operator.h"
#include "compilation.h"
#include "if_expression.h"
#include "lambda.h"
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
extern "C" {
// The code generated by Lemon uses assert.
#include <cassert>

#include "cpp.c"
#include "cpp.h"
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
        L"#include expects \"FILENAME\" or <FILENAME>; in line: " + str);
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
        L"character; in line: " +
        str);
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
          pos++;
          if (pos < str.size() && str.at(pos) == '=') {
            pos++;
            token = DIVIDE_EQ;
          } else {
            token = DIVIDE;
          }
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

      case '[':
        pos++;
        token = LBRACE;
        break;

      case ']':
        pos++;
        token = RBRACE;
        break;

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
        if (pos < str.size() && str.at(pos) == '=') {
          pos++;
          token = LESS_OR_EQUAL;
        }
        break;

      case '>':
        token = GREATER_THAN;
        pos++;
        if (pos < str.size() && str.at(pos) == '=') {
          pos++;
          token = GREATER_OR_EQUAL;
        }
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
          while (pos < str.size() &&
                 (iswalnum(str.at(pos)) || str.at(pos) == '_')) {
            pos++;
          }
          wstring symbol = str.substr(start, pos - start);
          if (symbol == L"include") {
            HandleInclude(compilation, parser, str, &pos);
          } else {
            compilation->AddError(L"Invalid preprocessing directive #" +
                                  symbol);
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
        pos++;
        if (pos < str.size() && str.at(pos) == '=') {
          pos++;
          token = PLUS_EQ;
        } else if (pos < str.size() && str.at(pos) == '+') {
          pos++;
          token = PLUS_PLUS;
        } else {
          token = PLUS;
        }
        break;

      case '-':
        pos++;
        if (pos < str.size() && str.at(pos) == '=') {
          pos++;
          token = MINUS_EQ;
        } else if (pos < str.size() && str.at(pos) == '-') {
          pos++;
          token = MINUS_MINUS;
        } else {
          token = MINUS;
        }
        break;

      case '*':
        pos++;
        if (pos < str.size() && str.at(pos) == '=') {
          pos++;
          token = TIMES_EQ;
        } else {
          token = TIMES;
        }
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
      case '9': {
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
        } else {
          token = INTEGER;
          input = Value::NewInteger(decimal);
        }
      } break;

      case '"': {
        token = STRING;
        input = std::make_unique<Value>(VMType::VM_STRING);
        pos++;
        input->str = L"";
        for (; pos < str.size(); pos++) {
          if (str.at(pos) == '"') {
            break;
          }
          if (str.at(pos) != '\\') {
            input->str.push_back(str.at(pos));
            ;
            continue;
          }
          pos++;
          if (pos >= str.size()) {
            continue;
          }
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
      } break;

      case 0:
      case ' ':
      case '\n':
      case '\t':
        pos++;
        continue;

      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
      case 'G':
      case 'H':
      case 'I':
      case 'J':
      case 'K':
      case 'L':
      case 'M':
      case 'N':
      case 'O':
      case 'P':
      case 'Q':
      case 'R':
      case 'S':
      case 'T':
      case 'U':
      case 'V':
      case 'W':
      case 'X':
      case 'Y':
      case 'Z':
      case 'a':
      case 'b':
      case 'c':
      case 'd':
      case 'e':
      case 'f':
      case 'g':
      case 'h':
      case 'i':
      case 'j':
      case 'k':
      case 'l':
      case 'm':
      case 'n':
      case 'o':
      case 'p':
      case 'q':
      case 'r':
      case 's':
      case 't':
      case 'u':
      case 'v':
      case 'w':
      case 'x':
      case 'y':
      case 'z':
      case '_':
      case '~': {
        size_t start = pos;
        while (pos < str.size() && (iswalnum(str.at(pos)) ||
                                    str.at(pos) == '_' || str.at(pos) == '~')) {
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
        } else if (symbol == L"for") {
          token = FOR;
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
      } break;

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
        compilation->AddError(L"Unhandled character at position: " +
                              to_wstring(pos) + L" in line: " + str);
        return;
    }
    if (token == SYMBOL || token == STRING) {
      CHECK(input != nullptr) << "No input with token: " << token;
      CHECK(input->type == VMType::VM_SYMBOL ||
            input->type == VMType::VM_STRING);
      compilation->last_token = input->str;
    }
    Cpp(parser, token, input.release(), compilation);
  }
}

unique_ptr<void, std::function<void(void*)>> GetParser(
    Compilation* compilation) {
  return unique_ptr<void, std::function<void(void*)>>(
      CppAlloc(malloc), [compilation](void* parser) {
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

std::optional<std::unordered_set<VMType>> CombineReturnTypes(
    std::unordered_set<VMType> a, std::unordered_set<VMType> b,
    std::wstring* error) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (a != b) {
    *error = L"Incompatible types found: `" + a.cbegin()->ToString() +
             L"` and `" + b.cbegin()->ToString() + L"`.";
    return std::nullopt;
  }
  return a;
}

unique_ptr<Expression> CompileFile(const string& path,
                                   std::shared_ptr<Environment> environment,
                                   wstring* error_description) {
  Compilation compilation;
  compilation.directory = CppDirname(path);
  compilation.expr = nullptr;
  compilation.environment = std::move(environment);

  CompileFile(path, &compilation, GetParser(&compilation).get());

  return ResultsFromCompilation(&compilation, error_description);
}

std::unique_ptr<Expression> CompileString(
    const std::wstring& str, std::shared_ptr<Environment> environment,
    std::wstring* error_description) {
  std::wstringstream instr(str, std::ios_base::in);
  Compilation compilation;
  compilation.directory = ".";
  compilation.expr = nullptr;
  compilation.environment = std::move(environment);

  CompileStream(instr, &compilation, GetParser(&compilation).get());

  return ResultsFromCompilation(&compilation, error_description);
}

Trampoline::Trampoline(Options options)
    : environment_(options.environment),
      yield_callback_(std::move(options.yield_callback)) {}

futures::DelayedValue<EvaluationOutput> Trampoline::Bounce(
    Expression* expression, VMType type) {
  CHECK(expression->SupportsType(type));
  static size_t kMaximumJumps = 100;
  if (++jumps_ < kMaximumJumps || yield_callback_ == nullptr) {
    return expression->Evaluate(this, type);
  }

  futures::Future<EvaluationOutput> output;
  yield_callback_([this, expression_raw = expression->Clone().release(), type,
                   consumer = std::move(output.consumer)]() mutable {
    std::unique_ptr<Expression> expression(expression_raw);
    jumps_ = 0;
    Bounce(expression.get(), type).SetConsumer(std::move(consumer));
  });
  return std::move(output.value);
}

void Trampoline::SetEnvironment(std::shared_ptr<Environment> environment) {
  environment_ = environment;
}

const std::shared_ptr<Environment>& Trampoline::environment() const {
  return environment_;
}

futures::DelayedValue<std::unique_ptr<Value>> Evaluate(
    Expression* expr, std::shared_ptr<Environment> environment,
    std::function<void(std::function<void()>)> yield_callback) {
  CHECK(expr != nullptr);
  Trampoline::Options options;
  options.environment = environment;
  options.yield_callback = yield_callback;
  auto trampoline = std::make_shared<Trampoline>(options);
  return futures::ImmediateTransform(trampoline->Bounce(expr, expr->Types()[0]),
                                     [trampoline](EvaluationOutput value) {
                                       DVLOG(4) << "Evaluation done.";
                                       DVLOG(5) << "Result: " << *value.value;
                                       return std::move(value.value);
                                     });
}

}  // namespace vm
}  // namespace afc
