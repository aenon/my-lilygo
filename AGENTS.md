# Agent and development conventions

## Branch, worktree, and PR (1:1:1)

Keep a **strict one-to-one-to-one mapping**:

| Concept | Rule |
|--------|------|
| **Branch** | One Git branch per unit of work (feature, fix, or chore). No unrelated commits on that branch. |
| **Worktree** | One linked Git worktree per branch, under **`.worktrees/`** in this repository. |
| **PR** | When the work is ready for review, **exactly one** pull/merge request from that branch (not multiple PRs for the same branch re-opened with different scopes). |

This keeps context isolated: checkout, builds, and IDE state for a line of work do not collide with other branches.

### Worktree path naming

Map branch → directory so there is no ambiguity and paths stay single-level:

- Take the **full branch name** (e.g. `feat/photo-slideshow`).
- Replace every `/` with **`-`**.
- Prefix with **`.worktrees/`**.

Examples:

| Branch | Worktree path |
|--------|----------------|
| `feat/photo-slideshow` | `.worktrees/feat-photo-slideshow` |
| `fix/wifi-reconnect` | `.worktrees/fix-wifi-reconnect` |
| `main` | *(do not create a worktree for `main` here; use the primary clone)* |

Function for shells:

```bash
worktree_path_for_branch() {
  printf '%s' ".worktrees/$(echo "$1" | tr '/' '-')"
}
```

### Creating a new line of work

From the **primary** clone of this repo (not from inside another worktree, unless you know what you are doing):

```bash
BRANCH="feat/my-task"
WT="$(worktree_path_for_branch "$BRANCH")"   # or: WT=".worktrees/feat-my-task"

git fetch origin
git worktree add -b "$BRANCH" "$WT" origin/main   # or: main
cd "$WT"
```

If the branch **already exists**:

```bash
BRANCH="feat/my-task"
WT=".worktrees/$(echo "$BRANCH" | tr '/' '-')"
git worktree add "$WT" "$BRANCH"
cd "$WT"
```

### Cleaning up

When the branch is merged and you are done:

```bash
git worktree remove .worktrees/feat-my-task --force   # from primary clone, adjust path
git branch -d feat/my-task   # after merge
```

Remove only after the worktree is no longer needed; `--force` drops uncommitted changes in that worktree.

### PR discipline

- Open the PR from the **same branch** that corresponds to the **`.worktrees/...`** checkout.
- Prefer **one PR per branch** from first review through merge; split work by creating **new branches and new worktrees**, not by overloading one branch with unrelated changes.

### Cursor / automation

Project rules in **`.cursor/rules/`** reference this file so agents follow the same 1:1:1 convention.
