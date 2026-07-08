#!/usr/bin/env bash
# Usage: ./find-file-in-branches.sh <pattern>
# Searches every local and remote branch tree for files matching <pattern>
# (case-insensitive, extended regex). Also checks commit history and stashes.

set -euo pipefail

pattern="${1:-}"
if [[ -z "$pattern" ]]; then
    echo "Usage: $0 <pattern>" >&2
    echo "Example: $0 '03-create-simple.tube.stl.sh'" >&2
    exit 1
fi

echo "=== Searching branch trees for: $pattern ==="
found=0
while read -r branch; do
    matches=$(git ls-tree -r --name-only "$branch" 2>/dev/null | grep -iE "$pattern" || true)
    if [[ -n "$matches" ]]; then
        echo "BRANCH: $branch"
        echo "$matches" | sed 's/^/  /'
        found=1
    fi
done < <(git for-each-ref --format='%(refname:short)' refs/heads/ refs/remotes/)

echo
echo "=== Checking full commit history (incl. deleted files) ==="
git log --all --oneline --name-only --diff-filter=A 2>/dev/null \
    | grep -iE "$pattern" | sort -u || true

echo
echo "=== Checking stashes ==="
git stash list | while read -r stash; do
    ref="${stash%%:*}"
    matches=$(git stash show --name-only "$ref" 2>/dev/null | grep -iE "$pattern" || true)
    [[ -n "$matches" ]] && echo "$ref: $matches"
done

[[ $found -eq 0 ]] && echo "(no branch tree matches)"
