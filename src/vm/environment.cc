#include "src/vm/environment.h"

#include <glog/logging.h>

#include <map>
#include <ranges>
#include <set>

#include "src/concurrent/protected.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/math/numbers.h"
#include "src/tests/tests.h"
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
using afc::language::overload;
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
    types::ObjectName{Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"VectorInt")}};

template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<Protected<std::set<int>>>>>::object_type_name =
    types::ObjectName{Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"SetInt")}};

EnvironmentIdentifierTable::EnvironmentIdentifierTable(ConstructorAccessTag) {}

void EnvironmentIdentifierTable::insert_or_assign(
    const Type& type, std::variant<UninitializedValue, gc::Ptr<Value>> value) {
  std::visit(overload{[&type](const gc::Ptr<Value>& ptr_value) {
                        CHECK(ptr_value->type() == type);
                        DVLOG(6) << "Inserting: " << ptr_value.value();
                      },
                      [](UninitializedValue) {}},
             value);
  table_.lock([&type, &value](Table& table) {
    table.insert_or_assign(type, std::move(value));
  });
}

void EnvironmentIdentifierTable::erase(const Type& type) {
  table_.lock([&type](Table& table) { table.erase(type); });
}

void EnvironmentIdentifierTable::clear() {
  table_.lock([](Table& table) { table.clear(); });
}

std::unordered_map<Type, std::variant<UninitializedValue, gc::Root<Value>>>
EnvironmentIdentifierTable::GetMapTypeVariantRootValue() const {
  return table_.lock([](const Table& table) {
    return container::MaterializeUnorderedMap(
        table |
        std::views::transform([](const std::pair<
                                  const Type&, std::variant<UninitializedValue,
                                                            gc::Ptr<Value>>>&
                                     entry) {
          return std::make_pair(
              entry.first,
              std::visit(
                  overload{
                      [](const gc::Ptr<Value>& value)
                          -> std::variant<UninitializedValue, gc::Root<Value>> {
                        return value.ToRoot();
                      },
                      [](UninitializedValue)
                          -> std::variant<UninitializedValue, gc::Root<Value>> {
                        return UninitializedValue{};
                      }},
                  entry.second));
        }));
  });
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
EnvironmentIdentifierTable::Expand() const {
  return table_.lock([](const Table& table) {
    return container::MaterializeVector(
        table | std::views::values |
        std::views::filter(
            [](const std::variant<UninitializedValue, gc::Ptr<Value>>& entry) {
              return std::holds_alternative<gc::Ptr<Value>>(entry);
            }) |
        std::views::transform(
            [](const std::variant<UninitializedValue, gc::Ptr<Value>>& entry) {
              return std::get<gc::Ptr<Value>>(entry).object_metadata();
            }));
  });
}

// TODO(easy, 2022-12-03): Get rid of this? Now that we have GC, shouldn't be
// needed.
void Environment::Clear() {
  object_types_.clear();
  data_.lock([](Data& data) { data.table.clear(); });
}

std::optional<gc::Ptr<Environment>> Environment::parent_environment() const {
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
  if (symbol == LazyString{L"void"}) {
    static Type output = types::Void{};
    return &output;
  } else if (symbol == LazyString{L"bool"}) {
    static Type output = types::Bool{};
    return &output;
  } else if (symbol == LazyString{L"number"}) {
    static Type output = types::Number{};
    return &output;
  } else if (symbol == LazyString{L"string"}) {
    static Type output = types::String{};
    return &output;
  }

  auto object_type = LookupObjectType(types::ObjectName(symbol));
  return object_type == nullptr ? nullptr : &object_type->type();
}

/* static */ gc::Root<Environment> Environment::New(gc::Pool& pool) {
  return pool.NewRoot(
      MakeNonNullUnique<Environment>(ConstructorAccessTag{}, pool));
}

/* static */ gc::Root<Environment> Environment::New(
    gc::Ptr<Environment> parent_environment) {
  gc::Pool& pool = parent_environment.pool();
  return pool.NewRoot(MakeNonNullUnique<Environment>(
      ConstructorAccessTag(), std::move(parent_environment)));
}

Environment::Environment(ConstructorAccessTag, gc::Pool& pool) : pool_(pool) {}

Environment::Environment(ConstructorAccessTag,
                         gc::Ptr<Environment> parent_environment)
    : pool_(parent_environment.pool()),
      parent_environment_(std::move(parent_environment)) {}

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

std::optional<Environment::LookupResult> Environment::Lookup(
    const Namespace& symbol_namespace, const Identifier& symbol,
    Type expected_type) const {
  VLOG(5) << "Lookup: " << symbol;
  for (LookupResult lookup_result : PolyLookup(symbol_namespace, symbol))
    if (gc::Root<Value>* root_value =
            std::get_if<gc::Root<Value>>(&lookup_result.value);
        root_value != nullptr)
      if (auto callback = GetImplicitPromotion(
              std::get<gc::Root<Value>>(lookup_result.value)->type(),
              expected_type);
          callback != nullptr) {
        gc::Root<Value> output_value = callback(*root_value);
        return LookupResult{.scope = lookup_result.scope,
                            .type = output_value->type(),
                            .value = output_value};
      }
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
            std::ranges::copy(
                it->second->GetMapTypeVariantRootValue() |
                    std::views::transform(
                        [variable_scope](
                            std::pair<Type, std::variant<UninitializedValue,
                                                         gc::Root<Value>>>
                                entry) {
                          return LookupResult{.scope = variable_scope,
                                              .type = std::move(entry.first),
                                              .value = std::move(entry.second)};
                        }),
                std::back_inserter(output));
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
          std::ranges::copy(
              item.second->GetMapTypeVariantRootValue() | std::views::values |
                  std::views::filter(
                      [](const std::variant<UninitializedValue,
                                            gc::Root<Value>>& value) {
                        return std::holds_alternative<gc::Root<Value>>(value);
                      }) |
                  std::views::transform(
                      [](std::variant<UninitializedValue, gc::Root<Value>>
                             value) {
                        return std::get<gc::Root<Value>>(std::move(value));
                      }),
              std::back_inserter(*output));
    });
  }
  // Deliverately ignoring `environment`:
  if (parent_environment_.has_value()) {
    (*parent_environment_)
        ->CaseInsensitiveLookup(symbol_namespace, symbol, output);
  }
}

