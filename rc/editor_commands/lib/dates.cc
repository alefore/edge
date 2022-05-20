// Useful functions for :-style completion.
string FormatDay(Time time) { return time.format("%Y-%m-%d"); }
string Yesterday() { return FormatDay(Now().AddDays(-1)); }
string Today() { return FormatDay(Now()); }
string Tomorrow() { return FormatDay(Now().AddDays(1)); }
string now() { return Now().format("%Y-%m-%d %H:%M"); }

// Receives the dates as ISO yyyy-mm-dd strings and returns an approximation of
// the number of days between them.
double Days(string date_a, string date_b) {
  return DurationBetween(ParseTime(date_a, "%F"), ParseTime(date_b, "%F"))
      .days();
}
