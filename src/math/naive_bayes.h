#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"
namespace afc::math::naive_bayes {
using ::operator<<;

// An Event represents an arbitrary action, such as opening a specific file.
class Event
    : public language::GhostType<Event, language::lazy_string::LazyString> {
  using GhostType::GhostType;
};

// A Feature represents some arbitrary characteristic of the environment where
// events take place.
//
// Examples would be:
// - A given file is currently open.
// - Today is Wednesday.
// - A given process is currently executing.
class Feature
    : public language::GhostType<Feature, language::lazy_string::LazyString> {
  using GhostType::GhostType;
};

// FeaturesSet represents a set of features. Typically this is used to capture
// the state of the environment in which an event is executed.
class FeaturesSet
    : public language::GhostType<FeaturesSet, std::unordered_set<Feature>> {};

// The history represents all the past executions of all events. For each
// execution, we store the set of features that were present.
class History
    : public language::GhostType<
          History, std::unordered_map<Event, std::vector<FeaturesSet>>> {
  using GhostType::GhostType;
};

// Given the history of all past executions of all events and the current state,
// apply Naive Bayes to sort all events by their predicted proportional
// probability (in ascending order).
//
// The returned vector contains the keys of `history`.
std::vector<Event> Sort(const History& history,
                        const FeaturesSet& current_features);
}  // namespace afc::math::naive_bayes
