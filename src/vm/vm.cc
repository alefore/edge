#include "src/vm/vm.h"

#include <glog/logging.h>
#include <libgen.h>
#include <math.h>

#include <fstream>
#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>

#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/vm/append_expression.h"
#include "src/vm/assign_expression.h"
#include "src/vm/binary_operator.h"
#include "src/vm/class_expression.h"
#include "src/vm/compilation.h"
#include "src/vm/constant_expression.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/function_call.h"
#include "src/vm/if_expression.h"
#include "src/vm/lambda.h"
#include "src/vm/logical_expression.h"
#include "src/vm/namespace_expression.h"
#include "src/vm/negate_expression.h"
#include "src/vm/return_expression.h"
#include "src/vm/string.h"
#include "src/vm/value.h"
#include "src/vm/variable_lookup.h"
#include "src/vm/while_expression.h"

namespace gc = afc::language::gc;
namespace numbers = afc::math::numbers;

using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NewError;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ToUniquePtr;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using numbers::BigInt;

namespace afc {
namespace vm {
namespace {
using ::operator<<;

extern "C" {
// The code generated by Lemon uses assert.
#include <cassert>

#include "src/vm/cpp.c"
#include "src/vm/cpp.h"
}

void CompileLine(Compilation& compilation, void* parser, const LazyString& str);

void CompileStream(std::wistream& stream, Compilation& compilation,
                   void* parser) {
  std::wstring line;
  while (compilation.errors().empty() && std::getline(stream, line)) {
    VLOG(4) << "Compiling line: [" << line << "] (" << line.size() << ")";
    CompileLine(compilation, parser, LazyString{std::move(line)});
    compilation.IncrementLine();
  }
}

void CompileFile(Path path, Compilation& compilation, void* parser) {
  VLOG(3) << "Compiling file: [" << path << "]";

  compilation.PushSource(path);

  std::wifstream infile(ToByteString(path.read()));
  infile.imbue(std::locale(""));
  if (infile.fail()) {
    compilation.AddError(Error(path.read() + L": open failed"));
  } else {
    CompileStream(infile, compilation, parser);
  }

  compilation.PopSource();
}

// It is the responsibility of the caller to register errors to compilation.
PossibleError HandleInclude(Compilation& compilation, void* parser,
                            const LazyString& str, ColumnNumber* pos_output) {
  CHECK(compilation.errors().empty());

  VLOG(6) << "Processing #include directive.";
  ColumnNumber pos = *pos_output;
  while (pos.ToDelta() < str.size() && str.get(pos) == ' ') ++pos;
  if (pos.ToDelta() >= str.size() ||
      (str.get(pos) != '\"' && str.get(pos) != '<')) {
    VLOG(5) << "Processing #include failed: Expected opening delimiter";
    return NewError(
        LazyString{L"#include expects \"FILENAME\" or <FILENAME>; in line: "} +
        str);
  }
  wchar_t delimiter = str.get(pos) == L'<' ? L'>' : L'\"';
  ++pos;
  ColumnNumber start = pos;
  while (pos.ToDelta() < str.size() && str.get(pos) != delimiter) ++pos;
  if (pos.ToDelta() >= str.size()) {
    VLOG(5) << "Processing #include failed: Expected closing delimiter";
    return NewError(
        LazyString{L"#include expects \"FILENAME\" or <FILENAME>, failed to "
                   L"find closing character; in line: "} +
        str);
  }

  ASSIGN_OR_RETURN(
      Path path,
      AugmentError(
          LazyString{L"#include was unable to extract path; in line: "} + str +
              LazyString{L"; error: "},
          Path::FromString(str.Substring(start, pos - start))));

  if (delimiter == '\"' && path.GetRootType() == Path::RootType::kRelative &&
      compilation.current_source_path().has_value()) {
    std::visit(
        overload{
            IgnoreErrors{},
            [&](Path source_directory) {
              path = Path::Join(source_directory, path);
            },
        },
        compilation.current_source_path()->Dirname());
  }

  CompileFile(path, compilation, parser);
  *pos_output = pos.next();
  VLOG(5) << path << ": Done compiling.";
  return Success();
}

numbers::BigInt ConsumeDecimal(const LazyString& str, ColumnNumber* pos) {
  BigInt output = numbers::BigInt::FromNumber(0);
  while (pos->ToDelta() < str.size() && isdigit(str.get(*pos))) {
    output = output * BigInt::FromNumber(10) +
             BigInt::FromNumber(str.get(*pos) - '0');
    ++(*pos);
  }
  return output;
}

void CompileLine(Compilation& compilation, void* parser,
                 const LazyString& str) {
  CHECK(compilation.errors().empty());
  ColumnNumber pos;
  int token;
  while (compilation.errors().empty() && pos.ToDelta() < str.size()) {
    compilation.SetSourceColumnInLine(pos);
    VLOG(5) << L"Compiling from character: " << std::wstring(1, str.get(pos));
    std::optional<gc::Root<Value>> input;
    switch (str.get(pos)) {
      case '/':
        if (pos.next().ToDelta() < str.size() && str.get(pos.next()) == L'/') {
          pos = ColumnNumber() + str.size();
          continue;
        } else {
          pos++;
          if (pos.ToDelta() < str.size() && str.get(pos) == L'=') {
            pos++;
            token = DIVIDE_EQ;
          } else {
            token = DIVIDE;
          }
        }
        break;

      case '!':
        pos++;
        if (pos.ToDelta() < str.size() && str.get(pos) == L'=') {
          pos++;
          token = NOT_EQUALS;
          break;
        }
        token = NOT;
        break;

      case '=':
        pos++;
        if (pos.ToDelta() < str.size() && str.get(pos) == '=') {
          pos++;
          token = EQUALS;
          break;
        }
        token = EQ;
        break;

      case '&':
        pos++;
        if (pos.ToDelta() < str.size() && str.get(pos) == '&') {
          pos++;
          token = AND;
          break;
        }
        compilation.AddError(Error(L"Unhandled character: &"));
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
        if (pos.ToDelta() < str.size() && str.get(pos) == '|') {
          pos++;
          token = OR;
          break;
        }
        compilation.AddError(Error(L"Unhandled character: |"));
        return;

      case '<':
        token = LESS_THAN;
        pos++;
        if (pos.ToDelta() < str.size() && str.get(pos) == '=') {
          pos++;
          token = LESS_OR_EQUAL;
        }
        break;

      case '>':
        token = GREATER_THAN;
        pos++;
        if (pos.ToDelta() < str.size() && str.get(pos) == '=') {
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
        if (pos.ToDelta() < str.size() && str.get(pos) == ':') {
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
          ColumnNumber start = pos;
          while (pos.ToDelta() < str.size() &&
                 (iswalnum(str.get(pos)) || str.get(pos) == '_'))
            ++pos;
          LazyString symbol_contents = str.Substring(start, pos - start);
          if (IdentifierOrError(symbol_contents) ==
              Success(IdentifierInclude()))
            HandleInclude(compilation, parser, str, &pos);
          else
            compilation.AddError(
                NewError(LazyString{L"Invalid preprocessing directive #"} +
                         symbol_contents));
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
        if (pos.ToDelta() < str.size() && str.get(pos) == '=') {
          pos++;
          token = PLUS_EQ;
        } else if (pos.ToDelta() < str.size() && str.get(pos) == '+') {
          pos++;
          token = PLUS_PLUS;
        } else {
          token = PLUS;
        }
        break;

      case '-':
        pos++;
        if (pos.ToDelta() < str.size() && str.get(pos) == '=') {
          pos++;
          token = MINUS_EQ;
        } else if (pos.ToDelta() < str.size() && str.get(pos) == '-') {
          pos++;
          token = MINUS_MINUS;
        } else {
          token = MINUS;
        }
        break;

      case '*':
        pos++;
        if (pos.ToDelta() < str.size() && str.get(pos) == '=') {
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
        token = NUMBER;
        numbers::Number value =
            numbers::Number::FromBigInt(ConsumeDecimal(str, &pos));
        if (pos.ToDelta() < str.size() &&
            (str.get(pos) == '.' || str.get(pos) == 'e')) {
          if (str.get(pos) == '.') {
            pos++;
            numbers::BigInt decimal_numerator = numbers::BigInt::FromNumber(0);
            numbers::NonZeroBigInt decimal_denominator =
                numbers::NonZeroBigInt::Constant<1>();
            while (pos.ToDelta() < str.size() && isdigit(str.get(pos))) {
              decimal_numerator *= numbers::BigInt::FromNumber(10);
              decimal_denominator *= numbers::NonZeroBigInt::Constant<10>();
              decimal_numerator +=
                  numbers::BigInt::FromNumber(str.get(pos) - L'0');
              pos++;
            }
            value += numbers::Number(true, std::move(decimal_numerator),
                                     std::move(decimal_denominator));
          }
          if (pos.ToDelta() < str.size() && str.get(pos) == 'e') {
            pos++;
            bool positive = true;
            if (pos.ToDelta() < str.size()) {
              switch (str.get(pos)) {
                case '+':
                  pos++;
                  break;
                case L'-':
                  positive = false;
                  pos++;
                  break;
              }
            }
            BigInt exponent = ConsumeDecimal(str, &pos);
            if (exponent > BigInt::FromNumber(1024)) {
              // The template type (int) doesn't matter, but we need to resolve
              // the ambiguity:
              compilation.RegisterErrors<int>(
                  Error(L"Cowardly refusing to create a number with very large "
                        L"exponent: " +
                        exponent.ToString()));
              return;
            }
            numbers::Number exponent_factor = numbers::Number::FromBigInt(
                BigInt::FromNumber(10).Pow(exponent));
            if (positive)
              value *= std::move(exponent_factor);
            else
              value /= std::move(exponent_factor);
          }
        }
        input = Value::NewNumber(compilation.pool, value);
      } break;

      case '"': {
        token = STRING;
        std::wstring output_string;
        pos++;
        for (; pos.ToDelta() < str.size(); pos++) {
          if (str.get(pos) == '"') {
            break;
          }
          if (str.get(pos) != '\\') {
            output_string.push_back(str.get(pos));
            ;
            continue;
          }
          pos++;
          if (pos.ToDelta() >= str.size()) {
            continue;
          }
          switch (str.get(pos)) {
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
              output_string.push_back(str.get(pos));
          }
        }
        if (pos.ToDelta() == str.size()) {
          compilation.AddError(Error(L"Missing terminating \" character."));
          return;
        }
        input = Value::NewString(compilation.pool,
                                 LazyString{std::move(output_string)});
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
        ColumnNumber start = pos;
        while (pos.ToDelta() < str.size() &&
               (iswalnum(str.get(pos)) || str.get(pos) == '_' ||
                str.get(pos) == '~'))
          ++pos;
        Identifier symbol =
            ValueOrDie(IdentifierOrError(str.Substring(start, pos - start)));
        struct Keyword {
          int token;
          std::function<gc::Root<Value>()> value_supplier = nullptr;
        };
        static const auto* const keywords =
            new std::unordered_map<Identifier, Keyword>(
                {{Identifier(L"true"),
                  {.token = BOOL,
                   .value_supplier =
                       [&pool = compilation.pool] {
                         return Value::NewBool(pool, true);
                       }}},
                 {Identifier(L"false"),
                  {.token = BOOL,
                   .value_supplier =
                       [&pool = compilation.pool] {
                         return Value::NewBool(pool, false);
                       }}},
                 {Identifier(L"while"), {.token = WHILE}},
                 {Identifier(L"for"), {.token = FOR}},
                 {Identifier(L"if"), {.token = IF}},
                 {Identifier(L"else"), {.token = ELSE}},
                 {Identifier(L"return"), {.token = RETURN}},
                 {Identifier(L"namespace"), {.token = NAMESPACE}},
                 {Identifier(L"class"), {.token = CLASS}}});
        if (auto it = keywords->find(symbol); it != keywords->end()) {
          token = it->second.token;
          if (auto supplier = it->second.value_supplier; supplier != nullptr)
            input = supplier();
        } else {
          token = SYMBOL;
          input = Value::NewSymbol(compilation.pool, Identifier(symbol));
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
        compilation.AddError(
            NewError(LazyString{L"Unhandled character at position: "} +
                     LazyString{std::to_wstring(pos.read())} +
                     LazyString{L" in line: "} + str));
        return;
    }
    if (token == SYMBOL || token == STRING) {
      CHECK(input.has_value()) << "No input with token: " << token;
      if (input.value().ptr()->IsSymbol())
        // TODO(easy, 2024-07-31): Find a way to avoid the call to LazyString
        // here.
        compilation.last_token =
            LazyString{input.value().ptr()->get_symbol().read()};
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
    return MergeErrors(compilation.errors(), L", ");
  }
  return language::VisitPointer(
      std::move(compilation.expr),
      &language::Success<NonNull<std::unique_ptr<Expression>>>,
      []() { return Error(L"Unexpected empty expression."); });
}
}  // namespace

ValueOrError<NonNull<std::unique_ptr<Expression>>> CompileFile(
    Path path, gc::Pool& pool, gc::Root<Environment> environment) {
  TRACK_OPERATION(vm_CompileFile);
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

}  // namespace vm
}  // namespace afc
