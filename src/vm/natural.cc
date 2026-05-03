#include "src/vm/natural.h"

#include <algorithm>
#include <ranges>

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/char_buffer.h"  // For tests.
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/math/numbers.h"
#include "src/tests/tests.h"
#include "src/vm/constant_expression.h"
#include "src/vm/default_environment.h"  // For tests.
#include "src/vm/delegating_expression.h"
#include "src/vm/environment.h"
#include "src/vm/function_call.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::FindFirstColumnWithPredicate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;

namespace afc::vm::natural {
namespace {
using ::operator<<;
using afc::vm::operator<<;

struct Tree {
  gc::Root<vm::Value> value;
  std::vector<Tree> children = {};

  // How many times can we descend down the right-most child?
  size_t DepthRightBranch() const {
    size_t output = 0;
    for (NonNull<const Tree*> tree = NonNull<const Tree*>::Unsafe(this);
         !tree->children.empty();
         tree = NonNull<const Tree*>::AddressOf(tree->children.back()))
      ++output;
    return output;
  }

  // Go down always choosing the right-most child `depth` times, return the
  // matching tree.
  Tree& RightBranchTreeAtDepth(size_t depth) {
    VLOG(5) << "Getting right branch at depth: " << depth;
    NonNull<Tree*> tree = NonNull<Tree*>::Unsafe(this);
    while (depth > 0) {
      CHECK(!tree->children.empty()) << "Invalid depth.";
      tree = NonNull<Tree*>::AddressOf(tree->children.back());
      --depth;
    }
    return tree.value();
  }
};

std::ostream& operator<<(std::ostream& os, const Tree& tree) {
  std::wstring separator = L"";
  os << L"[";
  os << tree.value.value();
  for (const Tree& c : tree.children) {
    os << separator << c;
    separator = L", ";
  }
  os << "]";
  return os;
}

class ParseState {
  gc::Pool& pool_;
  const SingleLine& input_;
  const std::vector<Token>& tokens_;
  const SingleLine& function_name_prefix_;
  const Environment& environment_;
  const std::vector<vm::Namespace>& search_namespaces_;

  std::vector<Tree> candidates_;

 public:
  ParseState(gc::Pool& pool, const SingleLine& input,
             const std::vector<Token>& tokens,
             const SingleLine& function_name_prefix,
             const Environment& environment,
             const std::vector<vm::Namespace>& search_namespaces)
      : pool_(pool),
        input_(input),
        tokens_(tokens),
        function_name_prefix_(function_name_prefix),
        environment_(environment),
        search_namespaces_(search_namespaces) {}

  ValueOrError<gc::Root<Expression>> Evaluate() {
    std::vector<Tree> early_found_candidates;
    for (const auto& [index, token] : tokens_ | std::views::enumerate) {
      VLOG(5) << "Consume token: " << token.value
              << ", candidates: " << candidates_.size();
      for (auto& c : candidates_) VLOG(6) << "Candidate: " << c;
      std::vector<Tree> extended_candidates;
      if (IsLiteralNumber(token))
        PushValue(Value::NewNumber(pool_, math::numbers::Number::FromInt64(atoi(
                                              token.value.ToBytes().c_str())))
                      .ptr(),
                  extended_candidates);
      VisitValue(index == 0
                     ? Identifier::New(token.value + function_name_prefix_)
                     : Identifier::New(token.value),
                 [&](Identifier identifier) {
                   for (gc::Root<Value> value : LookUp(identifier))
                     PushValue(value.ptr(), extended_candidates);
                 });
      PushValue(Value::NewString(pool_, ToLazyString(token.value)).ptr(),
                extended_candidates);

      // Try feeding the rest of the input as a string.
      if (index > 0) {
        LazyString arg = ToLazyString(input_.Substring(token.begin));
        VLOG(7) << "Trying early value with: " << arg;
        PushValue(Value::NewString(pool_, arg).ptr(), early_found_candidates);
      }

      candidates_ = std::move(extended_candidates);

      for (auto& c : candidates_) VLOG(6) << "Extended Candidate: " << c;
      for (auto& c : early_found_candidates)
        VLOG(6) << "Early Candidate: " << c;

      if (candidates_.empty()) {
        if (early_found_candidates.empty())
          return Error{LazyString{L"No valid parses found."}};
        else {
          break;
        }
      }
    }

    std::ranges::copy(early_found_candidates, std::back_inserter(candidates_));
    std::vector<std::optional<gc::Root<Expression>>> valid_outputs =
        candidates_ |
        std::views::transform(
            [this](Tree& tree) -> std::optional<gc::Root<Expression>> {
              std::optional<gc::Root<Expression>> output = CompileTree(tree);
              VLOG(5) << "Found value tree: " << tree;
              return output;
            }) |
        std::views::filter(
            [](const std::optional<gc::Root<Expression>>& candidate) {
              return candidate.has_value();
            }) |
        std::ranges::to<std::vector>();
    LOG(INFO) << "Natural results: " << valid_outputs.size();
    if (valid_outputs.empty())
      return Error{LazyString{L"No valid parses found (post compilation)."}};

    // Safe because we've dropped null values above.
    return valid_outputs.front().value();
  }

