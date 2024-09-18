#include "src/vm/environment.h"

#include <glog/logging.h>

#include <map>
#include <ranges>
#include <set>

#include "src/concurrent/protected.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/safe_types.h"
#include "src/math/numbers.h"
#include "src/vm/callbacks.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::Protected;
using afc::language::Error;
using afc::language::InsertOrDie;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::SingleLine;
using afc::math::numbers::Number;

namespace afc::vm {

using ::operator<<;

template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<Protected<std::vector<int>>>>>::object_type_name =
    types::ObjectName{LazyString{L"VectorInt"}};

template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<Protected<std::set<int>>>>>::object_type_name =
    types::ObjectName{LazyString{L"SetInt"}};

// TODO(easy, 2022-12-03): Get rid of this? Now that we have GC, shouldn't be
// needed.
void Environment::Clear() {
  object_types_.clear();
  data_.lock([](Data& data) { data.table.clear(); });
}

std::optional<language::gc::Ptr<Environment>> Environment::parent_environment()
    const {
  return parent_environment_;
}

const ObjectType* Environment::LookupObjectType(
    const types::ObjectName& name) const {
  if (auto it = object_types_.find(name); it != object_types_.end()) {
    return &it->second.value();
  }
  if (parent_environment_.has_value()) {
    return (*parent_environment_)->LookupObjectType(name);
  }
  return nullptr;
}

const Type* Environment::LookupType(const Identifier& symbol) const {
  if (symbol == Identifier{LazyString{L"void"}}) {
    static Type output = types::Void{};
    return &output;
  } else if (symbol == Identifier{LazyString{L"bool"}}) {
    static Type output = types::Bool{};
    return &output;
  } else if (symbol == Identifier{LazyString{L"number"}}) {
    static Type output = types::Number{};
    return &output;
  } else if (symbol == Identifier{LazyString{L"string"}}) {
    static Type output = types::String{};
    return &output;
  }

  auto object_type =
      LookupObjectType(types::ObjectName(symbol.read().read().read()));
  return object_type == nullptr ? nullptr : &object_type->type();
}

/* static */ language::gc::Root<Environment> Environment::New(
    language::gc::Pool& pool) {
  return pool.NewRoot(MakeNonNullUnique<Environment>(ConstructorAccessTag()));
}

/* static */ language::gc::Root<Environment> Environment::New(
    language::gc::Ptr<Environment> parent_environment) {
  gc::Pool& pool = parent_environment.pool();
  return pool.NewRoot(MakeNonNullUnique<Environment>(
      ConstructorAccessTag(), std::move(parent_environment)));
}

Environment::Environment(ConstructorAccessTag) {}

Environment::Environment(ConstructorAccessTag,
                         gc::Ptr<Environment> parent_environment)
    : parent_environment_(std::move(parent_environment)) {}

/* static */ gc::Root<Environment> Environment::NewNamespace(
    gc::Ptr<Environment> parent, Identifier name) {
  // TODO(thread-safety, 2023-10-13): There's actually a race condition here.
  // Multiple concurrent calls could trigger failures.
  if (std::optional<gc::Root<Environment>> previous =
          LookupNamespace(parent, Namespace({name}));
      previous.has_value()) {
    return *previous;
  }
  if (auto parent_value = parent->data_.lock(
          [&name](Data& parent_data) -> std::optional<gc::Root<Environment>> {
            if (auto result = parent_data.namespaces.find(name);
                result != parent_data.namespaces.end()) {
              return result->second.ToRoot();
            }
            return std::nullopt;
          });
      parent_value.has_value())
    return parent_value.value();

  gc::Root<Environment> namespace_env = Environment::New(parent);
  parent->data_.lock([&](Data& data) {
    InsertOrDie(data.namespaces, {name, namespace_env.ptr()});
  });
  return namespace_env;
}

/* static */ std::optional<gc::Root<Environment>> Environment::LookupNamespace(
    gc::Ptr<Environment> source, const Namespace& name) {
  if (std::optional<gc::Ptr<Environment>> output = container::FoldOptional(
          [](auto n, gc::Ptr<Environment> env) {
            return env->data_.lock(
                [&](Data& data) -> std::optional<gc::Ptr<Environment>> {
                  return language::GetValueOrNullOpt(data.namespaces, n);
                });
          },
          source, name);
      output.has_value())
    return output->ToRoot();
  return VisitOptional(
      [&name](gc::Ptr<Environment> parent_environment) {
        return LookupNamespace(parent_environment, name);
      },
      [] { return std::optional<gc::Root<Environment>>(); },
      source->parent_environment());
}

void Environment::DefineType(gc::Ptr<ObjectType> value) {
  object_types_.insert_or_assign(NameForType(value->type()), std::move(value));
}

std::optional<gc::Root<Value>> Environment::Lookup(
    gc::Pool& pool, const Namespace& symbol_namespace, const Identifier& symbol,
    Type expected_type) const {
  std::vector<LookupResult> values = PolyLookup(symbol_namespace, symbol);
  for (Value& value :
       values | std::views::transform(&LookupResult::value) | gc::view::Value)
    if (auto callback = GetImplicitPromotion(value.type, expected_type);
        callback != nullptr)
      return callback(pool, pool.NewRoot(MakeNonNullUnique<Value>(value)));
  return std::nullopt;
}

std::vector<Environment::LookupResult> Environment::PolyLookup(
    const Namespace& symbol_namespace, const Identifier& symbol) const {
  std::vector<LookupResult> output;
  PolyLookup(symbol_namespace, symbol, LookupResult::VariableScope::kLocal,
             output);
  return output;
}

void Environment::PolyLookup(const Namespace& symbol_namespace,
                             const Identifier& symbol,
                             LookupResult::VariableScope variable_scope,
                             std::vector<LookupResult>& output) const {
  if (const Environment* environment = FindNamespace(symbol_namespace);
      environment != nullptr) {
    environment->data_.lock(
        [&output, &symbol, variable_scope](const Data& data) {
          if (auto it = data.table.find(symbol); it != data.table.end()) {
            auto view = it->second | std::views::values |
                        std::views::transform(
                            [variable_scope](const gc::Ptr<Value>& entry) {
                              return LookupResult{.scope = variable_scope,
                                                  .value = entry.ToRoot()};
                            });
            output.insert(output.end(), view.begin(), view.end());
          }
        });
  }
  // Deliverately ignoring `environment`:
  if (parent_environment_.has_value()) {
    (*parent_environment_)
        ->PolyLookup(symbol_namespace, symbol,
                     LookupResult::VariableScope::kGlobal, output);
  }
}

void Environment::CaseInsensitiveLookup(
    const Namespace& symbol_namespace, const Identifier& symbol,
    std::vector<gc::Root<Value>>* output) const {
  if (const Environment* environment = FindNamespace(symbol_namespace);
      environment != nullptr) {
    environment->data_.lock([&output, &symbol](const Data& data) {
      SingleLine lower_case_symbol = LowerCase(symbol.read().read());
      for (auto& item : data.table)
        if (LowerCase(item.first.read().read()) == lower_case_symbol)
          std::ranges::copy(item.second | std::views::values | gc::view::Root,
                            std::back_inserter(*output));
    });
  }
  // Deliverately ignoring `environment`:
  if (parent_environment_.has_value()) {
    (*parent_environment_)
        ->CaseInsensitiveLookup(symbol_namespace, symbol, output);
  }
}

void Environment::Define(const Identifier& symbol, gc::Root<Value> value) {
  Type type = value.ptr()->type;
  data_.lock([&](Data& data) {
    data.table[symbol].insert_or_assign(type, value.ptr());
  });
}

void Environment::Assign(const Identifier& symbol, gc::Root<Value> value) {
  data_.lock([&](Data& data) {
    if (auto it = data.table.find(symbol); it != data.table.end()) {
      it->second.insert_or_assign(value.ptr()->type, value.ptr());
    } else {
      // TODO: Show the symbol.
      CHECK(parent_environment_.has_value())
          << "Environment::parent_environment_ is nullptr while trying to "
             "assign a new value to a symbol `...`. This likely means that the "
             "symbol is undefined (which the caller should have validated as "
             "part of the compilation process).";
      (*parent_environment_)->Assign(symbol, std::move(value));
    }
  });
}

void Environment::Remove(const Identifier& symbol, Type type) {
  data_.lock([&](Data& data) {
    if (auto it = data.table.find(symbol); it != data.table.end())
      it->second.erase(type);
  });
}

void Environment::ForEachType(
    std::function<void(const types::ObjectName&, ObjectType&)> callback) {
  if (parent_environment_.has_value()) {
    (*parent_environment_)->ForEachType(callback);
  }
  for (const std::pair<const types::ObjectName, gc::Ptr<ObjectType>>& entry :
       object_types_) {
    callback(entry.first, entry.second.value());
  }
}

void Environment::ForEach(
    std::function<void(const Identifier&, const gc::Ptr<Value>&)> callback)
    const {
  if (parent_environment_.has_value()) {
    (*parent_environment_)->ForEach(callback);
  }
  ForEachNonRecursive(callback);
}

void Environment::ForEachNonRecursive(
    std::function<void(const Identifier&, const gc::Ptr<Value>&)> callback)
    const {
  data_.lock([&](const Data& data) {
    for (const auto& symbol_entry : data.table) {
      for (const gc::Ptr<Value>& value :
           symbol_entry.second | std::views::values) {
        VLOG(5) << "ForEachNonRecursive: Running callback on: "
                << symbol_entry.first;
        callback(symbol_entry.first, value);
      }
    }
  });
}

std::vector<language::NonNull<std::shared_ptr<gc::ObjectMetadata>>>
Environment::Expand() const {
  std::vector<language::NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  if (parent_environment().has_value()) {
    output.push_back(parent_environment()->object_metadata());
  }
  ForEachNonRecursive(
      [&output](const Identifier&, const gc::Ptr<Value>& value) {
        output.push_back(value.object_metadata());
      });
  data_.lock([&output](const Data& data) {
    std::ranges::copy(
        data.namespaces | std::views::values | gc::view::ObjectMetadata,
        std::back_inserter(output));
  });

  std::ranges::copy(
      object_types_ | std::views::values | gc::view::ObjectMetadata,
      std::back_inserter(output));
  return output;
}

const Environment* Environment::FindNamespace(
    const Namespace& namespace_name) const {
  const Environment* environment = this;
  for (auto& n : namespace_name) {
    CHECK(environment != nullptr);
    environment =
        environment->data_.lock([&](const Data& data) -> const Environment* {
          if (auto it = data.namespaces.find(n); it != data.namespaces.end())
            return &it->second.value();
          else
            return nullptr;
        });
    if (environment == nullptr) return nullptr;
  }
  return environment;
}
}  // namespace afc::vm
