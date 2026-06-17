# Summarize cppcheck SARIF for make cppcheck console output.
# Usage: jq -r -f tools/cppcheck-sarif-summary.jq build/cppcheck.sarif

.runs[0].results as $r |
"cppcheck: \($r | length) findings in \($r | map(.locations[0].physicalLocation.artifactLocation.uri) | unique | length) files",
"",
"by level:",
($r | group_by(.level) | sort_by(-length)[] | "  \(.[0].level): \(length)"),
"",
"by rule (top 15):",
($r | group_by(.ruleId) | sort_by(-length) | .[:15][] | "  \(.[0].ruleId): \(length)")
