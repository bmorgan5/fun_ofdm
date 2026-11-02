# Prints lcov's coverage summaries as one table, with a column each for
# lines, functions and branches. Reads `lcov --summary` of the baseline, if
# there is one, then genhtml's output for the current report, and expects
# `src` (baseline or now) and, for the baseline, `commit` to be set before
# each file:
#
#   awk -f summary.awk src=baseline commit=<commit> <lcov summary> \
#     src=now <genhtml output>
#
# With a baseline, genhtml also counts the lines, functions and branches in
# each of its differential categories, like UNC (uncovered new code). They're
# listed in plain English, with their codes, which the HTML report uses too.

BEGIN {
  # Every category, most important first. New code no test runs, and code
  # tests no longer run, are always listed; the rest only when there are any.
  ncodes = split("UNC LBC UIC GNC GBC GIC CBC UBC ECB EUB DCB DUB", codes, " ")
  label["UNC"] = "New, not run"
  label["LBC"] = "Was run, now isn't"
  label["UIC"] = "Newly in the report, not run"
  label["GNC"] = "New, run"
  label["GBC"] = "Wasn't run, now is"
  label["GIC"] = "Newly in the report, run"
  label["CBC"] = "Unchanged, run"
  label["UBC"] = "Unchanged, not run"
  label["ECB"] = "Left the report, was run"
  label["EUB"] = "Left the report, wasn't run"
  label["DCB"] = "Deleted, was run"
  label["DUB"] = "Deleted, wasn't run"
  always["UNC"] = always["LBC"] = 1
}

# "  lines.......: 75.0% (1047 of 1396 lines)", or "... no data found".
/^ *(lines|functions|branches)\.+:/ {
  metric = $1
  sub(/\.+:$/, "", metric)
  if ($2 ~ /%$/) {
    rate[src, metric] = $2
    covered[src, metric] = substr($3, 2) " of " $5
  }
  next
}

# "       UBC....: 349", under the metric it counts.
src == "now" && /^ +[A-Z][A-Z][A-Z]\.+:/ {
  count[metric, substr($1, 1, 3)] = $2
  found[substr($1, 1, 3)] = 1
}

function row(name, a, b, c) {
  printf "%-36s %-14s %-14s %s\n", name, a, b, c
}

function value(array, key) {
  return (key in array) ? array[key] : "-"
}

function change(metric) {
  if (!(("baseline", metric) in rate) || !(("now", metric) in rate)) return "-"
  return sprintf("%+.1f", rate["now", metric] - rate["baseline", metric])
}

END {
  row("", "Lines", "Functions", "Branches")
  if (commit != "") {
    row("Baseline (" substr(commit, 1, 12) ")",
        value(rate, "baseline" SUBSEP "lines"),
        value(rate, "baseline" SUBSEP "functions"),
        value(rate, "baseline" SUBSEP "branches"))
  }
  row(commit != "" ? "Now" : "Coverage",
      value(rate, "now" SUBSEP "lines"),
      value(rate, "now" SUBSEP "functions"),
      value(rate, "now" SUBSEP "branches"))
  if (commit != "") {
    row("Change", change("lines"), change("functions"), change("branches"))
  }
  row("Covered", value(covered, "now" SUBSEP "lines"),
      value(covered, "now" SUBSEP "functions"),
      value(covered, "now" SUBSEP "branches"))

  if (commit == "") exit
  print ""
  print "Since the baseline:"
  for (i = 1; i <= ncodes; i++) {
    code = codes[i]
    if (!(code in always) && !(code in found)) continue
    row("  " label[code] " (" code ")",
        value(count, "lines" SUBSEP code) + 0,
        value(count, "functions" SUBSEP code) + 0,
        value(count, "branches" SUBSEP code) + 0)
  }
}
