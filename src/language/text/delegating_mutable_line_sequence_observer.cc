#include "src/language/text/delegating_mutable_line_sequence_observer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

namespace afc::language::text {
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;

DelegatingMutableLineSequenceObserver::DelegatingMutableLineSequenceObserver(
    std::vector<Delegate> delegates)
    : delegates_(std::move(delegates)) {}

void DelegatingMutableLineSequenceObserver::LinesInserted(
    LineNumber position, LineNumberDelta delta) {
  for (auto& delegate : delegates_) delegate->LinesInserted(position, delta);
}

void DelegatingMutableLineSequenceObserver::LinesErased(LineNumber position,
                                                        LineNumberDelta delta) {
  for (auto& delegate : delegates_) delegate->LinesErased(position, delta);
}

void DelegatingMutableLineSequenceObserver::SplitLine(LineColumn position) {
  for (auto& delegate : delegates_) delegate->SplitLine(position);
}

void DelegatingMutableLineSequenceObserver::FoldedLine(LineColumn position) {
  for (auto& delegate : delegates_) delegate->FoldedLine(position);
}

void DelegatingMutableLineSequenceObserver::Sorted() {
  for (auto& delegate : delegates_) delegate->Sorted();
}

void DelegatingMutableLineSequenceObserver::AppendedToLine(
    LineColumn position) {
  for (auto& delegate : delegates_) delegate->AppendedToLine(position);
}

void DelegatingMutableLineSequenceObserver::DeletedCharacters(
    LineColumn position, ColumnNumberDelta delta) {
  for (auto& delegate : delegates_)
    delegate->DeletedCharacters(position, delta);
}

void DelegatingMutableLineSequenceObserver::SetCharacter(LineColumn position) {
  for (auto& delegate : delegates_) delegate->SetCharacter(position);
}

void DelegatingMutableLineSequenceObserver::InsertedCharacter(
    LineColumn position) {
  for (auto& delegate : delegates_) delegate->InsertedCharacter(position);
}
}  // namespace afc::language::text
