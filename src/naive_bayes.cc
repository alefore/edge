#include "src/naive_bayes.h"

#include <numeric>
#include <ranges>

#include "glog/logging.h"
#include "src/infrastructure/tracker.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::naive_bayes {
GHOST_TYPE_DOUBLE(Probability);
namespace {
using EventProbabilityMap = std::unordered_map<Event, Probability>;
using FeatureProbabilityMap = std::unordered_map<Feature, Probability>;

// Returns the probability of each event in history.
EventProbabilityMap GetEventProbability(const History& history) {
  size_t instances_count = 0;
  for (const std::vector<FeaturesSet>& instances : std::views::values(history))
    instances_count += instances.size();

  EventProbabilityMap output;
  for (const auto& [event, instances] : history)
    output.insert({event, Probability(static_cast<double>(instances.size()) /
                                      instances_count)});
  return output;
}

const bool get_probability_of_event_tests_registration =
    tests::Register(L"GetEventProbabilityTests", [] {
      Event e0(L"e0"), e1(L"e1"), e2(L"e2");
      Feature f1(L"f1"), f2(L"f2"), f3(L"f3"), f4(L"f4"), f5(L"f5");
      return std::vector<tests::Test>(
          {{.name = L"Empty",
            .callback =
                [] { CHECK_EQ(GetEventProbability(History()).size(), 0ul); }},
           {.name = L"SingleEventSingleInstance",
            .callback =
                [=] {
                  auto result = GetEventProbability(
                      History{{{e0, {FeaturesSet({f1, f2})}}}});
                  CHECK_EQ(result.size(), 1ul);
                  CHECK_EQ(result.count(e0), 1ul);
                  CHECK_EQ(result.find(e0)->second, Probability(1.0));
                }},
           {.name = L"SingleEventMultipleInstance",
            .callback =
                [=] {
                  auto result =
                      GetEventProbability(History{{{e0,
                                                    {
                                                        FeaturesSet({f1, f2}),
                                                        FeaturesSet({f1}),
                                                        FeaturesSet({f2}),
                                                    }}}});
                  CHECK_EQ(result.size(), 1ul);
                  CHECK_EQ(result.count(e0), 1ul);
                  CHECK_EQ(result.find(e0)->second, Probability(1.0));
                }},
           {.name = L"MultipleEvents", .callback = [=] {
              auto result = GetEventProbability(History{{{e0,
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

              CHECK_EQ(result.count(e0), 1ul);
              CHECK_EQ(result.find(e0)->second, Probability(0.5));

              CHECK_EQ(result.count(e1), 1ul);
              CHECK_EQ(result.find(e1)->second, Probability(0.4));

              CHECK_EQ(result.count(e2), 1ul);
              CHECK_EQ(result.find(e2)->second, Probability(0.1));
            }}});
    }());

FeatureProbabilityMap GetFeatureProbability(
    const std::vector<FeaturesSet>& instances) {
  std::unordered_map<Feature, size_t> feature_count;
  for (const auto& instance : instances)
    for (const auto& feature : instance) feature_count[feature]++;
  FeatureProbabilityMap output;
  for (const auto& [feature, count] : feature_count)
    output.insert(
        {feature, Probability(static_cast<double>(count) / instances.size())});
  return output;
}

const bool get_probability_of_feature_given_event_tests_registration =
    tests::Register(L"GetPerEventFeatureProbabilityTests", []() {
      Event e0(L"e0"), e1(L"e1");
      Feature f1(L"f1"), f2(L"f2"), f3(L"f3");
      return std::vector<tests::Test>(
          {{.name = L"Empty",
            .callback =
                [] { CHECK_EQ(GetFeatureProbability({}).size(), 0ul); }},
           {.name = L"SingleEventSingleInstance",
            .callback =
                [=] {
                  auto result = GetFeatureProbability({FeaturesSet({f1, f2})});
                  CHECK_EQ(result.size(), 2ul);

                  CHECK_EQ(result.count(f1), 1ul);
                  CHECK_EQ(result[f1], Probability(1.0));

                  CHECK_EQ(result.count(f2), 1ul);
                  CHECK_EQ(result[f2], Probability(1.0));
                }},
           {.name = L"SingleEventMultipleInstances", .callback = [=] {
              auto result = GetFeatureProbability({
                  FeaturesSet({f1, f2, f3}),
                  FeaturesSet({f1, f2}),
                  FeaturesSet({f1}),
                  FeaturesSet({f1}),
                  FeaturesSet({f1}),
              });
              CHECK_EQ(result.size(), 3ul);

              CHECK_EQ(result.count(f1), 1ul);
              CHECK_EQ(result[f1], Probability(1.0));

              CHECK_EQ(result.count(f2), 1ul);
              CHECK_EQ(result[f2], Probability(0.4));

              CHECK_EQ(result.count(f3), 1ul);
              CHECK_EQ(result[f3], Probability(0.2));
            }}});
    }());

Probability MinimalFeatureProbability(
    std::unordered_map<Event, FeatureProbabilityMap>
        probability_of_feature_given_event) {
  Probability output(1.0);
  for (auto& [_0, features] : probability_of_feature_given_event)
    for (auto& [_1, value] : features) output = std::min(output, value);
  return output;
}

const bool minimal_feature_probability_tests_registration = tests::Register(
    L"MinimalFeatureProbabilityTests",
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
  // Let F = f0, f1, ..., fn be the set of current features. We'd like to
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
  //   = p(f0 ∩ f1 ∩ f2 ∩ ... fn ∩ eᵢ)
  //   = p(f0 | (f1 ∩ f2 ∩ ... fn ∩ eᵢ)) *
  //     p(f1 | (f2 ∩ ... ∩ fn ∩ eᵢ)) *
  //     ... *
  //     p(fn | eᵢ) *
  //     p(eᵢ)
  //
  // The naive assumption lets us simplify to p(fj | eᵢ) the expression:
  //
  //   p(fj | f(j+1) ∩ f(j+2) ∩ ... fn ∩ eᵢ)
  //
  // So (1) simplifies to:
  //
  //     p(eᵢ ∩ F)
  //   = p(f0 | eᵢ) * ... * p(fn | eᵢ) * p(eᵢ)
  //   = p(eᵢ) Πj p(fj | eᵢ)
  //
  // Πj denotes the multiplication over all values j.
  //
  // There's a small catch. For features absent from eᵢ's history (that is, for
  // features fj where p(fj|eᵢ) is 0), we don't want to fully discard eᵢ (i.e.,
  // we don't want to assign it a proportional probability of 0). If we did
  // that, sporadic features would be given too much weight. To achieve this, we
  // compute a small value epsilon and use:
  //
  //     p(eᵢ, F) = p(eᵢ) Πj max(epsilon, p(fj | eᵢ))
  static infrastructure::Tracker tracker(
      L"NaiveBayes::SortByProportionalProbability");
  auto call = tracker.Call();

  // p(eᵢ):
  EventProbabilityMap probability_of_event = GetEventProbability(history);

  // probability_of_feature_given_event[eᵢ][fj] represents a value p(fj | eᵢ):
  // the probability of feature fj given event eᵢ.
  std::unordered_map<Event, FeatureProbabilityMap>
      probability_of_feature_given_event;
  for (const auto& [event, features_sets] : history)
    probability_of_feature_given_event.insert(
        {event, GetFeatureProbability(features_sets)});

  const Probability epsilon =
      MinimalFeatureProbability(probability_of_feature_given_event) / 2;
  VLOG(5) << "Found epsilon: " << epsilon;

  std::unordered_map<Event, double> current_probability_value;
  for (const auto& [event, instances] : history) {
    Probability p = probability_of_event[event];
    const auto& feature_probability = probability_of_feature_given_event[event];
    for (const auto& feature : current_features) {
      auto it = feature_probability.find(feature);
      p *= it != feature_probability.end() ? it->second : epsilon;
    }
    VLOG(6) << "Current probability for " << event << ": " << p;
    current_probability_value[event] = p.read();
  }

  auto events = std::views::keys(history);
  std::vector<Event> output(events.begin(), events.end());

  sort(output.begin(), output.end(),
       [&current_probability_value](const Event& a, const Event& b) {
         return current_probability_value[a] < current_probability_value[b];
       });
  return output;
}

const bool bayes_sort_tests_probability_tests_registration =
    tests::Register(L"BayesSortTests", [] {
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
                 auto results = Sort(history, FeaturesSet());
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
                 auto results = Sort(history, FeaturesSet({f4}));
                 CHECK_EQ(results.size(), 2ul);
                 // TODO: Why is m0 more likely than m1?
                 CHECK_EQ(results.front(), e1);
                 CHECK_EQ(results.back(), e0);
               }},
          {.name = L"FeatureSelects",
           .callback =
               [=] {
                 auto results = Sort(History{{
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
                 auto results = Sort(History{{
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
                 auto results = Sort(History{{
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
}  // namespace afc::naive_bayes
