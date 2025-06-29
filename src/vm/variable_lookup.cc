#include "variable_lookup.h"

#include <glog/logging.h>

#include <unordered_set>

#include "compilation.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
using afc::language::lazy_string::ToLazyString;
namespace {

class VariableLookup : public Expression {
  const Namespace symbol_namespace_;
  const Identifier symbol_;
  const std::vector<Type> types_;

 public:
  VariableLookup(Namespace symbol_namespace, Identifier symbol,
                 std::vector<Type> types)
      : symbol_namespace_(std::move(symbol_namespace)),
        symbol_(std::move(symbol)),
        types_(types) {}

  std::vector<Type> Types() override { return types_; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType{}; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    TRACK_OPERATION(vm_VariableLookup_Evaluate);
    // TODO: Enable this logging.
    // DVLOG(5) << "Look up symbol: " << symbol_;
    return futures::Past(VisitOptional(
        [](Environment::LookupResult lookup_result) {
          DVLOG(5) << "Variable lookup: "
                   << std::get<gc::Root<Value>>(lookup_result.value).value();
          return Success(EvaluationOutput::New(
              std::get<gc::Root<Value>>(lookup_result.value)));
        },
        [this] {
          return Error{LazyString{L"Unexpected: variable value is null: "} +
                       ToLazyString(symbol_) + LazyString{L"."}};
        },
        trampoline.environment().ptr()->Lookup(symbol_namespace_, symbol_,
                                               type)));
  }
};

class StackFrameLookup : public Expression {
  const size_t index_;
  const Type type_;
  const Identifier identifier_;

 public:
  StackFrameLookup(size_t index, Type type, Identifier identifier)
      : index_(index), type_(type), identifier_(identifier) {}
  std::vector<Type> Types() override { return {type_}; }
  std::unordered_set<Type> ReturnTypes() const override { return {}; }

  PurityType purity() override { return PurityType{}; }

  futures::ValueOrError<EvaluationOutput> Evaluate(Trampoline& trampoline,
                                                   const Type& type) override {
    TRACK_OPERATION(vm_StackFrameLookup_Evaluate);
    CHECK(type == type_);
    return futures::Past(Success(EvaluationOutput::New(
        trampoline.stack().current_frame().get(index_).ToRoot())));
  }
};

}  // namespace

std::unique_ptr<Expression> NewVariableLookup(Compilation& compilation,
                                              std::list<Identifier> symbols) {
  CHECK(!symbols.empty());

  auto symbol = std::move(symbols.back());
  symbols.pop_back();
  Namespace symbol_namespace(
      std::vector<Identifier>(symbols.begin(), symbols.end()));

  if (std::optional<std::reference_wrapper<StackFrameHeader>> header =
          compilation.CurrentStackFrameHeader();
      header.has_value() && symbol_namespace.empty())
    if (std::optional<std::pair<size_t, Type>> argument_data =
            header->get().Find(symbol);
        argument_data.has_value())
      return std::make_unique<StackFrameLookup>(argument_data->first,
                                                argument_data->second, symbol);

  // We don't need to switch namespaces (i.e., we can use
  // `compilation->environment` directly) because during compilation, we know
  // that we'll be in the right environment.
  std::vector<Environment::LookupResult> result =
      compilation.environment.ptr()->PolyLookup(symbol_namespace, symbol);
  if (result.empty()) {
    compilation.AddError(
        Error{LazyString{L"Unknown variable: `" + to_wstring(symbol) + L"`"}});
    return nullptr;
  }

  std::unordered_set<Type> already_seen;
  std::vector<Type> types;
  // We can't use `MaterializeVector` here: we need to ensure that the filtering
  // is done in an order-preserving way.
  for (const Type& type :
       std::move(result) |
           std::views::transform(&Environment::LookupResult::type))
    if (already_seen.insert(type).second) types.push_back(type);
  return std::make_unique<VariableLookup>(std::move(symbol_namespace),
                                          std::move(symbol), types);
}

}  // namespace afc::vm
