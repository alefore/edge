#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ghost_type.h"

namespace afc::naive_bayes {
// An Event represents an arbitrary action, such as opening a specific file.
class Event {
 public:
  Event(std::wstring name) : name(std::move(name)) {}

  GHOST_TYPE_EQ(Event, name);
  GHOST_TYPE_HASH_FRIEND(Event, name);

  // TODO: Find a way to make name private?
  std::wstring name;
};

// A Feature represents some arbitrary characteristic of the environment where
// events take place.
//
// Examples would be:
// - A given file is currently open.
// - Today is Wednesday.
// - A given process is currently executing.
class Feature {
 public:
  Feature(std::wstring name) : name(std::move(name)) {}

  GHOST_TYPE_EQ(Feature, name);
  GHOST_TYPE_HASH_FRIEND(Feature, name);

  // TODO: Find a way to make name private?
  std::wstring name;
};
}  // namespace afc::naive_bayes

GHOST_TYPE_HASH(afc::naive_bayes::Event, name);
GHOST_TYPE_HASH(afc::naive_bayes::Feature, name);

namespace afc::naive_bayes {

// FeaturesSet represents a set of features. Typically this is used to capture
// the state of an instance when an event was executed.
class FeaturesSet {
 public:
  explicit FeaturesSet(std::unordered_set<Feature> features)
      : features_(std::move(features)) {}

  GHOST_TYPE_EQ(FeaturesSet, features_);
  GHOST_TYPE_BEGIN_END(FeaturesSet, features_);

 private:
  std::unordered_set<Feature> features_;
};

// The history represents all the past executions of all events. For each
// execution, we store the set of features that were present.
using History = std::unordered_map<Event, std::vector<FeaturesSet>>;

// Given the history of all past executions of all events, apply Naive Bayes and
// return the list of all keys, sorted by their predicted proportional
// probability (in ascending order).
//
// The returned vector contains the keys of `history`.
std::vector<Event> Sort(const History& history,
                        const FeaturesSet& current_features);
}  // namespace afc::naive_bayes
