#include "src/vm/natural.h"

#include "src/language/container.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/wstring.h"
#include "src/math/numbers.h"
#include "src/vm/constant_expression.h"
#include "src/vm/environment.h"
#include "src/vm/function_call.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::lazy_string::Token;

namespace afc::vm::natural {
namespace {
struct Tree {
  vm::Type type;
  NonNull<std::shared_ptr<Expression>> value;
  std::vector<NonNull<std::shared_ptr<Tree>>> children = {};

  size_t DepthRightBranch() const {
    size_t output = 0;
    for (NonNull<const Tree*> tree = NonNull<const Tree*>::Unsafe(this);
         !tree->children.empty(); tree = tree->children.back().get())
      ++output;
    return output;
  }

  Tree& RightBranchTreeAtDepth(size_t depth) {
    for (NonNull<Tree*> tree = NonNull<Tree*>::Unsafe(this);
         !tree->children.empty(); tree = tree->children.back().get())
      if (depth == 0)
        return tree.value();
      else
        depth--;
    LOG(FATAL) << "Invalid depth.";
    return *this;
  }
};

class ParseState {
  gc::Pool& pool_;
  const std::vector<Token>& tokens_;
  const Environment& environment_;

  std::vector<Tree> candidates_;

 public:
  ParseState(gc::Pool& pool, const std::vector<Token>& tokens,
             const Environment& environment)
      : pool_(pool), tokens_(tokens), environment_(environment) {}

  ValueOrError<NonNull<std::shared_ptr<Expression>>> Evaluate() {
    for (auto& token : tokens_) {
      if (IsQuotedString(token))
        ReceiveValue(Value::NewString(pool_, token.value));
      else if (IsLiteralNumber(token))
        ReceiveValue(Value::NewNumber(
            pool_,
            math::numbers::FromInt(atoi(ToByteString(token.value).c_str()))));
      for (gc::Root<Value> value : LookUp(token)) ReceiveValue(value);
    }

    if (candidates_.empty()) return Error(L"No valid parses found.");

    std::vector<std::shared_ptr<Expression>> valid_outputs =
        container::MaterializeVector(
            candidates_ |
            std::views::transform(
                [](Tree& tree) -> std::shared_ptr<Expression> {
                  return CompileTree(tree);
                }) |
            std::views::filter([](const std::shared_ptr<Expression>& value) {
              return value != nullptr;
            }));
    if (valid_outputs.size() > 1) return Error(L"Ambiguous parses found.");
    if (valid_outputs.empty())
      return Error(L"No valid parses found (post compilation).");

    // Safe because we've dropped null values above.
    return NonNull<std::shared_ptr<Expression>>::Unsafe(valid_outputs.front());
  }

 private:
  static std::shared_ptr<Expression> CompileTree(Tree& tree) {
    types::Function* function_type = std::get_if<types::Function>(&tree.type);
    if (function_type == nullptr) return tree.value.get_shared();

    std::vector<std::shared_ptr<Expression>> children_arguments =
        container::MaterializeVector(
            tree.children |
            std::views::transform(
                [](const NonNull<std::shared_ptr<Tree>>& argument) {
                  return CompileTree(argument.value());
                }));
    if (std::ranges::any_of(children_arguments,
                            [](auto& value) { return value == nullptr; }))
      return nullptr;

    return NewFunctionCall(
               std::move(tree.value),
               container::MaterializeVector(
                   std::move(children_arguments) |
                   std::views::transform(
                       [](std::shared_ptr<Expression>& value) {
                         return NonNull<std::shared_ptr<Expression>>::Unsafe(
                             std::move(value));
                       })))
        .get_unique();
  }

  void ReceiveValue(gc::Root<Value> value_root) {
    vm::Type type = value_root.ptr()->type;
    NonNull<std::shared_ptr<Expression>> value =
        NewConstantExpression(std::move(value_root));
    for (auto& tree : std::exchange(candidates_, std::vector<Tree>{}))
      ExtendTree(type, value, tree, 0);
  }

  void ExtendTree(const vm::Type& type,
                  const NonNull<std::shared_ptr<Expression>>& value, Tree tree,
                  size_t insertion_depth) {
    if (insertion_depth >= tree.DepthRightBranch()) return;
    ExtendTree(type, value, tree, insertion_depth + 1);
    if (Insert(type, value, tree, insertion_depth)) candidates_.push_back(tree);
  }

  // insertion_depth is the depth of the parent to which we'll add `value` as a
  // child.
  bool Insert(const vm::Type& type,
              const NonNull<std::shared_ptr<Expression>>& value, Tree& tree,
              size_t insertion_depth) {
    Tree& parent_tree = tree.RightBranchTreeAtDepth(insertion_depth);
    types::Function* function_type =
        std::get_if<types::Function>(&parent_tree.type);
    if (function_type == nullptr ||
        function_type->inputs.size() <= parent_tree.children.size() ||
        function_type->inputs[parent_tree.children.size()] != type)
      return false;
    parent_tree.children.push_back(
        MakeNonNullUnique<Tree>(Tree{.type = type, .value = value}));
    return true;
  }

  static bool IsQuotedString(const Token& token) { return token.has_quotes; }

  static bool IsLiteralNumber(const Token& token) {
    CHECK(!token.value.empty());
    // TODO(2023-12-15, trivial): Handle `-` and `.`.
    return std::ranges::all_of(token.value,
                               [](wchar_t c) { return std::iswdigit(c); });
  }

  std::vector<gc::Root<Value>> LookUp(const Token& token) {
    static const vm::Namespace kEmptyNamespace;
    std::vector<language::gc::Root<Value>> output;
    environment_.PolyLookup(kEmptyNamespace, token.value, &output);
    return output;
  }
};

ValueOrError<NonNull<std::shared_ptr<Expression>>> Compile(
    const std::vector<Token>& tokens, gc::Ptr<Environment> environment) {
  return ParseState(environment.pool(), tokens, environment.value()).Evaluate();
}
}  // namespace

language::ValueOrError<language::NonNull<std::shared_ptr<Expression>>> Compile(
    const language::lazy_string::LazyString& input,
    language::gc::Ptr<Environment> environment) {
  return Compile(TokenizeBySpaces(input), environment);
}

}  // namespace afc::vm::natural
