#!/usr/bin/env bash
#
# Flatten libs-external / libs-massif from submodules into the main repo, keeping
# every third-party fork a submodule at its current path (like integrations/nativescript).
#
# History is re-prefixed with git-filter-repo on a THROWAWAY CLONE of the submodule, then merged
# in. filter-repo never touches this repo, so no SHA here changes and no force-push is needed.
# Re-prefixing is what keeps `git log -- libs-massif/vt/...` working: a plain subtree-style graft
# leaves the historical paths at the root and the log stops dead at the merge.
#
# Procedure and follow-up checklist: docs/maintenance/flatten-submodules.md
#
set -euo pipefail

DRY_RUN=1
PHASES="all"

# Vendored forks that are permanently dirty in the working tree (untracked build stubs,
# nested pointers we never touch). Contained inside libs-external today; once hoisted they
# would show up next to SDK edits in the main repo's status.
IGNORE_DIRTY=("libs-external/brotli/brotli" "libs-external/date/date" "libs-external/valhalla/valhalla")

usage() {
    cat <<'EOF'
usage: scripts/flatten-submodules.sh [--apply] [--phase external|massif|all]

  --apply            actually run (default is a dry run that only prints the commands)
  --phase <name>     which submodule to flatten (default: all)

Preconditions, all enforced:
  - main repo working tree clean, HEAD on a branch that is not master
  - each submodule being flattened is clean and fully pushed
  - its remote is reachable
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --apply) DRY_RUN=0 ;;
        --phase) [ $# -ge 2 ] || { echo "--phase needs a value" >&2; exit 2; }; PHASES="$2"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
note() { printf '   %s\n' "$*"; }
die()  { printf '\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

# Every mutating command goes through this, so --dry-run is honest.
run() {
    if [ "$DRY_RUN" = 1 ]; then
        printf '   \033[2m$ %s\033[0m\n' "$*"
    else
        printf '   \033[2m$ %s\033[0m\n' "$*"
        "$@"
    fi
}

# --------------------------------------------------------------------------------------
# preflight
# --------------------------------------------------------------------------------------

preflight_main() {
    say "preflight: main repo"

    command -v git-filter-repo >/dev/null || \
        die "git-filter-repo not on PATH. brew install git-filter-repo — without it the history import leaves git log dead on every imported path."

    # Tracked state only: the migration rewrites the index and the submodule directory, and never
    # touches an untracked file elsewhere in the tree. An untracked file *inside* the submodule
    # would be destroyed, but preflight_submodule catches that separately.
    if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
        die "main working tree has uncommitted tracked changes. Commit them first — this script deletes and recreates whole directories."
    fi

    local branch
    branch="$(git rev-parse --abbrev-ref HEAD)"
    if [ "$branch" = "HEAD" ]; then die "detached HEAD. Check out a branch."; fi
    if [ "$branch" = "master" ]; then die "on master. Branch first (chore/flatten-submodules)."; fi
    note "branch: $branch"
}

# A submodule about to be deleted must have nothing left only in this checkout.
preflight_submodule() {
    local path="$1" url="$2"
    say "preflight: $path"

    [ -d "$path/.git" ] || [ -f "$path/.git" ] || die "$path is not initialized. Run git submodule update --init --recursive."

    if [ -n "$(git -C "$path" status --porcelain --ignore-submodules=dirty)" ]; then
        die "$path has uncommitted changes. Commit and push them to $url first — this script removes the directory."
    fi

    local sha
    sha="$(git -C "$path" rev-parse HEAD)"
    git -C "$path" fetch --quiet origin || die "cannot reach $url"
    git -C "$path" merge-base --is-ancestor "$sha" origin/develop 2>/dev/null || \
        die "$path HEAD ($sha) is not on origin/develop. Push it first, or the history graft points at a commit nobody else has."
    note "pinning at $sha (on origin/develop)"
}

# --------------------------------------------------------------------------------------
# the graft itself
# --------------------------------------------------------------------------------------

# Remove the submodule registration, the gitdir and the working tree, and commit that.
# Must be its own commit: read-tree --prefix refuses a path that is already in the index.
drop_submodule() {
    local path="$1"

    say "drop submodule registration: $path"
    run git submodule deinit -f -- "$path"
    run git rm --cached "$path"
    run git config -f .gitmodules --remove-section "submodule.$path"
    run git add .gitmodules
    run rm -rf ".git/modules/$path" "$path"
    run git commit -m "chore: unregister $path as a submodule

Prepares the history graft that puts its content in-tree. No behaviour change:
the next commit restores the same files at the same path."
}

# Re-prefix the submodule's history in a throwaway clone, then merge it in.
#
# Measured on the real repo: with the paths re-prefixed, `git log -- libs-massif/vt/src/vt/
# GLTileRenderer.cpp` keeps 198 commits and blame still reaches mtehver. Without it — the plain
# `merge -s ours` + `read-tree --prefix` subtree recipe — the same command returns 1 commit (the
# graft) and `--follow` returns 0. All 677 commits and 8 tags survive either way; only the paths
# recorded in them differ.
#
# Split in two: the clone has to happen while the submodule directory still exists, and
# drop_submodule deletes it.
SCRATCH_SRC=""

prepare_history() {
    local path="$1" sha="$2"
    local scratch

    say "re-prefix history in a throwaway clone: $path @ $sha"
    scratch="$(mktemp -d)"
    SCRATCH_SRC="$scratch/src"
    note "scratch: $SCRATCH_SRC"

    # --no-local: a real fetch, never hardlinks into the source object store.
    # filter-repo needs the commit on a branch — a detached HEAD leaves it nothing to rewrite.
    run git clone --quiet --no-local "$ROOT/$path" "$SCRATCH_SRC"
    run git -C "$SCRATCH_SRC" checkout --quiet -B flatten-src "$sha"
    run git -C "$SCRATCH_SRC" filter-repo --force --to-subdirectory-filter "$path"
}

merge_history() {
    local path="$1" url="$2"
    local remote="flatten-$path"

    say "merge the re-prefixed history"
    run git remote add --no-tags "$remote" "$SCRATCH_SRC"
    run git fetch --quiet "$remote"
    # paths no longer collide with anything here, so this is an ordinary merge
    run git merge --no-commit --allow-unrelated-histories "$remote/flatten-src"
    run git commit -m "chore!: flatten $path into the repo

$url is a single-consumer fork of an archived CartoDB repo: no releases, no CI,
no other consumer, and no upstream left to merge from. Keeping it a submodule cost
a second branch, a second PR and a pointer bump per change.

Full history comes with it, re-prefixed so git log and blame still work on every
file. No SHA in THIS repo changes; the imported commits get new SHAs, so old
references to $path commits only resolve in $url, which stays online.

BREAKING CHANGE: $path is no longer a submodule. Existing checkouts need
git submodule deinit -f -- $path && git submodule update --init --recursive"
    run git remote remove "$remote"
}

# Hoist the nested .gitmodules entries into the root one, prefixing name and path.
# Keys are copied verbatim (url, branch, shallow, ...) so fork branches are preserved.
hoist_nested_gitmodules() {
    local path="$1"
    local nested="$path/.gitmodules"

    [ -f "$nested" ] || { note "no nested .gitmodules, nothing to hoist"; return; }

    say "hoist nested submodules into the root .gitmodules"

    local name key value ignored
    while read -r name; do
        while read -r key; do
            value="$(git config -f "$nested" "submodule.$name.$key")"
            if [ "$key" = "path" ]; then value="$path/$value"; fi
            run git config -f .gitmodules "submodule.$path/$name.$key" "$value"
        done < <(git config -f "$nested" --name-only --get-regexp "^submodule\.$(sed 's/[.[\*^$/]/\\&/g' <<<"$name")\." \
                 | sed "s/^submodule\..*\.//" | sort -u)
        note "  $name -> $path/$name"
    done < <(git config -f "$nested" --name-only --get-regexp '^submodule\..*\.path$' \
             | sed -e 's/^submodule\.//' -e 's/\.path$//')

    for ignored in "${IGNORE_DIRTY[@]}"; do
        case "$ignored" in
            "$path"/*) run git config -f .gitmodules "submodule.$ignored.ignore" dirty ;;
        esac
    done

    run git rm -q "$nested"
    run git add .gitmodules
    run git commit -m "chore: register $path's forks as submodules of this repo

Same repos, same fork branches, same paths — only the parent changed. The three
permanently-dirty vendored trees get ignore = dirty so they stop showing up next
to SDK edits in git status."
}

# --------------------------------------------------------------------------------------
# phases
# --------------------------------------------------------------------------------------

flatten() {
    local path="$1" url="$2"
    local sha boost_target=""

    preflight_submodule "$path" "$url"
    sha="$(git -C "$path" rev-parse HEAD)"

    # gitignored symlink, lives inside the directory we are about to delete
    if [ -L "$path/boost" ]; then
        boost_target="$(readlink "$path/boost")"
        note "will restore boost symlink -> $boost_target"
    fi

    prepare_history "$path" "$sha"      # must clone before the directory is deleted
    drop_submodule "$path"
    merge_history "$path" "$url"
    hoist_nested_gitmodules "$path"

    say "re-initialize"
    run git submodule update --init --recursive -- "$path"

    if [ -n "$boost_target" ]; then
        run ln -s "$boost_target" "$path/boost"
    fi

    # 142 MB of test fixtures otherwise; BUILDING.md documents the same command
    if [ -d "$path/mlt/mlt" ]; then
        run git -C "$path/mlt/mlt" sparse-checkout set cpp
    fi
}

case "$PHASES" in
    external|massif|all) ;;
    *) die "unknown phase: $PHASES" ;;
esac

preflight_main

# libs-massif first: the bigger win and the simpler graft (no nested submodules of its own)
if [ "$PHASES" = massif ] || [ "$PHASES" = all ]; then
    flatten libs-massif https://github.com/massif-maps/massif-maps-libs
fi
if [ "$PHASES" = external ] || [ "$PHASES" = all ]; then
    flatten libs-external https://github.com/massif-maps/massif-external-libs
fi

say "done — follow-up, NOT done by this script"
cat <<'EOF'
   Prose still describes the old layout. Update in the same PR:

     CLAUDE.md                    layout table, the repos table, "Library documentation"
     BUILDING.md                  only for the external phase — drop the `cd libs-external` step

   No CI edit is needed: build.yml fetches with `git submodule update --init --recursive`,
   which just initializes whatever is still a submodule, and it refers to the flattened
   trees by path (`cd libs-massif/cartocss/util`), which does not move.

   Then, for every other checkout:

     git submodule deinit -f -- libs-external libs-massif
     git submodule update --init --recursive

   Open branches in the flattened repos are NOT migrated. Merge or re-apply them by hand.
EOF
