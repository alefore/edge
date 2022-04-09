#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ghost_type.h"
#include "src/wstring.h"

namespace afc::naive_bayes {
using ::operator<<;

// An Event represents an arbitrary action, such as opening a specific file.
GHOST_TYPE(Event, std::wstring);

// A Feature represents some arbitrary characteristic of the environment where
// events take place.
//
// Examples would be:
// - A given file is currently open.
// - Today is Wednesday.
// - A given process is currently executing.
GHOST_TYPE(Feature, std::wstring);
}  // namespace afc::naive_bayes

GHOST_TYPE_HASH(afc::naive_bayes::Event);
GHOST_TYPE_HASH(afc::naive_bayes::Feature);

namespace afc::naive_bayes {

// FeaturesSet represents a set of features. Typically this is used to capture
// the state of an instance when an event was executed.
GHOST_TYPE_CONTAINER(FeaturesSet, std::unordered_set<Feature>);

// The history represents all the past executions of all events. For each
// execution, we store the set of features that were present.
using InternalHistoryType = std::unordered_map<Event, std::vector<FeaturesSet>>;
GHOST_TYPE_CONTAINER(History, InternalHistoryType);

// Given the history of all past executions of all events, apply Naive Bayes and
// return the list of all keys, sorted by their predicted proportional
// probability (in ascending order).
//
// The returned vector contains the keys of `history`.
std::vector<Event> Sort(const History& history,
                        const FeaturesSet& current_features);
}  // namespace afc::naive_bayes
