#include "src/naive_bayes.h"

#include "glog/logging.h"
#include "src/tests/tests.h"
#include "src/tracker.h"
#include "src/wstring.h"

namespace afc::naive_bayes {

namespace {
using EventProbabilityMap = std::unordered_map<Event, double>;
using FeatureProbabilityMap = std::unordered_map<Feature, double>;

// Returns the probability of each event in history.
EventProbabilityMap GetEventProbability(const History& history) {
  size_t instances_count = 0;
  for (const auto& [event, instances] : history) {
    instances_count += instances.size();
  }

  EventProbabilityMap output;
  for (const auto& [event, instances] : history) {
    output.insert(
        {event, static_cast<double>(instances.size()) / instances_count});
  }
  return output;
}

const bool get_event_probability_tests_registration = tests::Register(
    L"GetEventProbabilityTests",
    {{.name = L"Empty",
      .callback = [] { CHECK_EQ(GetEventProbability({}).size(), 0ul); }},
     {.name = L"SingleEventSingleInstance",
      .callback =
          [] {
            History history;
            history[Event(L"m0")].push_back(
                FeaturesSet({Feature(L"foo"), Feature(L"bar")}));
            auto result = GetEventProbability(history);
            CHECK_EQ(result.size(), 1ul);
            CHECK_EQ(result.count(Event(L"m0")), 1ul);
            CHECK_EQ(result.find(Event(L"m0"))->second, 1.0);
          }},
     {.name = L"SingleEventMultipleInstance",
      .callback =
          [] {
            History history;
            history[Event(L"m0")] = {
                FeaturesSet({Feature(L"foo"), Feature(L"bar")}),
                FeaturesSet({Feature(L"foo")}), FeaturesSet({Feature(L"bar")})};
            auto result = GetEventProbability(history);
            CHECK_EQ(result.size(), 1ul);
            CHECK_EQ(result.count(Event(L"m0")), 1ul);
            CHECK_EQ(result.find(Event(L"m0"))->second, 1.0);
          }},
     {.name = L"MultipleEvents", .callback = [] {
        History history;
        history[Event(L"m0")] = {
            FeaturesSet({Feature(L"1")}), FeaturesSet({Feature(L"2")}),
            FeaturesSet({Feature(L"3")}), FeaturesSet({Feature(L"4")}),
            FeaturesSet({Feature(L"5")})};
        history[Event(L"m1")] = {
            FeaturesSet({Feature(L"1")}), FeaturesSet({Feature(L"2")}),
            FeaturesSet({Feature(L"3")}), FeaturesSet({Feature(L"4")})};
        history[Event(L"m2")] = {FeaturesSet({Feature(L"1")})};
        auto result = GetEventProbability(history);
        CHECK_EQ(result.size(), 3ul);

        CHECK_EQ(result.count(Event(L"m0")), 1ul);
        CHECK_EQ(result.find(Event(L"m0"))->second, 0.5);

        CHECK_EQ(result.count(Event(L"m1")), 1ul);
        CHECK_EQ(result.find(Event(L"m1"))->second, 0.4);

        CHECK_EQ(result.count(Event(L"m2")), 1ul);
        CHECK_EQ(result.find(Event(L"m2"))->second, 0.1);
      }}});

std::unordered_map<Event, FeatureProbabilityMap> GetPerEventFeatureProbability(
    const History& history) {
  std::unordered_map<Event, FeatureProbabilityMap> output;
  for (const auto& [event, instances] : history) {
    std::unordered_map<Feature, size_t> feature_count;
    for (const auto& instance : instances) {
      for (const auto& feature : instance) {
        feature_count[feature]++;
      }
    }
    FeatureProbabilityMap* feature_probability = &output[event];
    for (const auto& [feature, count] : feature_count) {
      feature_probability->insert(
          {feature, static_cast<double>(count) / instances.size()});
      VLOG(8) << "Probability for " << feature << " given " << event << ": "
              << feature_probability->find(feature)->second;
    }
  }
  return output;
}

const bool get_per_event_feature_probability_tests_registration =
    tests::Register(
        L"GetPerEventFeatureProbabilityTests",
        {{.name = L"Empty",
          .callback =
              [] { CHECK_EQ(GetPerEventFeatureProbability({}).size(), 0ul); }},
         {.name = L"SingleEventSingleInstance",
          .callback =
              [] {
                History history;
                history[Event(L"m0")].push_back(
                    FeaturesSet({Feature(L"a"), Feature(L"b")}));
                auto result = GetPerEventFeatureProbability(history);
                CHECK_EQ(result.size(), 1ul);
                CHECK_EQ(result.count(Event(L"m0")), 1ul);
                CHECK_EQ(result[Event(L"m0")].size(), 2ul);

                CHECK_EQ(result[Event(L"m0")].count(Feature(L"a")), 1ul);
                CHECK_EQ(result[Event(L"m0")][Feature(L"a")], 1.0);

                CHECK_EQ(result[Event(L"m0")].count(Feature(L"b")), 1ul);
                CHECK_EQ(result[Event(L"m0")][Feature(L"b")], 1.0);
              }},
         {.name = L"SingleEventMultipleInstances",
          .callback =
              [] {
                History history;
                history[Event(L"m0")] = {
                    FeaturesSet({Feature(L"a"), Feature(L"b"), Feature(L"c")}),
                    FeaturesSet({Feature(L"a"), Feature(L"b")}),
                    FeaturesSet({Feature(L"a")}), FeaturesSet({Feature(L"a")}),
                    FeaturesSet({Feature(L"a")})};
                auto result = GetPerEventFeatureProbability(history);
                CHECK_EQ(result.size(), 1ul);
                CHECK_EQ(result.count(Event(L"m0")), 1ul);
                CHECK_EQ(result[Event(L"m0")].size(), 3ul);

                CHECK_EQ(result[Event(L"m0")].count(Feature(L"a")), 1ul);
                CHECK_EQ(result[Event(L"m0")][Feature(L"a")], 1.0);

                CHECK_EQ(result[Event(L"m0")].count(Feature(L"b")), 1ul);
                CHECK_EQ(result[Event(L"m0")][Feature(L"b")], 0.4);

                CHECK_EQ(result[Event(L"m0")].count(Feature(L"c")), 1ul);
                CHECK_EQ(result[Event(L"m0")][Feature(L"c")], 0.2);
              }},
         {.name = L"MultipleEventMultipleInstance", .callback = [] {
            History history;
            history[Event(L"m0")] = {
                FeaturesSet({Feature(L"a"), Feature(L"b"), Feature(L"c")}),
                FeaturesSet({Feature(L"a"), Feature(L"b")}),
                FeaturesSet({Feature(L"a")}), FeaturesSet({Feature(L"a")}),
                FeaturesSet({Feature(L"a")})};
            history[Event(L"m1")] = {
                FeaturesSet({Feature(L"a"), Feature(L"b"), Feature(L"c")}),
                FeaturesSet({Feature(L"c")})};

            auto result = GetPerEventFeatureProbability(history);

            CHECK_EQ(result.size(), 2ul);
            CHECK_EQ(result.count(Event(L"m0")), 1ul);
            CHECK_EQ(result.count(Event(L"m1")), 1ul);
            CHECK_EQ(result[Event(L"m0")].size(), 3ul);
            CHECK_EQ(result[Event(L"m1")].size(), 3ul);

            CHECK_EQ(result[Event(L"m0")].count(Feature(L"a")), 1ul);
            CHECK_EQ(result[Event(L"m0")][Feature(L"a")], 1.0);

            CHECK_EQ(result[Event(L"m0")].count(Feature(L"b")), 1ul);
            CHECK_EQ(result[Event(L"m0")][Feature(L"b")], 0.4);

            CHECK_EQ(result[Event(L"m0")].count(Feature(L"c")), 1ul);
            CHECK_EQ(result[Event(L"m0")][Feature(L"c")], 0.2);

            CHECK_EQ(result[Event(L"m1")].count(Feature(L"a")), 1ul);
            CHECK_EQ(result[Event(L"m1")][Feature(L"a")], 0.5);

            CHECK_EQ(result[Event(L"m1")].count(Feature(L"c")), 1ul);
            CHECK_EQ(result[Event(L"m1")][Feature(L"c")], 1.0);
          }}});

double MinimalFeatureProbability(
    std::unordered_map<Event, FeatureProbabilityMap>
        per_event_feature_probability) {
  double output = 1.0;
  for (auto& [_, features] : per_event_feature_probability) {
    for (auto& [_, value] : features) {
      output = std::min(output, value);
    }
  }
  return output;
}

const bool minimal_feature_probability_tests_registration = tests::Register(
    L"MinimalFeatureProbabilityTests",
    {{.name = L"Empty",
      .callback = [] { CHECK_EQ(MinimalFeatureProbability({}), 1.0); }},
     {.name = L"SomeData", .callback = [] {
        std::unordered_map<Event, FeatureProbabilityMap> data;
        data[Event(L"m0")][Feature(L"a")] = 0.2;
        data[Event(L"m0")][Feature(L"b")] = 0.8;
        data[Event(L"m1")][Feature(L"a")] = 0.8;
        data[Event(L"m1")][Feature(L"b")] = 0.5;
        data[Event(L"m2")][Feature(L"a")] = 0.1;  // <--- Minimal.
        data[Event(L"m2")][Feature(L"b")] = 0.5;
        CHECK_EQ(MinimalFeatureProbability(data), 0.1);
      }}});
}  // namespace

std::vector<Event> Sort(const History& history,
                        const FeaturesSet& current_features) {
  // Let F = f0, f1, ..., fn be the set of current features. We'd like to
  // compute the probability of each event mi in history given current_features:
  // p(mi | F).
  //
  // We know that:
  //
  //     p(mi | F) p(F) = p(mi ∩ F)                         (1)
  //
  // Since p(F) is the same for all i (and thus won't affect the computation for
  // mi for different values if i), we get rid of it.
  //
  //     p(mi | F) ~= p(mi ∩ F)
  //
  // We know that (1):
  //
  //     p(mi ∩ F)
  //   = p(f0 ∩ f1 ∩ f2 ∩ ... fn ∩ mi)
  //   = p(f0 | (f1 ∩ f2 ∩ ... fn ∩ mi)) *
  //     p(f1 | (f2 ∩ ... ∩ fn ∩ mi)) *
  //     ... *
  //     p(fn | mi) *
  //     p(mi)
  //
  // The naive assumption lets us simplify to p(fj | mi) the expression:
  //
  //   p(fj | f(j+1) ∩ f(j+2) ∩ ... fn ∩ mi)
  //
  // So (1) simplifies to:
  //
  //     p(mi ∩ F)
  //   = p(f0 | mi) * ... * p(fn | mi) * p(mi)
  //   = p(mi) Πj p(fj | mi)
  //
  // Πj denotes the multiplication over all values j.
  //
  // There's a small catch. For features absent from mi's history (that is, for
  // features fj where p(fj|mi) is 0), we don't want to fully discard mi (i.e.,
  // we don't want to assign it a proportional probability of 0). If we did
  // that, sporadic features would be given too much weight. To achieve this, we
  // compute a small value epsilon and use:
  //
  //     p(mi, F) = p(mi) Πj max(epsilon, p(fj | mi))
  static afc::editor::Tracker tracker(
      L"NaiveBayes::SortByProportionalProbability");
  auto call = tracker.Call();

  // p(mi):
  EventProbabilityMap event_probability = GetEventProbability(history);

  // per_event_feature_probability[mi][fj] represents a value p(fj | mi): the
  // probability of feature fj given event mi.
  std::unordered_map<Event, FeatureProbabilityMap>
      per_event_feature_probability = GetPerEventFeatureProbability(history);

  const double epsilon =
      MinimalFeatureProbability(per_event_feature_probability) / 2;
  VLOG(5) << "Found epsilon: " << epsilon;

  std::unordered_map<Event, double> current_probability_value;
  for (const auto& [event, instances] : history) {
    double p = event_probability[event];
    const auto& feature_probability = per_event_feature_probability[event];
    for (const auto& feature : current_features) {
      if (auto it = feature_probability.find(feature);
          it != feature_probability.end()) {
        VLOG(9) << event << ": Feature " << feature
                << " contributes prob: " << it->second;
        p *= it->second;
      } else {
        VLOG(9) << event << ": Feature " << feature << " contributes epsilon.";
        p *= epsilon;
      }
    }
    VLOG(6) << "Current probability for " << event << ": " << p;
    current_probability_value[event] = p;
  }

  std::vector<Event> output;
  for (const auto& [event, _] : history) {
    output.push_back(event);
  }
  sort(output.begin(), output.end(),
       [&current_probability_value](const Event& a, const Event& b) {
         return current_probability_value[a] < current_probability_value[b];
       });
  return output;
}

const bool bayes_sort_tests_probability_tests_registration = tests::Register(
    L"BayesSortTests",
    {
        {.name = L"EmptyHistoryAndFeatures",
         .callback = [] { CHECK_EQ(Sort({}, FeaturesSet({})).size(), 0ul); }},
        {.name = L"EmptyHistory",
         .callback =
             [] {
               CHECK_EQ(
                   Sort({}, FeaturesSet({Feature(L"a"), Feature(L"b")})).size(),
                   0ul);
             }},
        {.name = L"EmptyFeatures",
         .callback =
             [] {
               History history;
               history[Event(L"m0")] = {FeaturesSet({Feature(L"a")}),
                                        FeaturesSet({Feature(L"b")})};
               history[Event(L"m1")] = {FeaturesSet({Feature(L"c")})};
               auto results = Sort(history, FeaturesSet({}));
               CHECK_EQ(results.size(), 2ul);
               // TODO: Why can't I use CHECK_EQ below?
               CHECK(results.front() == Event(L"m1"));
               CHECK(results.back() == Event(L"m0"));
             }},
        {.name = L"NewFeature",
         .callback =
             [] {
               History history;
               history[Event(L"m0")] = {FeaturesSet({Feature(L"a")}),
                                        FeaturesSet({Feature(L"b")})};
               history[Event(L"m1")] = {FeaturesSet({Feature(L"c")})};
               auto results = Sort(history, FeaturesSet({Feature(L"d")}));
               CHECK_EQ(results.size(), 2ul);
               // TODO: Why can't I use CHECK_EQ below?
               // TODO: Why is m0 more likely than m1?
               CHECK(results.front() == Event(L"m1"));
               CHECK(results.back() == Event(L"m0"));
             }},
        {.name = L"FeatureSelects",
         .callback =
             [] {
               History history;
               history[Event(L"m0")] = {FeaturesSet({Feature(L"a")}),
                                        FeaturesSet({Feature(L"b")})};
               history[Event(L"m1")] = {FeaturesSet({Feature(L"c")})};
               auto results = Sort(history, FeaturesSet({Feature(L"c")}));
               CHECK_EQ(results.size(), 2ul);
               // TODO: Why can't I use CHECK_EQ below?
               CHECK(results.front() == Event(L"m0"));
               CHECK(results.back() == Event(L"m1"));
             }},
        {.name = L"FeatureSelectsSomeOverlap",
         .callback =
             [] {
               History history;
               history[Event(L"m0")] = {FeaturesSet({Feature(L"a")}),
                                        FeaturesSet({Feature(L"b")})};
               history[Event(L"m1")] = {FeaturesSet({Feature(L"a")})};
               auto results = Sort(history, FeaturesSet({Feature(L"b")}));
               CHECK_EQ(results.size(), 2ul);
               // TODO: Why can't I use CHECK_EQ below?
               CHECK(results.front() == Event(L"m1"));
               CHECK(results.back() == Event(L"m0"));
             }},
        {.name = L"FeatureSelectsFive",
         .callback =
             [] {
               History history;
               history[Event(L"m0")] = {
                   FeaturesSet({Feature(L"1")}),
                   FeaturesSet({Feature(L"a"), Feature(L"red")}),
                   FeaturesSet({Feature(L"2")})};
               history[Event(L"m1")] = {FeaturesSet({Feature(L"a")}),
                                        FeaturesSet({Feature(L"red")}),
                                        FeaturesSet({Feature(L"a")})};
               history[Event(L"m2")] = {FeaturesSet({Feature(L"a")}),
                                        FeaturesSet({Feature(L"2")}),
                                        FeaturesSet({Feature(L"3")})};
               history[Event(L"m3")] = {
                   FeaturesSet({Feature(L"a"), Feature(L"2")}),
                   FeaturesSet({Feature(L"red")})};
               history[Event(L"m4")] = {FeaturesSet({Feature(L"4")})};
               auto results =
                   Sort(history, FeaturesSet({Feature(L"a"), Feature(L"red")}));
               CHECK_EQ(results.size(), 5ul);
               CHECK(results[4] == Event(L"m1"));
               CHECK(results[3] == Event(L"m3"));
               CHECK(results[2] == Event(L"m0"));
             }},
    });
}  // namespace afc::naive_bayes
