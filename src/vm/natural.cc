#include "src/vm/natural.h"

#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/char_buffer.h"  // For tests.
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/math/numbers.h"
#include "src/tests/tests.h"
#include "src/vm/constant_expression.h"
#include "src/vm/default_environment.h"  // For tests.
#include "src/vm/environment.h"
#include "src/vm/function_call.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::FindFirstColumnWithPredicate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::Token;

namespace afc::vm::natural {
namespace {
using ::operator<<;
using vm::operator<<;

struct Tree {
  vm::Type type;
  NonNull<std::shared_ptr<Expression>> value;
  std::vector<Tree> children = {};

  size_t DepthRightBranch() const {
    size_t output = 0;
    for (NonNull<const Tree*> tree = NonNull<const Tree*>::Unsafe(this);
         !tree->children.empty();
         tree = NonNull<const Tree*>::AddressOf(tree->children.back()))
      ++output;
    return output;
  }

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
  os << tree.type;
  for (const Tree& c : tree.children) {
    os << separator << c;
    separator = L", ";
  }
  os << "]";
  return os;
}

class ParseState {
  gc::Pool& pool_;
  const std::vector<Token>& tokens_;
  const LazyString& function_name_prefix_;
  const Environment& environment_;
  const std::vector<vm::Namespace>& search_namespaces_;

  std::vector<Tree> candidates_;

 public:
  ParseState(gc::Pool& pool, const std::vector<Token>& tokens,
             const LazyString& function_name_prefix,
             const Environment& environment,
             const std::vector<vm::Namespace>& search_namespaces)
      : pool_(pool),
        tokens_(tokens),
        function_name_prefix_(function_name_prefix),
        environment_(environment),
        search_namespaces_(search_namespaces) {}

  ValueOrError<NonNull<std::shared_ptr<Expression>>> Evaluate() {
    bool first_token = true;
    for (auto& token : tokens_) {
      VLOG(5) << "Consume token: " << token.value
              << ", candidates: " << candidates_.size();
      for (auto& c : candidates_) VLOG(6) << "Candidate: " << c;
      std::vector<Tree> extended_candidates;
      if (IsLiteralNumber(token))
        PushValue(
            Value::NewNumber(
                pool_, math::numbers::Number::FromInt64(
                           atoi(ToByteString(token.value.ToString()).c_str()))),
            extended_candidates);
      std::visit(overload{[&](Identifier identifier) {
                            for (gc::Root<Value> value : LookUp(identifier))
                              PushValue(value, extended_candidates);
                          },
                          IgnoreErrors{}},
                 first_token
                     ? Identifier::New(token.value + function_name_prefix_)
                     : Identifier::New(token.value));
      PushValue(Value::NewString(pool_, token.value), extended_candidates);
      if (extended_candidates.empty())
        return Error{LazyString{L"No valid parses found."}};
      first_token = false;
      candidates_ = std::move(extended_candidates);
      for (auto& c : candidates_) VLOG(6) << "Extended Candidate: " << c;
    }

    std::vector<std::shared_ptr<Expression>> valid_outputs =
        container::MaterializeVector(
            candidates_ |
            std::views::transform(
                [this](Tree& tree) -> std::shared_ptr<Expression> {
                  return CompileTree(tree);
                }) |
            std::views::filter([](const std::shared_ptr<Expression>& value) {
              return value != nullptr;
            }));
    LOG(INFO) << "Natural results: " << valid_outputs.size();
    if (valid_outputs.empty())
      return Error{LazyString{L"No valid parses found (post compilation)."}};

    // Safe because we've dropped null values above.
    return NonNull<std::shared_ptr<Expression>>::Unsafe(valid_outputs.front());
  }

 private:
  std::shared_ptr<Expression> CompileTree(const Tree& tree) {
    const types::Function* function_type =
        std::get_if<types::Function>(&tree.type);
    if (function_type == nullptr) return tree.value.get_shared();
    std::vector<std::shared_ptr<Expression>> children_arguments =
        container::MaterializeVector(
            tree.children | std::views::transform([this](const Tree& argument) {
              return CompileTree(argument);
            }));

    if (std::ranges::any_of(children_arguments,
                            [](auto& value) { return value == nullptr; }))
      return nullptr;

    while (children_arguments.size() < function_type->inputs.size()) {
      if (std::holds_alternative<types::String>(
              function_type->inputs[children_arguments.size()]))
        children_arguments.push_back(
            NewConstantExpression(Value::NewString(pool_, LazyString{}))
                .get_unique());
      else
        return nullptr;
    }
    return NewFunctionCall(
               tree.value,
               container::MaterializeVector(
                   std::move(children_arguments) |
                   std::views::transform([](std::shared_ptr<Expression> value) {
                     return NonNull<std::shared_ptr<Expression>>::Unsafe(
                         std::move(value));
                   })))
        .get_unique();
  }

