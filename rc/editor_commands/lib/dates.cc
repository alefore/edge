// Useful functions for :-style completion.
string FormatDay(Time time) { return time.format("%Y-%m-%d"); }
string Yesterday() { return FormatDay(Now().AddDays(-1)); }
string Today() { return FormatDay(Now()); }
string Tomorrow() { return FormatDay(Now().AddDays(1)); }