void Environment::DefineUninitialized(const Identifier& symbol,
                                      const Type& type) {
  data_.lock([&pool = pool_, &symbol, &type](Data& data) {
    GetOrCreateTable(pool, data, symbol)
        .insert_or_assign(type, UninitializedValue{});
  });
}

void Environment::Define(const Identifier& symbol, gc::Root<Value> value) {
  data_.lock([&pool = pool_, &symbol, &value](Data& data) {
    DVLOG(6) << symbol << ": Define";
    DVLOG(7) << symbol << ": Define with value: " << value.ptr().value();
    GetOrCreateTable(pool, data, symbol)
        .insert_or_assign(value->type(), value.ptr());
  });
}

void Environment::Assign(const Identifier& symbol, gc::Root<Value> value) {
  data_.lock([&](Data& data) {
    if (auto it = data.table.find(symbol); it != data.table.end()) {
      it->second->insert_or_assign(value->type(), value.ptr());
    } else {
      CHECK(parent_environment_.has_value())
          << "Environment::parent_environment_ is nullptr while trying to "
             "assign a new value to a symbol `"
          << symbol
          << "`. This likely means that the "
             "symbol is undefined (which the caller should have validated as "
             "part of the compilation process).";
      (*parent_environment_)->Assign(symbol, std::move(value));
    }
  });
}