  void PushValue(gc::Root<Value> value_root, std::vector<Tree>& output) const {
    vm::Type type = value_root.ptr()->type;
    VLOG(8) << "Receive value type: " << type;
    NonNull<std::shared_ptr<Expression>> value =
        NewConstantExpression(std::move(value_root));
    CHECK(!value->Types().empty());
    if (candidates_.empty())
      output.push_back(Tree{.type = value->Types().front(), .value = value});
    else
      for (const Tree& tree : candidates_)
        ExtendTree(type, value, tree, output);
  }

  static void ExtendTree(const vm::Type& type,
                         const NonNull<std::shared_ptr<Expression>>& value,
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
  static std::optional<Tree> Insert(
      const vm::Type& type, const NonNull<std::shared_ptr<Expression>>& value,
      Tree tree, size_t insertion_depth) {
    Tree& parent_tree = tree.RightBranchTreeAtDepth(insertion_depth);
    const types::Function* parent_function_type =
        std::get_if<types::Function>(&parent_tree.type);
    VLOG(7) << "Attempt insert at depth " << insertion_depth
            << " to parent_tree.type:" << parent_tree.type;
    if (parent_function_type == nullptr ||
        parent_function_type->inputs.size() <= parent_tree.children.size())
      return std::nullopt;
    const types::Function* value_function_type =
        std::get_if<types::Function>(&type);
    if (parent_function_type->inputs[parent_tree.children.size()] == type ||
        (value_function_type != nullptr &&
         parent_function_type->inputs[parent_tree.children.size()] ==
             value_function_type->output.get())) {
      parent_tree.children.push_back(Tree{.type = type, .value = value});
      VLOG(8) << "Insert: " << type << " at " << insertion_depth;
      return tree;
    }
    return std::nullopt;
  }

  static bool IsQuotedString(const Token& token) { return token.has_quotes; }

  static bool IsLiteralNumber(const Token& token) {
    CHECK(!token.value.IsEmpty());
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

ValueOrError<NonNull<std::shared_ptr<Expression>>> CompileTokens(
    const std::vector<Token>& tokens, const LazyString& function_name_prefix,
    const Environment& environment,
    const std::vector<vm::Namespace>& search_namespaces, gc::Pool& pool) {
  return ParseState(pool, tokens, function_name_prefix, environment,
                    search_namespaces)
      .Evaluate();
}
}  // namespace

language::ValueOrError<language::NonNull<std::shared_ptr<Expression>>> Compile(
    const LazyString& input, const LazyString& function_name_prefix,
    const Environment& environment,
    const std::vector<vm::Namespace>& search_namespaces, gc::Pool& pool) {
  return CompileTokens(TokenizeBySpaces(input), function_name_prefix,
                       environment, search_namespaces, pool);
}

namespace {
using ::operator<<;
using afc::language::operator<<;
static const vm::Namespace kEmptyNamespace;
bool tests_registration = tests::Register(
    L"vm::natural",
    std::vector<tests::Test>{
        {.name = L"SimpleString",
         .callback =
             [] {
               gc::Pool pool({});
               language::gc::Root<Environment> environment =
                   afc::vm::NewDefaultEnvironment(pool);
               NonNull<std::shared_ptr<Expression>> expression = ValueOrDie(
                   Compile(LazyString{L"\"foo\""}, LazyString{},
                           environment.ptr().value(), {kEmptyNamespace}, pool));
               CHECK(ValueOrDie(Evaluate(expression, pool, environment, nullptr)
                                    .Get()
                                    .value())
                         .ptr()
                         ->get_string() == LazyString{L"foo"});
             }},
        {.name = L"FunctionNoArguments",
         .callback =
             [] {
               gc::Pool pool({});
               language::gc::Root<Environment> environment =
                   afc::vm::NewDefaultEnvironment(pool);
               environment.ptr()->Define(
                   Identifier{LazyString{L"SomeFunction"}},
                   vm::NewCallback(pool, kPurityTypePure,
                                   []() -> std::wstring { return L"quux"; }));
               NonNull<std::shared_ptr<Expression>> expression = ValueOrDie(
                   Compile(LazyString{L"SomeFunction"}, LazyString{},
                           environment.ptr().value(), {kEmptyNamespace}, pool));
               CHECK(ValueOrDie(Evaluate(expression, pool, environment, nullptr)
                                    .Get()
                                    .value())
                         .ptr()
                         ->get_string() == LazyString{L"quux"});
             }},
        {.name = L"MissingArguments",
         .callback =
             [] {
               gc::Pool pool({});
               language::gc::Root<Environment> environment =
                   afc::vm::NewDefaultEnvironment(pool);
               environment.ptr()->Define(
                   Identifier{LazyString{L"Moo"}},
                   vm::NewCallback(pool, kPurityTypePure,
                                   [](std::wstring a, std::wstring b,
                                      std::wstring c) -> std::wstring {
                                     return L"{" + a + L"," + b + L"," + c +
                                            L"}";
                                   }));
               NonNull<std::shared_ptr<Expression>> expression = ValueOrDie(
                   Compile(LazyString{L"Moo Moo"}, LazyString{},
                           environment.ptr().value(), {kEmptyNamespace}, pool));
               LOG(INFO) << "Evaluating.";
               CHECK_EQ(
                   ValueOrDie(Evaluate(expression, pool, environment, nullptr)
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
                   Identifier{LazyString{L"UnaryFunction"}},
                   vm::NewCallback(pool, kPurityTypePure,
                                   [](std::wstring a) -> std::wstring {
                                     CHECK(a == L"bar");
                                     return L"quux";
                                   }));
               NonNull<std::shared_ptr<Expression>> expression = ValueOrDie(
                   Compile(LazyString{L"UnaryFunction bar"}, LazyString{},
                           environment.ptr().value(), {kEmptyNamespace}, pool));
               CHECK(ValueOrDie(Evaluate(expression, pool, environment, nullptr)
                                    .Get()
                                    .value())
                         .ptr()
                         ->get_string() == LazyString{L"quux"});
             }},
        {.name = L"UnaryFunctionUnquotedArgumentWithDot",
         .callback =
             [] {
               gc::Pool pool({});
               language::gc::Root<Environment> environment =
                   afc::vm::NewDefaultEnvironment(pool);
               environment.ptr()->Define(
                   Identifier{LazyString{L"UnaryFunction"}},
                   vm::NewCallback(pool, kPurityTypePure,
                                   [](std::wstring a) -> std::wstring {
                                     CHECK(a == L"ba.r");
                                     return L"quux";
                                   }));
               NonNull<std::shared_ptr<Expression>> expression = ValueOrDie(
                   Compile(LazyString{L"UnaryFunction ba.r"}, LazyString{},
                           environment.ptr().value(), {kEmptyNamespace}, pool));
               CHECK(ValueOrDie(Evaluate(expression, pool, environment, nullptr)
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
                   Identifier{LazyString{L"SomeFunction"}},
                   vm::NewCallback(
                       pool, kPurityTypePure,
                       [](std::wstring a, std::wstring b) -> std::wstring {
                         CHECK(a == L"bar");
                         CHECK(b == L"foo");
                         return L"quux";
                       }));
               NonNull<std::shared_ptr<Expression>> expression = ValueOrDie(
                   Compile(LazyString{L"SomeFunction \"bar\" \"foo\""},
                           LazyString{}, environment.ptr().value(),
                           {kEmptyNamespace}, pool));
               CHECK(ValueOrDie(Evaluate(expression, pool, environment, nullptr)
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
               Identifier{LazyString{L"foo"}},
               vm::NewCallback(pool, kPurityTypePure,
                               [&calls](std::wstring a) -> std::wstring {
                                 calls++;
                                 return L"[" + a + L"]";
                               }));
           NonNull<std::shared_ptr<Expression>> expression = ValueOrDie(
               Compile(LazyString{L"foo foo foo \"bar\" "}, LazyString{},
                       environment.ptr().value(), {kEmptyNamespace}, pool));
           CHECK_EQ(ValueOrDie(Evaluate(expression, pool, environment, nullptr)
                                   .Get()
                                   .value())
                        .ptr()
                        ->get_string(),
                    LazyString{L"[[[bar]]]"});
           CHECK_EQ(calls, 3ul);
         }}});
}  // namespace
}  // namespace afc::vm::natural