 private:
  std::optional<gc::Root<Expression>> CompileTree(const Tree& tree) {
    if (!tree.value->IsFunction())
      return NewConstantExpression(tree.value.ptr());
    std::vector<std::optional<gc::Root<Expression>>> children_arguments =
        tree.children | std::views::transform([this](const Tree& argument) {
          return CompileTree(argument);
        }) |
        std::ranges::to<std::vector>();

    if (std::ranges::any_of(children_arguments,
                            [](auto& value) { return value == std::nullopt; }))
      return std::nullopt;

    const types::Function function_type =
        std::get<types::Function>(tree.value->type());
    while (children_arguments.size() < function_type.inputs.size()) {
      if (std::holds_alternative<types::String>(
              function_type.inputs[children_arguments.size()]))
        children_arguments.push_back(
            NewConstantExpression(Value::NewString(pool_, LazyString{}).ptr()));
      else
        return std::nullopt;
    }
    return NewFunctionCall(
        NewConstantExpression(tree.value.ptr()).ptr(),
        children_arguments |
            std::views::transform(
                [](std::optional<gc::Root<Expression>>& expr) {
                  return expr->ptr();
                }) |
            std::ranges::to<std::vector>());
  }

  void PushValue(gc::Ptr<Value> value, std::vector<Tree>& output) const {
    vm::Type type = value->type();
    VLOG(8) << "Receive value type: " << type;
    if (candidates_.empty())
      output.push_back(Tree{.value = value.ToRoot()});
    else
      for (const Tree& tree : candidates_)
        ExtendTree(type, value, tree, output);
  }

  static void ExtendTree(const vm::Type& type, const gc::Ptr<vm::Value>& value,
                         Tree tree, std::vector<Tree>& output) {
    for (size_t child_insertion_depth = tree.DepthRightBranch() + 1;
         child_insertion_depth > 0; --child_insertion_depth)
      if (std::optional<Tree> new_tree =
              Insert(type, value, tree, child_insertion_depth - 1);
          new_tree.has_value())
        output.push_back(new_tree.value());
  }