void Environment::Remove(const Identifier& symbol, Type type) {
  data_.lock([&](Data& data) {
    if (auto it = data.table.find(symbol); it != data.table.end())
      it->second->erase(type);
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
    std::function<void(const Identifier&,
                       const std::variant<UninitializedValue, gc::Ptr<Value>>&)>
        callback) const {
  if (parent_environment_.has_value())
    (*parent_environment_)->ForEach(callback);
  ForEachNonRecursive(callback);
}

void Environment::ForEachNonRecursive(
    std::function<void(const Identifier&,
                       const std::variant<UninitializedValue, gc::Ptr<Value>>&)>
        callback) const {
  data_.lock([&](const Data& data) {
    for (const auto& symbol_entry : data.table)
      std::ranges::for_each(
          symbol_entry.second->GetMapTypeVariantRootValue() |
              std::views::values,
          [&callback, &identifier = symbol_entry.first](
              std::variant<UninitializedValue, gc::Root<Value>> value) {
            VLOG(5) << "ForEachNonRecursive: Running callback on: "
                    << identifier;
            callback(identifier,
                     std::visit(overload{[](gc::Root<Value> root)
                                             -> std::variant<UninitializedValue,
                                                             gc::Ptr<Value>> {
                                           return root.ptr();
                                         },
                                         [](UninitializedValue)
                                             -> std::variant<UninitializedValue,
                                                             gc::Ptr<Value>> {
                                           return UninitializedValue{};
                                         }},
                                value));
          });
  });
}

EnvironmentIdentifierTable& Environment::GetOrCreateTable(
    gc::Pool& pool, Environment::Data& data, const Identifier& symbol) {
  if (auto it = data.table.find(symbol); it != data.table.end())
    return it->second.value();
  return data.table
      .insert(std::make_pair(
          symbol,
          pool.NewRoot(MakeNonNullUnique<EnvironmentIdentifierTable>(
                           EnvironmentIdentifierTable::ConstructorAccessTag{}))
              .ptr()))
      .first->second.value();
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Environment::Expand()
    const {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  if (parent_environment().has_value())
    output.push_back(parent_environment()->object_metadata());
  data_.lock([&output](const Data& data) {
    std::ranges::copy(
        data.namespaces | std::views::values | gc::view::ObjectMetadata,
        std::back_inserter(output));
    std::ranges::copy(
        data.table | std::views::values | gc::view::ObjectMetadata,
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

namespace {
const bool environment_tests_registration = tests::Register(
    L"Environment",
    {
        {.name = L"ParentSurvivesCollection",
         .callback =
             [] {
               gc::Pool pool{gc::Pool::Options{}};
               CHECK_EQ(pool.count_objects(), 0ul);
               std::optional<gc::Root<Environment>> parent =
                   Environment::New(pool);
               CHECK_EQ(pool.count_objects(), 1ul);

               static const Identifier id{
                   NON_EMPTY_SINGLE_LINE_CONSTANT(L"foo")};

               parent->ptr()->DefineUninitialized(id, types::String{});
               parent->ptr()->Assign(
                   id, Value::NewString(pool, LazyString{L"bar"}));
               CHECK_EQ(pool.count_objects(), 3ul);

               std::optional<gc::Root<Environment>> child =
                   Environment::New(parent->ptr());
               CHECK_EQ(pool.count_objects(), 4ul);

               parent = std::nullopt;
               pool.FullCollect();
               pool.BlockUntilDone();
               CHECK_EQ(pool.count_objects(), 4ul);

               std::vector<Environment::LookupResult> output =
                   child->ptr()->PolyLookup(Namespace{}, id);
               CHECK_EQ(output.size(), 1ul);
               CHECK_EQ(
                   std::get<gc::Root<Value>>(output.at(0).value)->get_string(),
                   LazyString{L"bar"});

               child = std::nullopt;
               pool.FullCollect();
               pool.BlockUntilDone();
               CHECK_EQ(pool.count_objects(), 1ul);
               CHECK_EQ(
                   std::get<gc::Root<Value>>(output.at(0).value)->get_string(),
                   LazyString{L"bar"});

               output.clear();
               pool.FullCollect();
               pool.BlockUntilDone();
               CHECK_EQ(pool.count_objects(), 0ul);
             }},
    });
}  // namespace
}  // namespace afc::vm
