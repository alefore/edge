#include "src/naive_bayes.h"

#include "glog/logging.h"
#include "src/tests/tests.h"
#include "src/tracker.h"
#include "src/wstring.h"

namespace afc::naive_bayes {

namespace {
using History = std::unordered_map<std::wstring, std::vector<FeaturesSet>>;
using ProbabilityMap = std::unordered_map<std::wstring, double>;

// Returns the probability of each event in history.
ProbabilityMap GetEventProbability(const History& history) {
  size_t instances_count = 0;
  for (const auto& [event, instances] : history) {
    instances_count += instances.size();
  }

  ProbabilityMap output;
  for (const auto& [event, instances] : history) {
    output.insert(
        {event, static_cast<double>(instances.size()) / instances_count});
  }
  return output;
}

class GetEventProbabilityTests
    : public tests::TestGroup<GetEventProbabilityTests> {
 public:
  GetEventProbabilityTests() : TestGroup<GetEventProbabilityTests>() {}
  std::wstring Name() const override { return L"GetEventProbabilityTests"; }
  std::vector<tests::Test> Tests() const override {
    return {{.name = L"Empty",
             .callback = [] { CHECK_EQ(GetEventProbability({}).size(), 0ul); }},
            {.name = L"SingleEventSingleInstance",
             .callback =
                 [] {
                   History history;
                   history[L"m0"].push_back({L"foo", L"bar"});
                   auto result = GetEventProbability(history);
                   CHECK_EQ(result.size(), 1ul);
                   CHECK_EQ(result.count(L"m0"), 1ul);
                   CHECK_EQ(result.find(L"m0")->second, 1.0);
                 }},
            {.name = L"SingleEventMultipleInstance",
             .callback =
                 [] {
                   History history;
                   history[L"m0"] = {{L"foo", L"bar"}, {L"foo"}, {L"bar"}};
                   auto result = GetEventProbability(history);
                   CHECK_EQ(result.size(), 1ul);
                   CHECK_EQ(result.count(L"m0"), 1ul);
                   CHECK_EQ(result.find(L"m0")->second, 1.0);
                 }},
            {.name = L"MultipleEvents", .callback = [] {
               History history;
               history[L"m0"] = {{L"1"}, {L"2"}, {L"3"}, {L"4"}, {L"5"}};
               history[L"m1"] = {{L"1"}, {L"2"}, {L"3"}, {L"4"}};
               history[L"m2"] = {{L"1"}};
               auto result = GetEventProbability(history);
               CHECK_EQ(result.size(), 3ul);

               CHECK_EQ(result.count(L"m0"), 1ul);
               CHECK_EQ(result.find(L"m0")->second, 0.5);

               CHECK_EQ(result.count(L"m1"), 1ul);
               CHECK_EQ(result.find(L"m1")->second, 0.4);

               CHECK_EQ(result.count(L"m2"), 1ul);
               CHECK_EQ(result.find(L"m2")->second, 0.1);
             }}};
  }
};

template <>
const bool tests::TestGroup<GetEventProbabilityTests>::registration_ =
    tests::Add<naive_bayes::GetEventProbabilityTests>();

std::unordered_map<std::wstring, ProbabilityMap> GetPerEventFeatureProbability(
    const History& history) {
  std::unordered_map<std::wstring, ProbabilityMap> output;
  for (const auto& [event, instances] : history) {
    std::unordered_map<std::wstring, size_t> feature_count;
    for (const auto& instance : instances) {
      for (const auto& feature : instance) {
        feature_count[feature]++;
      }
    }
    ProbabilityMap* feature_probability = &output[event];
    for (const auto& [feature, count] : feature_count) {
      feature_probability->insert(
          {feature, static_cast<double>(count) / instances.size()});
      VLOG(8) << "Probability for " << feature << " given " << event << ": "
              << feature_probability->find(feature)->second;
    }
  }
  return output;
}
};  // namespace

std::vector<std::wstring> Sort(const History& history,
                               const FeaturesSet& current_features) {
  // Let F = f0, f1, ..., fn be the set of current features. For each key mi in
  // history (refered as an "Event"), we'd like to compute the probability of m1
  // given the features: p(mi | F). We know that:
  //
  //     p(mi | F) p(F) = p(mi, F)
  //
  // Since p(F) is the same for all i (and thus won't affect the computation for
  // mi for different values if i), we get rid of it.
  //
  //     p(mi | F) ~= p(mi, F)
  //
  // We know that (1):
  //
  //     p(mi, F)
  //   = p(f0, f1, f2, ... fn, mi)
  //   = p(f0 | f1, f2, ..., fn, mi) *
  //     p(f1 | f2, ..., fn, mi) *
  //     ... *
  //     p(fn | mi) *
  //     p(mi)
  //
  // The naive assumption lets us simplify to p(fj | mi) the expression:
  //
  //   p(fj | f(j+1), f(j+2), ... fn, mi)
  //
  // So (1) simplifies to:
  //
  //     p(mi, F)
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
  using Feature = std::wstring;
  using Event = std::wstring;

  static afc::editor::Tracker tracker(
      L"NaiveBayes::SortByProportionalProbability");
  auto call = tracker.Call();

  // p(mi):
  std::unordered_map<Event, double> event_probability =
      GetEventProbability(history);

  // feature_probability[mi][fj] represents a value p(fj | mi): the probability
  // of feature fj given value mi.
  std::unordered_map<Event, std::unordered_map<Feature, double>>
      per_event_feature_probability = GetPerEventFeatureProbability(history);

  double epsilon = 1.0;
  for (auto& [_, features] : per_event_feature_probability) {
    for (auto& [_, value] : features) {
      epsilon = std::min(epsilon, value);
    }
  }
  epsilon /= 2;
  VLOG(5) << "Found epsilon: " << epsilon;

  std::unordered_map<Event, double> current_probability_value;
  for (const auto& [event, instances] : history) {
    double p = event_probability[event];
    auto feature_probability = per_event_feature_probability[event];
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

  std::vector<std::wstring> output;
  for (const auto& [event, _] : history) {
    output.push_back(event);
  }
  sort(output.begin(), output.end(),
       [&current_probability_value](const std::wstring& a,
                                    const std::wstring& b) {
         return current_probability_value[a] < current_probability_value[b];
       });
  return output;
}
}  // namespace afc::naive_bayes
