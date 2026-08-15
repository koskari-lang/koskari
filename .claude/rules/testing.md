# Testing Discipline

- All automated tests live under `tests/`.
- Do not use typing annotations in test modules.
- Tests exist to reveal defects. A failing test is useful evidence and must
  never be muted, skipped, xfailed, or pinned to the broken output.
- Never adjust fixtures, mocks, or assertions to mask a genuine failure.
  Skipping a test is a failure, not a resolution.
- When a test fails, diagnose it and report the failure here in chat with
  reproduction steps and the suspected root cause. Do not modify production
  code to make it pass unless explicitly instructed.
- A test expectation broken as a side effect of unrelated work is a stop
  signal. Report it and ask; do not rewrite the expectation.
- Native memory tests need a container: `make test-native`, `make test-asan`,
  `make test-valgrind`.

# The end.
