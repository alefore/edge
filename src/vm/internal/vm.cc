#include "../public/vm.h"

#include <glog/logging.h>
#include <libgen.h>
#include <math.h>

#include <fstream>
#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>

#include "append_expression.h"
#include "assign_expression.h"
#include "binary_operator.h"
#include "class_expression.h"
#include "compilation.h"
#include "if_expression.h"
#include "lambda.h"
#include "logical_expression.h"
#include "namespace_expression.h"
#include "negate_expression.h"
#include "return_expression.h"
#include "src/infrastructure/dirname.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"
#include "string.h"
#include "types_promotion.h"
#include "variable_lookup.h"
#include "while_expression.h"
#include "wstring.h"

namespace afc {
namespace vm {

namespace {
using infrastructure::Path;
using language::Error;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;

namespace gc = language::gc;

extern "C" {
// The code generated by Lemon uses assert.
#include <cassert>

#include "cpp.c"
#include "cpp.h"
}

void CompileLine(Compilation& compilation, void* parser, const wstring& str);

void CompileStream(std::wistream& stream, Compilation& compilation,
                   void* parser) {
  std::wstring line;
  while (compilation.errors().empty() && std::getline(stream, line)) {
    VLOG(4) << "Compiling line: [" << line << "] (" << line.size() << ")";
    CompileLine(compilation, parser, line);
    compilation.IncrementLine();
  }
}

void CompileFile(Path path, Compilation& compilation, void* parser) {
  VLOG(3) << "Compiling file: [" << path << "]";

  compilation.PushSource(path);

  std::wifstream infile(ToByteString(path.read()));
  infile.imbue(std::locale(""));
  if (infile.fail()) {
    compilation.AddError(path.read() + L": open failed");
  } else {
    CompileStream(infile, compilation, parser);
  }

  compilation.PopSource();
}

void HandleInclude(Compilation& compilation, void* parser, const wstring& str,
                   size_t* pos_output) {
  CHECK(compilation.errors().empty());

  VLOG(6) << "Processing #include directive.";
  size_t pos = *pos_output;
  while (pos < str.size() && str[pos] == ' ') {
    pos++;
  }
  if (pos >= str.size() || (str[pos] != '\"' && str[pos] != '<')) {
    VLOG(5) << "Processing #include failed: Expected opening delimiter";
    compilation.AddError(
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
    compilation.AddError(
        L"#include expects \"FILENAME\" or <FILENAME>, failed to find closing "
        L"character; in line: " +
        str);
    return;
  }

  Visit(
      Path::FromString(str.substr(start, pos - start)),
      [&](Path path) {
        if (delimiter == '\"' &&
            path.GetRootType() == Path::RootType::kRelative &&
            compilation.current_source_path().has_value()) {
          Visit(
              compilation.current_source_path()->Dirname(),
              [&](Path source_directory) {
                path = Path::Join(source_directory, path);
              },
              [](Error) {});
        }

        CompileFile(path, compilation, parser);
        // TODO(easy, 2022-05-28): Move this to compilation.AddError?
        for (auto& error : compilation.errors()) {
          error = L"During processing of included file \"" + path.read() +
                  L"\": " + error;
        }

        *pos_output = pos + 1;
        VLOG(5) << path << ": Done compiling.";
      },
      [&](Error error) {
        compilation.AddError(L"#include was unable to extract path; in line: " +
                             str + L"; error: " + error.description);
      });
}

int ConsumeDecimal(const wstring& str, size_t* pos) {
  int output = 0;
  while (*pos < str.size() && isdigit(str.at(*pos))) {
    output = output * 10 + str.at(*pos) - '0';
    (*pos)++;
  }
  return output;
}

void CompileLine(Compilation& compilation, void* parser, const wstring& str) {
  CHECK(compilation.errors().empty());
  size_t pos = 0;
  int token;
  while (compilation.errors().empty() && pos < str.size()) {
    compilation.SetSourceColumnInLine(pos);
    VLOG(5) << L"Compiling from character: " << std::wstring(1, str.at(pos));
    std::optional<gc::Root<Value>> input;
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
        compilation.AddError(L"Unhandled character: &");
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
        compilation.AddError(L"Unhandled character: |");
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
        if (pos < str.size() && str.at(pos) == ':') {
          pos++;
          token = DOUBLECOLON;
        }
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
            compilation.AddError(L"Invalid preprocessing directive #" + symbol);
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
        int decimal = ConsumeDecimal(str, &pos);
        if (pos < str.size() && (str.at(pos) == '.' || str.at(pos) == 'e')) {
          double value = decimal;
          double current_fraction = 1;
          if (str.at(pos) == '.') {
            pos++;
            while (pos < str.size() && isdigit(str.at(pos))) {
              current_fraction /= 10;
              value += current_fraction * (str.at(pos) - '0');
              pos++;
            }
          }
          if (pos < str.size() && str.at(pos) == 'e') {
            pos++;
            int signal = 1;
            if (pos < str.size()) {
              switch (str.at(pos)) {
                case '+':
                  signal = 1;
                  pos++;
                  break;
                case L'-':
                  signal = -1;
                  pos++;
                  break;
              }
            }
            value *= pow(10, signal * ConsumeDecimal(str, &pos));
          }
          token = DOUBLE;
          input = Value::NewDouble(compilation.pool, value);
        } else {
          token = INTEGER;
          input = Value::NewInt(compilation.pool, decimal);
        }
      } break;

      case '"': {
        token = STRING;
        std::wstring output_string;
        pos++;
        for (; pos < str.size(); pos++) {
          if (str.at(pos) == '"') {
            break;
          }
          if (str.at(pos) != '\\') {
            output_string.push_back(str.at(pos));
            ;
            continue;
          }
          pos++;
          if (pos >= str.size()) {
            continue;
          }
          switch (str.at(pos)) {
            case 'n':
              output_string.push_back('\n');
              break;
            case 't':
              output_string.push_back('\t');
              break;
            case '"':
              output_string.push_back('"');
              break;
            default:
              output_string.push_back(str.at(pos));
          }
        }
        if (pos == str.size()) {
          compilation.AddError(L"Missing terminating \" character.");
          return;
        }
        input = Value::NewString(compilation.pool, std::move(output_string));
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
        struct Keyword {
          int token;
          std::function<gc::Root<Value>()> value_supplier = nullptr;
        };
        static const auto* const keywords =
            new std::unordered_map<std::wstring, Keyword>(
                {{L"true",
                  {.token = BOOL,
                   .value_supplier =
                       [&pool = compilation.pool] {
                         return Value::NewBool(pool, true);
                       }}},
                 {L"false",
                  {.token = BOOL,
                   .value_supplier =
                       [&pool = compilation.pool] {
                         return Value::NewBool(pool, false);
                       }}},
                 {L"while", {.token = WHILE}},
                 {L"for", {.token = FOR}},
                 {L"if", {.token = IF}},
                 {L"else", {.token = ELSE}},
                 {L"return", {.token = RETURN}},
                 {L"namespace", {.token = NAMESPACE}},
                 {L"class", {.token = CLASS}}});
        if (auto it = keywords->find(symbol); it != keywords->end()) {
          token = it->second.token;
          if (auto supplier = it->second.value_supplier; supplier != nullptr) {
            input = supplier();
          }
        } else {
          token = SYMBOL;
          input = Value::NewSymbol(compilation.pool, symbol);
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
        compilation.AddError(L"Unhandled character at position: " +
                             std::to_wstring(pos) + L" in line: " + str);
        return;
    }
    if (token == SYMBOL || token == STRING) {
      CHECK(input.has_value()) << "No input with token: " << token;
      if (input.value().ptr()->IsSymbol())
        compilation.last_token = input.value().ptr()->get_symbol();
      else if (input.value().ptr()->IsString())
        compilation.last_token = input.value().ptr()->get_string();
      else
        LOG(FATAL) << "Invalid input.";
    }
    Cpp(parser, token,
        std::make_unique<std::optional<gc::Root<Value>>>(input).release(),
        &compilation);
  }
}

std::unique_ptr<void, std::function<void(void*)>> GetParser(
    Compilation& compilation) {
  return std::unique_ptr<void, std::function<void(void*)>>(
      CppAlloc(malloc), [&compilation](void* parser) {
        Cpp(parser, 0, nullptr, &compilation);
        CppFree(parser, free);
      });
}

ValueOrError<NonNull<std::unique_ptr<Expression>>> ResultsFromCompilation(
    Compilation compilation) {
  if (!compilation.errors().empty()) {
    std::wstring error_description;
    wstring separator = L"";
    for (auto& error : compilation.errors()) {
      error_description += separator + error;
      separator = L"\n  ";
    }
    return Error(error_description);
  }
  return VisitPointer(std::move(compilation.expr),
                      &language::Success<NonNull<std::unique_ptr<Expression>>>,
                      []() { return Error(L"Unexpected empty expression."); });
}
}  // namespace

ValueOrError<std::unordered_set<VMType>> CombineReturnTypes(
    std::unordered_set<VMType> a, std::unordered_set<VMType> b) {
  if (a.empty()) return Success(b);
  if (b.empty()) return Success(a);
  if (a != b) {
    return Error(L"Incompatible return types found: `" +
                 a.cbegin()->ToString() + L"` and `" + b.cbegin()->ToString() +
                 L"`.");
  }
  return Success(a);
}

ValueOrError<NonNull<std::unique_ptr<Expression>>> CompileFile(
    Path path, gc::Pool& pool, gc::Root<Environment> environment) {
  Compilation compilation(pool, std::move(environment));
  CompileFile(path, compilation, GetParser(compilation).get());
  return ResultsFromCompilation(std::move(compilation));
}

ValueOrError<NonNull<std::unique_ptr<Expression>>> CompileString(
    const std::wstring& str, gc::Pool& pool,
    gc::Root<Environment> environment) {
  std::wstringstream instr(str, std::ios_base::in);
  Compilation compilation(pool, std::move(environment));
  compilation.PushSource(std::nullopt);
  CompileStream(instr, compilation, GetParser(compilation).get());
  compilation.PopSource();
  return ResultsFromCompilation(std::move(compilation));
}

Trampoline::Trampoline(Options options)
    : pool_(NonNull<gc::Pool*>::AddressOf(options.pool)),
      environment_(std::move(options.environment)),
      yield_callback_(std::move(options.yield_callback)) {}

futures::ValueOrError<EvaluationOutput> Trampoline::Bounce(
    Expression& expression, VMType type) {
  if (!expression.SupportsType(type)) {
    LOG(FATAL) << "Expression has types: " << TypesToString(expression.Types())
               << ", expected: " << type;
  }
  static const size_t kMaximumJumps = 100;
  if (++jumps_ < kMaximumJumps || yield_callback_ == nullptr) {
    return expression.Evaluate(*this, type);
  }

  futures::Future<language::ValueOrError<EvaluationOutput>> output;
  yield_callback_([this,
                   expression_raw = expression.Clone().get_unique().release(),
                   type, consumer = std::move(output.consumer)]() mutable {
    std::unique_ptr<Expression> expression(expression_raw);
    jumps_ = 0;
    Bounce(*expression, type).SetConsumer(std::move(consumer));
  });
  return std::move(output.value);
}

void Trampoline::SetEnvironment(gc::Root<Environment> environment) {
  environment_ = environment;
}

const gc::Root<Environment>& Trampoline::environment() const {
  return environment_;
}

gc::Pool& Trampoline::pool() const { return pool_.value(); }

bool Expression::SupportsType(const VMType& type) {
  auto types = Types();
  if (std::find(types.begin(), types.end(), type) != types.end()) {
    return true;
  }
  for (auto& source : types) {
    if (GetImplicitPromotion(source, type) != nullptr) return true;
  }
  return false;
}

futures::ValueOrError<gc::Root<Value>> Evaluate(
    Expression& expr, gc::Pool& pool, gc::Root<Environment> environment,
    std::function<void(std::function<void()>)> yield_callback) {
  NonNull<std::shared_ptr<Trampoline>> trampoline =
      MakeNonNullShared<Trampoline>(
          Trampoline::Options{.pool = pool,
                              .environment = std::move(environment),
                              .yield_callback = std::move(yield_callback)});
  return OnError(trampoline->Bounce(expr, expr.Types()[0])
                     .Transform([trampoline](EvaluationOutput value)
                                    -> language::ValueOrError<gc::Root<Value>> {
                       DVLOG(5)
                           << "Evaluation done: " << value.value.ptr().value();
                       return Success(std::move(value.value));
                     }),
                 [](Error error) {
                   LOG(INFO) << "Evaluation error: " << error;
                   return futures::Past(error);
                 });
}

}  // namespace vm
}  // namespace afc