  // insertion_depth is the depth of the parent to which we'll add `value` as a
  // child.
  static std::optional<Tree> Insert(const vm::Type& type,
                                    const gc::Ptr<Value>& value, Tree tree,
                                    size_t insertion_depth) {
    Tree& parent_tree = tree.RightBranchTreeAtDepth(insertion_depth);
    const types::Function* parent_function_type =
        std::get_if<types::Function>(&parent_tree.value->type());
    VLOG(7) << "Attempt insert at depth " << insertion_depth
            << " to parent_tree:" << parent_tree.value.value();
    if (parent_function_type == nullptr ||
        parent_function_type->inputs.size() <= parent_tree.children.size())
      return std::nullopt;
    const types::Function* value_function_type =
        std::get_if<types::Function>(&type);
    if (parent_function_type->inputs[parent_tree.children.size()] == type ||
        (value_function_type != nullptr &&
         parent_function_type->inputs[parent_tree.children.size()] ==
             value_function_type->output.get())) {
      parent_tree.children.push_back(Tree{.value = value.ToRoot()});
      VLOG(8) << "Insert: " << type << " at " << insertion_depth;
      return tree;
    }
    return std::nullopt;
  }

  static bool IsQuotedString(const Token& token) { return token.has_quotes; }

  static bool IsLiteralNumber(const Token& token) {
    CHECK(!token.value.empty());
    // TODO(2023-12-15, trivial): Handle `-` and `.`.
    return FindFirstColumnWithPredicate(token.value,
                                        [](ColumnNumber, wchar_t c) {
                                          return !std::iswdigit(c);
                                        }) == std::nullopt;
  }

