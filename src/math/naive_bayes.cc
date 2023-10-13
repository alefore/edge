#include "src/math/naive_bayes.h"

#include <numeric>
#include <ranges>

#include "glog/logging.h"
#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using ::operator<<;
using afc::language::GetValueOrDefault;
using afc::language::GetValueOrDie;

namespace afc::math::naive_bayes {
GHOST_TYPE_DOUBLE(Probability);
namespace {
using EventProbabilityMapInternal = std::unordered_map<Event, Probability>;
GHOST_TYPE_CONTAINER(EventProbabilityMap, EventProbabilityMapInternal);

using FeatureProbabilityMapInternal = std::unordered_map<Feature, Probability>;
GHOST_TYPE_CONTAINER(FeatureProbabilityMap, FeatureProbabilityMapInternal);

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
        return Probability(static_cast<double>(instances.size()) / count);
      }));
}

const bool get_probability_of_event_tests_registration =
    tests::Register(L"GetEventProbability", [] {
      Event e0(L"e0"), e1(L"e1"), e2(L"e2");
      Feature f1(L"f1"), f2(L"f2"), f3(L"f3"), f4(L"f4"), f5(L"f5");
      return std::vector<tests::Test>(
          {{.name = L"Empty",
            .callback =
                [] { CHECK_EQ(GetEventProbability(History()).size(), 0ul); }},
           {.name = L"SingleEventSingleInstance",
            .callback =
                [=] {
                  EventProbabilityMap result = GetEventProbability(
                      History{{{e0, {FeaturesSet({f1, f2})}}}});
                  CHECK_EQ(result.size(), 1ul);
                  CHECK_EQ(GetValueOrDie(result, e0), Probability(1.0));
                }},
           {.name = L"SingleEventMultipleInstance",
            .callback =
                [=] {
                  EventProbabilityMap result =
                      GetEventProbability(History{{{e0,
                                                    {
                                                        FeaturesSet({f1, f2}),
                                                        FeaturesSet({f1}),
                                                        FeaturesSet({f2}),
                                                    }}}});
                  CHECK_EQ(result.size(), 1ul);
                  CHECK_EQ(GetValueOrDie(result, e0), Probability(1.0));
                }},
           {.name = L"MultipleEvents", .callback = [=] {
              EventProbabilityMap result =
                  GetEventProbability(History{{{e0,
                                                {
                                                    FeaturesSet({f1}),
                                                    FeaturesSet({f2}),
                                                    FeaturesSet({f3}),
                                                    FeaturesSet({f4}),
                                                    FeaturesSet({f5}),
                                                }},
                                               {e1,
                                                {
                                                    FeaturesSet({f1}),
                                                    FeaturesSet({f2}),
                                                    FeaturesSet({f3}),
                                                    FeaturesSet({f4}),
                                                }},
                                               {e2,
                                                {
                                                    FeaturesSet({f1}),
                                                }}}});
              CHECK_EQ(result.size(), 3ul);
              CHECK_EQ(GetValueOrDie(result, e0), Probability(0.5));
              CHECK_EQ(GetValueOrDie(result, e1), Probability(0.4));
              CHECK_EQ(GetValueOrDie(result, e2), Probability(0.1));
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
      Event e0(L"e0"), e1(L"e1");
      Feature f1(L"f1"), f2(L"f2"), f3(L"f3");
      return std::vector<tests::Test>(
          {{.name = L"Empty",
            .callback =
                [] { CHECK_EQ(GetFeatureProbability({}).size(), 0ul); }},
           {.name = L"SingleEventSingleInstance",
            .callback =
                [=] {
                  FeatureProbabilityMap result =
                      GetFeatureProbability({FeaturesSet({f1, f2})});
                  CHECK_EQ(result.size(), 2ul);
                  CHECK_EQ(GetValueOrDie(result, f1), Probability(1.0));
                  CHECK_EQ(GetValueOrDie(result, f2), Probability(1.0));
                }},
           {.name = L"SingleEventMultipleInstances", .callback = [=] {
              FeatureProbabilityMap result = GetFeatureProbability({
                  FeaturesSet({f1, f2, f3}),
                  FeaturesSet({f1, f2}),
                  FeaturesSet({f1}),
                  FeaturesSet({f1}),
                  FeaturesSet({f1}),
              });
              CHECK_EQ(result.size(), 3ul);

              CHECK_EQ(GetValueOrDie(result, f1), Probability(1.0));
              CHECK_EQ(GetValueOrDie(result, f2), Probability(0.4));
              CHECK_EQ(GetValueOrDie(result, f3), Probability(0.2));
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
          [] { CHECK_EQ(MinimalFeatureProbability({}), Probability(1.0)); }},
     {.name = L"SomeData", .callback = [] {
        Event e0(L"e0"), e1(L"e1"), e2(L"e2");
        Feature f1(L"f1"), f2(L"f2");

        std::unordered_map<Event, FeatureProbabilityMap> data;
        data[e0][f1] = Probability(0.2);
        data[e0][f2] = Probability(0.8);
        data[e1][f1] = Probability(0.8);
        data[e1][f2] = Probability(0.5);
        data[e2][f1] = Probability(0.1);  // <--- Minimal.
        data[e2][f2] = Probability(0.5);
        CHECK_EQ(MinimalFeatureProbability(data), Probability(0.1));
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
  static infrastructure::Tracker tracker(
      L"NaiveBayes::SortByProportionalProbability");
  auto call = tracker.Call();

  // probability_of_feature_given_event[eᵢ][fⱼ] represents a value p(fⱼ | eᵢ):
  // the probability of fⱼ given eᵢ.
  const std::unordered_map<Event, FeatureProbabilityMap>
      probability_of_feature_given_event = TransformValues(
          history, [](const Event&, const std::vector<FeaturesSet>& instances) {
            return GetFeatureProbability(instances);
          });

  const Probability epsilon =
      MinimalFeatureProbability(probability_of_feature_given_event) / 2;
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
      Event e0(L"e0"), e1(L"e1"), e2(L"e2"), e3(L"e3"), e4(L"e4");
      Feature f1(L"f1"), f2(L"f2"), f3(L"f3"), f4(L"f4"), f5(L"f5"), f6(L"f6");
      return std::vector<tests::Test>({
          {.name = L"EmptyHistoryAndFeatures",
           .callback =
               [] { CHECK_EQ(Sort(History(), FeaturesSet()).size(), 0ul); }},
          {.name = L"EmptyHistory",
           .callback =
               [=] {
                 CHECK_EQ(Sort(History(), FeaturesSet({f1, f2})).size(), 0ul);
               }},
          {.name = L"EmptyFeatures",
           .callback =
               [=] {
                 History history;
                 history[e0] = {FeaturesSet({f1}), FeaturesSet({f2})};
                 history[e1] = {FeaturesSet({f3})};
                 std::vector<Event> results = Sort(history, FeaturesSet());
                 CHECK_EQ(results.size(), 2ul);
                 CHECK_EQ(results.front(), e1);
                 CHECK_EQ(results.back(), e0);
               }},
          {.name = L"NewFeature",
           .callback =
               [=] {
                 History history;
                 history[e0] = {FeaturesSet({f1}), FeaturesSet({f2})};
                 history[e1] = {FeaturesSet({f3})};
                 std::vector<Event> results = Sort(history, FeaturesSet({f4}));
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
                                   FeaturesSet({f1}),
                                   FeaturesSet({f2}),
                               }},
                              {e1, {FeaturesSet({f3})}},
                          }},
                          FeaturesSet({f3}));
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
                                   FeaturesSet({f1}),
                                   FeaturesSet({f2}),
                               }},
                              {e1, {FeaturesSet({f1})}},
                          }},
                          FeaturesSet({f2}));
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
                                   FeaturesSet({f1}),
                                   FeaturesSet({f5, f6}),
                                   FeaturesSet({f2}),
                               }},
                              {e1,
                               {
                                   FeaturesSet({f5}),
                                   FeaturesSet({f6}),
                                   FeaturesSet({f5}),
                               }},
                              {e2,
                               {
                                   FeaturesSet({f5}),
                                   FeaturesSet({f2}),
                                   FeaturesSet({f3}),
                               }},
                              {e3,
                               {
                                   FeaturesSet({f5, f2}),
                                   FeaturesSet({f6}),
                               }},
                              {e4,
                               {
                                   FeaturesSet({f4}),
                               }},
                          }},
                          FeaturesSet({f5, f6}));
                 CHECK_EQ(results.size(), 5ul);
                 CHECK(results[4] == e1);
                 CHECK(results[3] == e3);
                 CHECK(results[2] == e0);
               }},
      });
    }());
}  // namespace afc::math::naive_bayes
