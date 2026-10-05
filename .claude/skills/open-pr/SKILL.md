---
name: open-pr
description: MANDATORY skill for ALL pull requests. Must be used EVERY TIME before creating any pull request. No exceptions.
---

# Generating Pull Requests

## Mandatory Process

**`--auto`** (caller runs autonomously): skip step 0 sign-off and any push/PR approval wait — proceed directly. Still fix push errors, still `--draft`.

0. **ALWAYS** ensure the change was verified before opening the PR. Minimum for C++ changes: `clang++ -fsyntax-only` clean on every touched translation unit (recipe in the [commit](../commit/SKILL.md) skill); `all/modules/*.i` touched → wrappers regenerated with `swigpp-java.py` and staged. Renderer/visual changes are **not** provable that way — say so explicitly and flag that an on-device/emulator check is still required, then propose the scenarios (camera, zoom/tilt, which intent extras) a reviewer should run.
1. **ALWAYS** `git push` and check for errors. There are no local hooks and no commitlint in CI, so nothing will catch a malformed title for you — the convention is enforced by you.
2. **ALWAYS** fix any errors — autofixup into the relevant commits, or create a new commit if autofixup does not apply.
3. **ALWAYS** create the PR as **draft**:
   1. This repo has **no** `.github/PULL_REQUEST_TEMPLATE.md`. Read it **only if one exists** and mirror its structure; otherwise write a clean default body (see "Writing the description" + "Default body" below).
   2. **CRITICAL — `--repo` is mandatory.** Every repo here is a fork of an **archived** CartoDB original; without `--repo`, `gh` targets upstream and fails with "Repository was archived so is read-only".
   3. **CRITICAL**: `--template` and `--body` are **mutually exclusive** in `gh pr create`. Always use `--body` with an inline multiline string, never `--template`:
      ```sh
      gh pr create --repo massif-maps/MassifMaps --base master --draft \
        --title "fix(terrain): re-clamp the elevation level for far tiles" --body "$(cat <<'EOF'
      ## Summary
      ...
      EOF
      )"
      ```
   4. **ALWAYS** use `--draft` — only the user decides when a PR is ready for review.
   5. The PR title **ALWAYS** follows the Conventional Commits header (`<type>(<scope>): <subject>`) — same convention as the [commit](../commit/SKILL.md) skill.
   6. **ALWAYS** reference the tracking issue in the body: `Fixes #<issue>` / `Closes #<issue>`.
   7. **Split SDK and styles work**: a branch touching `styles/massif` and anything the SDK ships is
      two PRs, the SDK one first — see the root [`CLAUDE.md`](../../../CLAUDE.md#a-pr-title-is-the-changelog-entry).

## Submodule work = one PR per repo

Work touching `libs-massif` / `libs-external` is **never** a single PR. Each repo gets its own branch (see [branch-check](../branch-check/SKILL.md)) and its own PR, and the submodule PR goes **first** — the main-repo PR is unreviewable until the reviewer can see the commit its pointer bump refers to.

| Repo          | `--repo`                         | `--base`  |
| ------------- | -------------------------------- | --------- |
| main SDK      | `massif-maps/MassifMaps`              | `master`  |
| carto libs    | `massif-maps/massif-maps-libs` | `develop` |
| external libs | `massif-maps/massif-external-libs`    | `develop` |

Order:

1. **Check the submodule branch is real** — `git -C libs-massif status -sb` must show `## <branch>...origin/<branch>`, not `## HEAD (no branch)`. On a detached HEAD the push says "Everything up-to-date" and the PR would be empty.
2. **Push and open the submodule PR** — same conventional title, scoped to its module (`fix(vt): …`):
   ```sh
   git -C libs-massif push -u origin <branch>
   gh pr create --repo massif-maps/massif-maps-libs --base develop --draft --title "fix(vt): ..." --body "..."
   ```
3. **Open the main-repo PR**, whose branch carries the pointer bump. Its title is the CHANGELOG
   entry for this work — the diff is one line, the title is not: name the user-visible outcome, not
   the bump. Rules and examples in the root [`CLAUDE.md`](../../../CLAUDE.md#a-massifmaps-pr-title-is-the-changelog-entry).
   **Cross-link both ways**: the main PR body gets a `## Depends on` line with the submodule PR URL;
   the submodule PR body gets "Consumed by <main PR URL>".
4. **Never merge in the wrong order** — submodule PR merges first, then the pointer bump is rebased onto the merged submodule commit (not the branch commit) before the main PR merges. Point this out in the main PR body; only the user merges.

## Writing the description

The description is for a human reviewer who needs to grasp _what this PR does_ at a glance. Write the kind of summary you'd write by hand.

- Summarize the **main changes only** — the meaningful, functional changes a reviewer needs to know about. A few clear bullet points or short sentences is enough.
- **NEVER dump commit details** — no commit-by-commit breakdown, no pasted commit messages. Git history already holds that.
- **Skip non-important changes** — small refactors, formatting, renames. They dilute the signal.
- For renderer / terrain / label changes, the reviewer cannot see the bug from the diff: name the **symptom**, the **mechanism**, and the **camera** that reproduces it (lon/lat/zoom/tilt + the intent extras), so they can check it themselves.
- If the description reads like a changelog of every diff, it's wrong. Clear, concise, high-level — that's the bar.

## Default body (no PR template)

```markdown
## Summary

<1-4 bullets of the meaningful changes>

## Testing

<clang -fsyntax-only clean on X/Y; what still needs an on-device check, and the camera/extras to reproduce with>

## Depends on

<submodule PR URL — omit the section when no submodule is involved>

Fixes #<issue>
```

Keep it lean. Add a `## Breaking changes` section **only** when the PR truly breaks something (impact + migration path) — a changed signature in `all/modules/*.i` is breaking for every app binding even when the C++ compiles.

## If a template ever gets added

If `.github/PULL_REQUEST_TEMPLATE.md` exists, mirror its structure instead. The `<!-- ... -->` HTML comments in it are **instructions to the author**, not content — strip every one out; they must never appear in the final PR body. Check (`[x]`) only checklist boxes that genuinely hold; never check a box that isn't true.