  std::vector<gc::Root<Value>> LookUp(const Identifier& symbol) {
    std::vector<language::gc::Root<Value>> output;
    for (auto& search_namespace : search_namespaces_)
      environment_.CaseInsensitiveLookup(search_namespace, symbol, &output);
    return output;
  }
};

ValueOrError<gc::Root<Expression>> CompileTokens(
    const SingleLine& input, const std::vector<Token>& tokens,
    const SingleLine& function_name_prefix, const Environment& environment,
    const std::vector<vm::Namespace>& search_namespaces, gc::Pool& pool) {
  return ParseState(pool, input, tokens, function_name_prefix, environment,
                    search_namespaces)
      .Evaluate();
}
}  // namespace

language::ValueOrError<gc::Root<Expression>> Compile(
    const SingleLine& input, const SingleLine& function_name_prefix,
    const Environment& environment,
    const std::vector<vm::Namespace>& search_namespaces, gc::Pool& pool) {
  return CompileTokens(input, TokenizeBySpaces(input), function_name_prefix,
                       environment, search_namespaces, pool);
}

namespace {
using ::operator<<;
using afc::language::operator<<;
static const vm::Namespace kEmptyNamespace;
bool tests_registration = tests::Register(
    L"vm::natural", std::invoke([] {
      struct Expectations {
        std::optional<std::wstring> unary_argument;
      };
      auto test = [](std::wstring name, std::wstring input,
                     Expectations expectations) {
        return tests::Test{
            .name = name, .callback = [input, expectations] {
              gc::Pool pool({});
              language::gc::Root<Environment> environment =
                  afc::vm::NewDefaultEnvironment(pool);
              environment.ptr()->Define(
                  Identifier{NonEmptySingleLine{
                      SingleLine{LazyString{L"UnaryFunction"}}}},
                  vm::NewCallback(
                      pool, kPurityTypePure,
                      [expectations](std::wstring a) -> std::wstring {
                        CHECK_EQ(
                            LazyString{a},
                            LazyString{expectations.unary_argument.value()});
                        return L"quux";
                      }));
              gc::Root<Expression> expression = ValueOrDie(
                  Compile(SingleLine{LazyString{input}}, SingleLine{},
                          environment.ptr().value(), {kEmptyNamespace}, pool));
              CHECK(ValueOrDie(
                        Evaluate(expression.ptr(), environment.ptr(), nullptr)
                            .Get()
                            .value())
                        .ptr()
                        ->get_string() == LazyString{L"quux"});
            }};
      };
      return std::vector<tests::Test>{
          {.name = L"SimpleString",
           .callback =
               [] {
                 gc::Pool pool({});
                 language::gc::Root<Environment> environment =
                     afc::vm::NewDefaultEnvironment(pool);
                 gc::Root<Expression> expression = ValueOrDie(Compile(
                     SingleLine{LazyString{L"\"foo\""}}, SingleLine{},
                     environment.ptr().value(), {kEmptyNamespace}, pool));
                 CHECK_EQ(ValueOrDie(Evaluate(expression.ptr(),
                                              environment.ptr(), nullptr)
                                         .Get()
                                         .value())
                              .ptr()
                              ->get_string(),
                          LazyString{L"foo"});
               }},
          {.name = L"FunctionNoArguments",
           .callback =
               [] {
                 gc::Pool pool({});
                 language::gc::Root<Environment> environment =
                     afc::vm::NewDefaultEnvironment(pool);
                 environment.ptr()->Define(
                     Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"SomeFunction"}}}},
                     vm::NewCallback(pool, kPurityTypePure,
                                     []() -> std::wstring { return L"quux"; }));
                 gc::Root<Expression> expression = ValueOrDie(Compile(
                     SingleLine{LazyString{L"SomeFunction"}}, SingleLine{},
                     environment.ptr().value(), {kEmptyNamespace}, pool));
                 CHECK_EQ(ValueOrDie(Evaluate(expression.ptr(),
                                              environment.ptr(), nullptr)
                                         .Get()
                                         .value())
                              .ptr()
                              ->get_string(),
                          LazyString{L"quux"});
               }},
          {.name = L"MissingArguments",
           .callback =
               [] {
                 gc::Pool pool({});
                 language::gc::Root<Environment> environment =
                     afc::vm::NewDefaultEnvironment(pool);
                 environment.ptr()->Define(
                     Identifier{
                         NonEmptySingleLine{SingleLine{LazyString{L"Moo"}}}},
                     vm::NewCallback(pool, kPurityTypePure,
                                     [](std::wstring a, std::wstring b,
                                        std::wstring c) -> std::wstring {
                                       return L"{" + a + L"," + b + L"," + c +
                                              L"}";
                                     }));
                 gc::Root<Expression> expression = ValueOrDie(Compile(
                     SingleLine{LazyString{L"Moo Moo"}}, SingleLine{},
                     environment.ptr().value(), {kEmptyNamespace}, pool));
                 LOG(INFO) << "Evaluating.";
                 CHECK_EQ(ValueOrDie(Evaluate(expression.ptr(),
                                              environment.ptr(), nullptr)
                                         .Get()
                                         .value())
                              .ptr()
                              ->get_string(),
                          LazyString{L"{{,,},,}"});
               }},
          {.name = L"UnaryFunctionUnquotedArgument",
           .callback =
               [] {
                 gc::Pool pool({});
                 language::gc::Root<Environment> environment =
                     afc::vm::NewDefaultEnvironment(pool);
                 environment.ptr()->Define(
                     Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"UnaryFunction"}}}},
                     vm::NewCallback(pool, kPurityTypePure,
                                     [](std::wstring a) -> std::wstring {
                                       CHECK(a == L"bar");
                                       return L"quux";
                                     }));
                 gc::Root<Expression> expression = ValueOrDie(Compile(
                     SingleLine{LazyString{L"UnaryFunction bar"}}, SingleLine{},
                     environment.ptr().value(), {kEmptyNamespace}, pool));
                 CHECK(ValueOrDie(Evaluate(expression.ptr(), environment.ptr(),
                                           nullptr)
                                      .Get()
                                      .value())
                           .ptr()
                           ->get_string() == LazyString{L"quux"});
               }},
          {.name = L"UnaryFunctionSwallowTail",
           .callback =
               [] {
                 gc::Pool pool({});
                 language::gc::Root<Environment> environment =
                     afc::vm::NewDefaultEnvironment(pool);
                 environment.ptr()->Define(
                     Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"UnaryFunction"}}}},
                     vm::NewCallback(pool, kPurityTypePure,
                                     [](std::wstring a) -> std::wstring {
                                       CHECK_EQ(LazyString{a},
                                                LazyString{L"bar \"foo\" meh"});
                                       return L"quux";
                                     }));
                 gc::Root<Expression> expression = ValueOrDie(Compile(
                     SingleLine{LazyString{L"UnaryFunction bar \"foo\" meh"}},
                     SingleLine{}, environment.ptr().value(), {kEmptyNamespace},
                     pool));
                 CHECK(ValueOrDie(Evaluate(expression.ptr(), environment.ptr(),
                                           nullptr)
                                      .Get()
                                      .value())
                           .ptr()
                           ->get_string() == LazyString{L"quux"});
               }},
          test(L"UnaryFunctionSwallowTailQuotes", L"UnaryFunction bar=\"foo\"",
               Expectations{.unary_argument = L"bar=\"foo\""}),
          {.name = L"UnaryFunctionUnquotedArgumentWithDot",
           .callback =
               [] {
                 gc::Pool pool({});
                 language::gc::Root<Environment> environment =
                     afc::vm::NewDefaultEnvironment(pool);
                 environment.ptr()->Define(
                     Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"UnaryFunction"}}}},
                     vm::NewCallback(pool, kPurityTypePure,
                                     [](std::wstring a) -> std::wstring {
                                       CHECK(a == L"ba.r");
                                       return L"quux";
                                     }));
                 gc::Root<Expression> expression = ValueOrDie(
                     Compile(SingleLine{LazyString{L"UnaryFunction ba.r"}},
                             SingleLine{}, environment.ptr().value(),
                             {kEmptyNamespace}, pool));
                 CHECK(ValueOrDie(Evaluate(expression.ptr(), environment.ptr(),
                                           nullptr)
                                      .Get()
                                      .value())
                           .ptr()
                           ->get_string() == LazyString{L"quux"});
               }},
          {.name = L"SimpleFunctionTwoArguments",
           .callback =
               [] {
                 gc::Pool pool({});
                 language::gc::Root<Environment> environment =
                     afc::vm::NewDefaultEnvironment(pool);
                 environment.ptr()->Define(
                     Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"SomeFunction"}}}},
                     vm::NewCallback(
                         pool, kPurityTypePure,
                         [](std::wstring a, std::wstring b) -> std::wstring {
                           CHECK(a == L"bar");
                           CHECK(b == L"foo");
                           return L"quux";
                         }));
                 gc::Root<Expression> expression = ValueOrDie(Compile(
                     SingleLine{LazyString{L"SomeFunction \"bar\" \"foo\""}},
                     SingleLine{}, environment.ptr().value(), {kEmptyNamespace},
                     pool));
                 CHECK(ValueOrDie(Evaluate(expression.ptr(), environment.ptr(),
                                           nullptr)
                                      .Get()
                                      .value())
                           .ptr()
                           ->get_string() == LazyString{L"quux"});
               }},
          {.name = L"NestingFunctions", .callback = [] {
             gc::Pool pool({});
             language::gc::Root<Environment> environment =
                 afc::vm::NewDefaultEnvironment(pool);
             size_t calls = 0;
             environment.ptr()->Define(
                 Identifier{NonEmptySingleLine{SingleLine{LazyString{L"foo"}}}},
                 vm::NewCallback(pool, kPurityTypePure,
                                 [&calls](std::wstring a) -> std::wstring {
                                   calls++;
                                   return L"[" + a + L"]";
                                 }));
             gc::Root<Expression> expression = ValueOrDie(Compile(
                 SingleLine{LazyString{L"foo foo foo \"bar\" "}}, SingleLine{},
                 environment.ptr().value(), {kEmptyNamespace}, pool));
             CHECK_EQ(ValueOrDie(
                          Evaluate(expression.ptr(), environment.ptr(), nullptr)
                              .Get()
                              .value())
                          .ptr()
                          ->get_string(),
                      LazyString{L"[[[bar]]]"});
             CHECK_EQ(calls, 3ul);
           }}};
    }));
}  // namespace
}  // namespace afc::vm::natural
