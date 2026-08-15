---
name: pull-request
description: Create a GitHub pull request for the current branch with gh. Use only when the user explicitly requests a PR.
disable-model-invocation: true
tools: Bash
---

# Pull Request

Constraints live in [.claude/rules/git.md](../../rules/git.md).

## Never push

Pushing is the user's operation, in every case, including when they ask.
Do not push, do not offer to push, do not work around an unpushed branch.

## If the branch is not pushed

Check first:

```bash
git --no-pager status -sb
```

If the branch has no upstream, or is ahead of its upstream, stop. Report that
the pull request cannot be created because the branch has not been pushed,
and wait. Do not attempt to push it yourself.

Worktree branches carry randomly generated local names. Resolve `@{upstream}`
to get the real remote branch name before referring to it.

## Building the body

Read the diff locally. Do not query the remote for it:

```bash
git --no-pager diff master...HEAD --stat
git --no-pager diff master...HEAD
```

Keep remote traffic to the minimum the `gh` call itself requires.

## Creating it

Use command substitution with a quoted heredoc, so backticks, `$`, and other
shell metacharacters in the body survive intact:

```bash
gh pr create --base master --head branch-name \
  --title "PR Title Here" \
  --body "$(cat << 'EOF'
Multi-line description here, with `backticks`, $variables, and
whatever else, none of which bash will try to interpret.

## Section Headers
- Bullet points
- Code references like `file.py`
EOF
)"
```

The quoted delimiter `'EOF'` is what prevents expansion. The same pattern
works for `gh issue create --body`.

Verify with `gh pr view NUMBER`.

## Editing an existing PR

Never edit, comment on, or otherwise modify an existing PR without explicit
per-act authorization from the user, even when the change seems like an
obvious improvement to one you just created.

When authorized, the same heredoc pattern applies:

```bash
gh pr edit PR_NUMBER --body "$(cat << 'EOF'
Updated description
EOF
)"
```
