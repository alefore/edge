#include "src/math/naive_bayes.h"

#include <numeric>
#include <ranges>

#include "glog/logging.h"
#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using ::operator<<;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::GetValueOrDefault;
using afc::language::GetValueOrDie;
using afc::language::GhostType;
using afc::language::IsError;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;

namespace afc::math::naive_bayes {
struct ProbabilityValidator {
  static PossibleError Validate(const double& input) {
    if (input < 0)
      return Error{LazyString{L"Invalid probability value (less than 0.0)."}};
    if (input > 1.0)
      return Error{
          LazyString{L"Invalid probability value (greater than 1.0)."}};
    return Success();
  }
};

class Probability
    : public GhostType<Probability, double, ProbabilityValidator> {
  using GhostType::GhostType;
};

namespace {
const bool probability_constructor_good_inputs_tests_registration =
    tests::Register(L"ProbabilityConstructorGoodInputs",
                    {{.name = L"Zero", .callback = [] { Probability{0.0}; }},
                     {.name = L"One", .callback = [] { Probability{1.0}; }},
                     {.name = L"Half", .callback = [] { Probability{0.5}; }}});

const bool probability_constructor_bad_inputs_tests_registration =
    tests::Register(
        L"ProbabilityConstructorBadInputs",
        {
            {.name = L"Negative",
             .callback = [] { CHECK(IsError(Probability::New(-1.0))); }},
            {.name = L"NegativeCrash",
             .callback =
                 [] {
                   tests::ForkAndWaitForFailure([] { Probability(-1.0); });
                 }},
            {.name = L"TooLarge",
             .callback = [] { CHECK(IsError(Probability::New(1.01))); }},
            {.name = L"TooLargeCrash",
             .callback =
                 [] {
                   tests::ForkAndWaitForFailure([] { Probability(1.01); });
                 }},

        });

class EventProbabilityMap
    : public GhostType<EventProbabilityMap,
                       std::unordered_map<Event, Probability>> {};

class FeatureProbabilityMap
    : public GhostType<FeatureProbabilityMap,
                       std::unordered_map<Feature, Probability>> {};

template <typename Container, typename Callable>
auto TransformValues(const Container& container, Callable callable) {
  using K = std::remove_const<
      decltype(std::declval<typename Container::value_type>().first)>::type;
  using V = decltype(std::declval<typename Container::value_type>().second);
  std::unordered_map<K,
                     decltype(callable(std::declval<K>(), std::declval<V>()))>
      output;
  for (const std::pair<const K, V>& entry : container)
    output.insert({entry.first, callable(entry.first, entry.second)});
  return output;
}

// Returns the probability of each event in history.
EventProbabilityMap GetEventProbability(const History& history) {
  size_t count = 0;
  for (const std::vector<FeaturesSet>& instances : std::views::values(history))
    count += instances.size();

  return EventProbabilityMap(TransformValues(
      history,
      [&count](const Event&, const std::vector<FeaturesSet>& instances) {
        return ValueOrDie(
            Probability::New(static_cast<double>(instances.size()) / count));
      }));
}

const bool get_probability_of_event_tests_registration =
    tests::Register(L"GetEventProbability", [] {
      Event e0{LazyString{L"e0"}}, e1{LazyString{L"e1"}}, e2{LazyString{L"e2"}};
      Feature f1{LazyString{L"f1"}}, f2{LazyString{L"f2"}},
          f3{LazyString{L"f3"}}, f4{LazyString{L"f4"}}, f5{LazyString{L"f5"}};
      return std::vector<tests::Test>(
          {{.name = L"Empty",
            .callback =
                [] { CHECK_EQ(GetEventProbability(History()).size(), 0ul); }},
           {.name = L"SingleEventSingleInstance",
            .callback =
                [=] {
                  EventProbabilityMap result = GetEventProbability(
                      History{{{e0, {FeaturesSet{{f1, f2}}}}}});
                  CHECK_EQ(result.size(), 1ul);
                  CHECK_EQ(GetValueOrDie(result, e0), Probability{1.0});
                }},
           {.name = L"SingleEventMultipleInstance",
            .callback =
                [=] {
                  EventProbabilityMap result =
                      GetEventProbability(History{{{e0,
                                                    {
                                                        FeaturesSet{{f1, f2}},
                                                        FeaturesSet{{f1}},
                                                        FeaturesSet{{f2}},
                                                    }}}});
                  CHECK_EQ(result.size(), 1ul);
                  CHECK_EQ(GetValueOrDie(result, e0), Probability{1.0});
                }},
           {.name = L"MultipleEvents", .callback = [=] {
              EventProbabilityMap result =
                  GetEventProbability(History{{{e0,
                                                {
                                                    FeaturesSet{{f1}},
                                                    FeaturesSet{{f2}},
                                                    FeaturesSet{{f3}},
                                                    FeaturesSet{{f4}},
                                                    FeaturesSet{{f5}},
                                                }},
                                               {e1,
                                                {
                                                    FeaturesSet{{f1}},
                                                    FeaturesSet{{f2}},
                                                    FeaturesSet{{f3}},
                                                    FeaturesSet{{f4}},
                                                }},
                                               {e2,
                                                {
                                                    FeaturesSet{{f1}},
                                                }}}});
              CHECK_EQ(result.size(), 3ul);
              CHECK_EQ(GetValueOrDie(result, e0), Probability{0.5});
              CHECK_EQ(GetValueOrDie(result, e1), Probability{0.4});
              CHECK_EQ(GetValueOrDie(result, e2), Probability{0.1});
            }}});
    }());

FeatureProbabilityMap GetFeatureProbability(
    const std::vector<FeaturesSet>& instances) {
  std::unordered_map<Feature, size_t> features;
  for (const FeaturesSet& instance : instances)
    for (const Feature& f : instance) features[f]++;

  return FeatureProbabilityMap(
      TransformValues(features, [&](const Feature&, size_t feature_count) {
        return Probability(static_cast<double>(feature_count) /
                           instances.size());
      }));
}

const bool get_probability_of_feature_given_event_tests_registration =
    tests::Register(L"GetFeatureProbability", []() {
      Event e0{LazyString{L"e0"}}, e1{LazyString{L"e1"}};
      Feature f1{LazyString{L"f1"}}, f2{LazyString{L"f2"}},
          f3{LazyString{L"f3"}};
      return std::vector<tests::Test>(
          {{.name = L"Empty",
            .callback =
                [] { CHECK_EQ(GetFeatureProbability({}).size(), 0ul); }},
           {.name = L"SingleEventSingleInstance",
            .callback =
                [=] {
                  FeatureProbabilityMap result =
                      GetFeatureProbability({FeaturesSet{{f1, f2}}});
                  CHECK_EQ(result.size(), 2ul);
                  CHECK_EQ(GetValueOrDie(result, f1), Probability{1.0});
                  CHECK_EQ(GetValueOrDie(result, f2), Probability{1.0});
                }},
           {.name = L"SingleEventMultipleInstances", .callback = [=] {
              FeatureProbabilityMap result = GetFeatureProbability({
                  FeaturesSet{{f1, f2, f3}},
                  FeaturesSet{{f1, f2}},
                  FeaturesSet{{f1}},
                  FeaturesSet{{f1}},
                  FeaturesSet{{f1}},
              });
              CHECK_EQ(result.size(), 3ul);

              CHECK_EQ(GetValueOrDie(result, f1), Probability{1.0});
              CHECK_EQ(GetValueOrDie(result, f2), Probability{0.4});
              CHECK_EQ(GetValueOrDie(result, f3), Probability{0.2});
            }}});
    }());

Probability MinimalFeatureProbability(
    const std::unordered_map<Event, FeatureProbabilityMap>&
        probability_of_feature_given_event) {
  Probability output(1.0);
  for (const FeatureProbabilityMap& features :
       probability_of_feature_given_event | std::views::values)
    for (const Probability p : features | std::views::values)
      output = std::min(output, p);
  return output;
}

const bool minimal_feature_probability_tests_registration = tests::Register(
    L"MinimalFeatureProbability",
    {{.name = L"Empty",
      .callback =
          [] { CHECK_EQ(MinimalFeatureProbability({}), Probability{1.0}); }},
     {.name = L"SomeData", .callback = [] {
        Event e0{LazyString{L"e0"}}, e1{LazyString{L"e1"}},
            e2{LazyString{L"e2"}};
        Feature f1{LazyString{L"f1"}}, f2{LazyString{L"f2"}};

        std::unordered_map<Event, FeatureProbabilityMap> data;
        data[e0][f1] = Probability{0.2};
        data[e0][f2] = Probability{0.8};
        data[e1][f1] = Probability{0.8};
        data[e1][f2] = Probability{0.5};
        data[e2][f1] = Probability{0.1};  // <--- Minimal.
        data[e2][f2] = Probability{0.5};
        CHECK_EQ(MinimalFeatureProbability(data), Probability{0.1});
      }}});
}  // namespace

std::vector<Event> Sort(const History& history,
                        const FeaturesSet& current_features) {
  // Let F = f₀, f₁, ..., fₙ be the set of current features. We'd like to
  // compute the probability of each event eᵢ in history given current_features:
  // p(eᵢ | F).
  //
  // We know that:
  //
  //     p(eᵢ | F) p(F) = p(eᵢ ∩ F)                         (1)
  //
  // Since p(F) is the same for all i (and thus won't affect the computation for
  // eᵢ for different values if i), we get rid of it.
  //
  //     p(eᵢ | F) ~= p(eᵢ ∩ F)
  //
  // We know that (1):
  //
  //     p(eᵢ ∩ F)
  //   = p(f₀ ∩ f₁ ∩ f₂ ∩ ... fₙ ∩ eᵢ)
  //   = p(f₀ | (f₁ ∩ f₂ ∩ ... fₙ ∩ eᵢ)) *
  //     p(f₁ | (f₂ ∩ ... ∩ fₙ ∩ eᵢ)) *
  //     ... *
  //     p(fₙ | eᵢ) *
  //     p(eᵢ)
  //
  // The naive assumption lets us simplify to p(fⱼ | eᵢ) the expression:
  //
  //   p(fⱼ | fⱼ₊₁ ∩ fⱼ₊₂ ∩ ... fₙ ∩ eᵢ)
  //
  // So (1) simplifies to:
  //
  //     p(eᵢ ∩ F)
  //   = p(f₀ | eᵢ) * ... * p(fₙ | eᵢ) * p(eᵢ)
  //   = p(eᵢ) Πj p(fⱼ | eᵢ)
  //
  // Πj denotes the multiplication over all values j.
  //
  // There's a small catch. For features absent from eᵢ's history (that is, for
  // features fⱼ where p(fⱼ|eᵢ) is 0), we don't want to fully discard eᵢ (i.e.,
  // we don't want to assign it a proportional probability of 0). If we did
  // that, sporadic features would be given too much weight. To achieve this, we
  // compute a small value epsilon and use:
  //
  //     p(eᵢ, F) = p(eᵢ) Πj max(epsilon, p(fⱼ | eᵢ))
  TRACK_OPERATION(NaiveBayes_SortByProportionalProbability);

  // probability_of_feature_given_event[eᵢ][fⱼ] represents a value p(fⱼ | eᵢ):
  // the probability of fⱼ given eᵢ.
  const std::unordered_map<Event, FeatureProbabilityMap>
      probability_of_feature_given_event = TransformValues(
          history, [](const Event&, const std::vector<FeaturesSet>& instances) {
            return GetFeatureProbability(instances);
          });

  const Probability epsilon = ValueOrDie(
      MinimalFeatureProbability(probability_of_feature_given_event) / 2);
  VLOG(5) << "Found epsilon: " << epsilon;

  const std::unordered_map<Event, Probability> current_probability_value =
      TransformValues(
          GetEventProbability(history), [&](const Event& event, Probability p) {
            const FeatureProbabilityMap& feature_probability_map =
                GetValueOrDie(probability_of_feature_given_event, event);
            for (const Feature& feature : current_features)
              p *= GetValueOrDefault(feature_probability_map, feature, epsilon);
            VLOG(6) << "Current probability for " << event << ": " << p;
            return p;
          });

  auto events = std::views::keys(history);
  std::vector<Event> output(events.begin(), events.end());

  sort(output.begin(), output.end(),
       [&current_probability_value](const Event& a, const Event& b) {
         return GetValueOrDie(current_probability_value, a) <
                GetValueOrDie(current_probability_value, b);
       });
  return output;
}

const bool bayes_sort_tests_probability_tests_registration =
    tests::Register(L"BayesSort", [] {
      Event e0{LazyString{L"e0"}}, e1{LazyString{L"e1"}}, e2{LazyString{L"e2"}},
          e3{LazyString{L"e3"}}, e4{LazyString{L"e4"}};
      Feature f1{LazyString{L"f1"}}, f2{LazyString{L"f2"}},
          f3{LazyString{L"f3"}}, f4{LazyString{L"f4"}}, f5{LazyString{L"f5"}},
          f6{LazyString{L"f6"}};
      return std::vector<tests::Test>({
          {.name = L"EmptyHistoryAndFeatures",
           .callback =
               [] { CHECK_EQ(Sort(History(), FeaturesSet{}).size(), 0ul); }},
          {.name = L"EmptyHistory",
           .callback =
               [=] {
                 CHECK_EQ(Sort(History(), FeaturesSet{{f1, f2}}).size(), 0ul);
               }},
          {.name = L"EmptyFeatures",
           .callback =
               [=] {
                 History history;
                 history[e0] = {FeaturesSet{{f1}}, FeaturesSet{{f2}}};
                 history[e1] = {FeaturesSet{{f3}}};
                 std::vector<Event> results = Sort(history, FeaturesSet{});
                 CHECK_EQ(results.size(), 2ul);
                 CHECK_EQ(results.front(), e1);
                 CHECK_EQ(results.back(), e0);
               }},
          {.name = L"NewFeature",
           .callback =
               [=] {
                 History history;
                 history[e0] = {FeaturesSet{{f1}}, FeaturesSet{{f2}}};
                 history[e1] = {FeaturesSet{{f3}}};
                 std::vector<Event> results = Sort(history, FeaturesSet{{f4}});
                 CHECK_EQ(results.size(), 2ul);
                 CHECK_EQ(results.front(), e1);
                 CHECK_EQ(results.back(), e0);
               }},
          {.name = L"FeatureSelects",
           .callback =
               [=] {
                 std::vector<Event> results =
                     Sort(History{{
                              {e0,
                               {
                                   FeaturesSet{{f1}},
                                   FeaturesSet{{f2}},
                               }},
                              {e1, {FeaturesSet{{f3}}}},
                          }},
                          FeaturesSet{{f3}});
                 CHECK_EQ(results.size(), 2ul);
                 CHECK_EQ(results.front(), e0);
                 CHECK_EQ(results.back(), e1);
               }},
          {.name = L"FeatureSelectsSomeOverlap",
           .callback =
               [=] {
                 std::vector<Event> results =
                     Sort(History{{
                              {e0,
                               {
                                   FeaturesSet{{f1}},
                                   FeaturesSet{{f2}},
                               }},
                              {e1, {FeaturesSet{{f1}}}},
                          }},
                          FeaturesSet{{f2}});
                 CHECK_EQ(results.size(), 2ul);
                 CHECK_EQ(results.front(), e1);
                 CHECK_EQ(results.back(), e0);
               }},
          {.name = L"FeatureSelectsFive",
           .callback =
               [=] {
                 std::vector<Event> results =
                     Sort(History{{
                              {e0,
                               {
                                   FeaturesSet{{f1}},
                                   FeaturesSet{{f5, f6}},
                                   FeaturesSet{{f2}},
                               }},
                              {e1,
                               {
                                   FeaturesSet{{f5}},
                                   FeaturesSet{{f6}},
                                   FeaturesSet{{f5}},
                               }},
                              {e2,
                               {
                                   FeaturesSet{{f5}},
                                   FeaturesSet{{f2}},
                                   FeaturesSet{{f3}},
                               }},
                              {e3,
                               {
                                   FeaturesSet{{f5, f2}},
                                   FeaturesSet{{f6}},
                               }},
                              {e4,
                               {
                                   FeaturesSet{{f4}},
                               }},
                          }},
                          FeaturesSet{{f5, f6}});
                 CHECK_EQ(results.size(), 5ul);
                 CHECK(results[4] == e1);
                 CHECK(results[3] == e3);
                 CHECK(results[2] == e0);
               }},
      });
    }());
}  // namespace afc::math::naive_bayes
