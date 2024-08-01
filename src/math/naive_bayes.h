#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/language/ghost_type.h"
#include "src/language/ghost_type_class.h"
#include "src/language/wstring.h"

namespace afc::math::naive_bayes {
using ::operator<<;

// An Event represents an arbitrary action, such as opening a specific file.
class Event : public language::GhostType<Event, std::wstring> {};

// A Feature represents some arbitrary characteristic of the environment where
// events take place.
//
// Examples would be:
// - A given file is currently open.
// - Today is Wednesday.
// - A given process is currently executing.
class Feature : public language::GhostType<Feature, std::wstring> {};
}  // namespace afc::math::naive_bayes

namespace afc::math::naive_bayes {
// FeaturesSet represents a set of features. Typically this is used to capture
// the state of the environment in which an event is executed.
GHOST_TYPE_CONTAINER(FeaturesSet, std::unordered_set<Feature>);

// The history represents all the past executions of all events. For each
// execution, we store the set of features that were present.
using InternalHistoryType = std::unordered_map<Event, std::vector<FeaturesSet>>;
GHOST_TYPE_CONTAINER(History, InternalHistoryType);

// Given the history of all past executions of all events and the current state,
// apply Naive Bayes to sort all events by their predicted proportional
// probability (in ascending order).
//
// The returned vector contains the keys of `history`.
std::vector<Event> Sort(const History& history,
                        const FeaturesSet& current_features);
}  // namespace afc::math::naive_bayes
