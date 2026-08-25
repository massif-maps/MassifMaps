---
name: test
description: Write or extend the host test suite. Use when shipping any change to all/native, libs-massif or the facade API — tests ship in the SAME commit as the work, never as a follow-up.
---

`tests/` is a host-native ctest suite over everything that links **without the renderer**. It runs
in under a second, and it is the only gate in this repo that checks behaviour rather than syntax.

```sh
cd tests && ./run.sh
```

**New work ships its own tests.** Not optional, not a follow-up commit. What the suite has caught so
far, none of it by review: a colour round-trip sign-extending through `getARGB()`, event dispatch
ordering by slot index instead of registration order, a payload not released when its subscription
died between the emit and the drain, a spec key silently dropped one level down.

## Rules

- **Cover the failure modes, not the happy path.** A malformed spec must leave the value untouched;
  a stale handle must be rejected; an unknown name must be an error rather than a silent
  pass-through; a removed property must be *refused*, not ignored. Those are the checks that fail
  when someone refactors.
- **Assert on something that could be wrong.** A test whose data passes either way is worse than
  none — a MultiPoint test with evenly spaced points passes whether or not the index is used.
- **Keep the link small.** The property table takes the address of every accessor thunk, so a full
  table would need the full SDK. `tests/api/CMakeLists.txt` generates a **reduced** one from an
  explicit module list (`gen-api-tables.py --modules`) and lists only the sources those classes
  need. A class that drags in `Options`, the renderer, sqlite or the elevation manager does not go
  in the list — that is the signal to verify on a device instead, and to say so.
- **State what the tests do not cover** in the file's header comment. They are not a render check
  and not a device check. If a function could not be reached without a heavy link, name it and name
  why.

## Adding a file

1. Write `tests/api/<Name>Test.cpp`. Include `TestCheck.h` after `using namespace massif;` and
   `using namespace massif::api;`, and assert with `TEST_CHECK(condition, "what it proves")` — the
   message is read as the suite's output, so write it as a claim, not a label.
2. Declare each entry point beside the others near the top of `tests/api/ApiTest.cpp` and call it
   from `main()`.
3. Add the file to `add_executable(api_tests …)` in `tests/api/CMakeLists.txt`, plus any `.i` module
   and `.cpp` source the new classes need — with a comment saying what forced each one in.
4. `cd tests && ./run.sh`, then read the individual `ok` lines for the new checks. A test that never
   ran is not a passing test.
