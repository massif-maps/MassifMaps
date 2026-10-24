---
name: commit
description: MANDATORY skill for ALL commits. Must be used EVERY TIME before creating any git commit. No exceptions.
---

# Generating Commit Messages

## Mandatory Process

**BEFORE ANY git commit COMMAND:**

1. **ALWAYS** run `git diff --staged` first to see changes. When the work spans submodules, do it per repo: `git -C libs-massif diff --staged`, `git -C libs-external diff --staged`.
2. **ALWAYS** analyze the staged changes thoroughly
3. **ALWAYS** split the code changes into atomic commits, one per coherent / cohesive change. One change spanning several files (`all/native` layer + renderer + the SWIG `.i` + its generated wrapper) is ONE cohesive change — do not split it by file. Only split when changes are truly unrelated (e.g. a renderer fix + a demo-app knob + a docs update).
4. **ALWAYS** run the checks relevant to the change before committing (this repo has **no test/lint/format scripts** — `package.json` has no real `test`):
   - **Syntax/type check every touched `.cpp`** — the cheap gate that catches most breakage without a 1h build:
     ```sh
     clang++ -fsyntax-only -std=c++17 -I all/native -I libs-massif/vt/src -I libs-massif/mapnikvt/src \
       -I libs-massif/cartocss/src -I libs-massif/nml/src -I libs-external/cglib -I libs-external/stdext \
       -I libs-external/boost -I libs-external/picojson -I libs-external/pbf -I libs-external/tinyformat \
       -I libs-external/utf8/source -I libs-external/angle-metal/include <file>.cpp
     ```
     (add `-DTARGET_OS_ANDROID` for Android-only paths; `boost` is only needed for `boost::math::constants::pi` in `vt` — a stub header suffices)
   - **Touched `all/modules/*.i`** → the checked-in wrappers under `generated/` are stale until regenerated, and gradle never runs SWIG. Regenerate and stage the result in the same commit:
     ```sh
     cd scripts && python3 swigpp-java.py --profile "standard+objectapi+valhalla+geocoding+routing+packagemanager" --swig /Volumes/dev/carto/mobile-swig/swig
     ```
   - **Renderer / visual changes** cannot be proven by a syntax check. State plainly that an on-device/emulator check is still required (see the [document](../document/SKILL.md) skill's A/B method and the root `CLAUDE.md`), and don't claim it was verified when it wasn't.
5. **ALWAYS** generate a commit message following the format below
6. **NEVER** commit automatically as a side effect of making code changes. Only commit when the user explicitly invokes the commit skill or says "commit".

## Submodule commits — order matters

`libs-massif` and `libs-external` are separate repos. A change in one is **two commits in two repos**, in this order:

1. **Commit inside the submodule first**, on its own branch (see [branch-check](../branch-check/SKILL.md)):
   ```sh
   git -C libs-massif status -sb          # MUST NOT print "## HEAD (no branch)"
   git -C libs-massif add <paths> && git -C libs-massif commit
   ```
   A commit on a detached HEAD is lost work — check `status -sb` again after committing.
2. **Then commit the pointer bump in the main repo.** The submodule path (`libs-massif`) shows up as a modified entry; stage it with the main-repo changes that need it, so `master` never points at a commit that doesn't build. Mention the submodule commit in the body:
   ```
   Requires libs-massif <short-sha> (vt: clamp elevation level per tile).
   ```
3. Never stage `libs-external`'s stray nested pointers (`brotli/brotli`, `date/date`) — they are dirty for unrelated reasons.

Both submodule and pointer-bump commits are presented in the same commit plan below, as one reviewable unit.

## Confirmation Before Committing

**`--auto`** (caller runs autonomously): skip all approval/confirmation waits here (commit-plan gate below + fixup/rebase) — commit directly. Diff review, atomic splitting, checks, message format unchanged.

User trust requires seeing the plan before execution. Always present the full commit plan and wait for explicit approval before running any `git commit` command.

**For each commit (regular or fixup), present:**

- The repo it lands in (main / `libs-massif` / `libs-external`)
- The commit message (header + body if applicable)
- The list of files included
- If splitting into multiple commits: the full split plan (which files go in which commit, in what order)
- If fixup: which commit SHA it targets and why

**Then ask the user to confirm.** Do not proceed until they approve. If they request changes to the message or grouping, adjust and re-present.

This applies equally to regular commits, fixups, and any commits triggered during the open-pr workflow.

## Auto-Fixup Detection

Before creating a new commit, check whether the staged changes should be fixup'd into a recent commit on the current branch.

**Process:**

1. Run `git log master..HEAD --oneline` to list all commits on the branch since diverging from `master` (in a submodule: `git -C libs-massif log develop..HEAD --oneline`)
2. For each staged file, check `git log master..HEAD -- <file>` to see if it was modified in a recent branch commit
3. If a staged change clearly amends or extends code from a previous commit (same file, nearby lines, related logic — e.g. fixing a bias constant introduced in a prior commit, adding a missing include for a recently added header), suggest fixup'ing into that commit
4. Present the suggestion: "This change to `<file>` looks like it should be fixup'd into `<sha> <message>`. Want me to fixup instead of creating a new commit?"

**When fixup is confirmed:**

1. Run `git commit --fixup=<sha>` (with user confirmation)
2. Then run `GIT_SEQUENCE_EDITOR=true git rebase --interactive --autosquash master` to squash immediately (with user confirmation before the rebase)

If the change doesn't clearly relate to a previous commit, proceed with a normal new commit.

## Required Commit Message Format

This repo uses Conventional Commits (by convention — there is no commitlint here, so the discipline is yours). The format is fixed:

```
<type>(<scope>): <subject>
<BLANK LINE>
<body>
<BLANK LINE>
<footer>
```

The **header** is mandatory; **scope**, **body**, and **footer** are optional.

### Header

**Shape:** `<type>(<scope>): <subject>`

- `<type>` is one of: `build` `chore` `ci` `docs` `feat` `fix` `perf` `refactor` `revert` `style` `test`
- `<scope>` (optional) is the affected area. Use the SDK subsystem, not a file path — the scopes already in the history: `terrain`, `layers`, `datasources`, `network`, `renderers`, `vt`, `labels`, `demo`, `docs`. When a change is platform-specific, prefix with the platform: `android/network`, `ios/ui`. A submodule commit is scoped by its own module (`vt`, `mapnikvt`, `cartocss`, `sgre`).
- `<subject>`: imperative present tense ("change" not "changed"), lowercase first letter, no trailing period
- **No line may exceed 100 characters**

**Examples:**

```
fix(terrain): stop far tiles rendering flat by re-clamping the elevation level
feat(layers): add DirAssetPackage for folder-based styles
docs(terrain): record the demo-app loop and how to debug the renderer
```

### Body

ONLY add a body when the header alone isn't enough for a reviewer:

1. Use the imperative present tense, same as the subject
2. Explain WHAT changed only if the commit touches more than 3 files
3. Explain WHY — the motivation, contrasted with previous behavior. For renderer work this is the load-bearing part: which camera/zoom reproduced it, and which mechanism (depth bias, tile zoom, label placement) was actually wrong.
4. Note the paired submodule commit when there is one (`Requires libs-massif <sha> (…)`)
5. Keep every line under 100 characters

### Footer

- Reference the issue this commit closes: `Fixes #<issue>` / `Closes #<issue>`
- Breaking changes start with `BREAKING CHANGE:` followed by a description and migration path. The public API surface is `all/modules/*.i` — a signature change there breaks every app binding, so it is a breaking change even when the C++ compiles.

### Revert

A commit that reverts another begins with `revert: ` followed by the reverted header. The body states `This reverts commit <hash>.` Renderer experiments get reverted often — when reverting a submodule commit, revert the pointer bump in the main repo too, or `master` still points at the reverted work.

## Co-Authored-By

Only add a `Co-Authored-By` trailer when Claude actually wrote the code being committed. If the user wrote the changes themselves (and Claude is just committing), do not add it.
