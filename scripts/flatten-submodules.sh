#!/usr/bin/env bash
#
# Flatten libs-external / libs-massif from submodules into the main repo, keeping
# every third-party fork a submodule at its current path (like integrations/nativescript).
#
# History is grafted with `git merge -s ours` + `git read-tree --prefix` (what `git subtree add`
# does; Apple Git ships no git-subtree). No existing commit is rewritten, so no force-push.
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

    if [ -n "$(git status --porcelain)" ]; then
        die "main working tree is dirty. Commit or stash first — this script deletes and recreates whole directories."
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

# Graft the submodule's history under its own path. Two parents, no rewrite.
graft_history() {
    local path="$1" url="$2" sha="$3"
    local remote="flatten-$path"

    say "graft history: $path @ $sha"
    run git remote add --no-tags "$remote" "$url"
    run git fetch --quiet "$remote"
    run git merge -s ours --no-commit --allow-unrelated-histories "$sha"
    run git read-tree --prefix="$path/" -u "$sha"
    run git commit -m "chore!: flatten $path into the repo

$url is a single-consumer fork of an archived CartoDB repo: no releases, no CI,
no other consumer, and no upstream left to merge from. Keeping it a submodule cost
a second branch, a second PR and a pointer bump per change, and the pointer was not
even honoured — CI fetches with --remote.

History is grafted, not rewritten: no SHA in this repo changes.

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

    drop_submodule "$path"
    graft_history "$path" "$url" "$sha"
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

if [ "$PHASES" = external ] || [ "$PHASES" = all ]; then
    flatten libs-external https://github.com/massif-maps/massif-external-libs
fi
if [ "$PHASES" = massif ] || [ "$PHASES" = all ]; then
    flatten libs-massif https://github.com/massif-maps/massif-maps-libs
fi

say "done — follow-up, NOT done by this script"
cat <<'EOF'
   Prose and CI still describe the old layout. Update in the same PR:

     BUILDING.md:22-35            drop the `cd libs-external` step, keep the mlt sparse-checkout
     .github/workflows/build.yml  --remote no longer moves the flattened trees (242, 469)
     CLAUDE.md                    repository layout table + "Submodule gotcha" paragraph
     .claude/CLAUDE.md            "Repos — one fork, two submodules, three PR targets"
     docs/maintenance/index.md    already lists flatten-submodules.md

   Then, for every other checkout:

     git submodule deinit -f -- libs-external libs-massif
     git submodule update --init --recursive

   Open branches in the flattened repos are NOT migrated. Merge or re-apply them by hand.
EOF
