#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace afc::naive_bayes {

using FeaturesSet = std::unordered_set<std::wstring>;

// Given a history of "events", each executed when a set of features was present
// and the information about the currently present set of features, applies
// Naive Bayes to return a vector containing all the events, sorted by their
// predicted proportional probability (in ascending order).
//
// The returned vector contains the keys of `history`.
std::vector<std::wstring> Sort(
    const std::unordered_map<std::wstring, std::vector<FeaturesSet>>& history,
    const FeaturesSet& current_features);
}  // namespace afc::naive_bayes
