# Tests

The SDK has no test framework and a full build takes an hour, so these are deliberately small:
a host-native binary over the parts that can be linked without the renderer, plus a plain
`TEST_CHECK` macro instead of a dependency.

```sh
cd tests && ./run.sh
```

Needs `cmake`, `python3` and a host compiler. Nothing Android or iOS.

## What is covered, and what is not

`api/` covers the facade's [property table, handle table and registry](../docs/internals/api-facade.md):
generated-table lookups, the base-class chain, handle generations and the stale-handle rule,
`set`/`get` per value type, and dotted path resolution.

It does **not** cover anything that needs a real map. Two things make that a hard boundary rather
than a choice:

- **`Options` drags the renderer.** Linking it standalone pulls `MapRenderer`, `ElevationManager`,
  the `Bitmap` codecs and more, so `Options -> FogOptions` traversal is checked on a device
  instead, through the demo's `apiSet` knob.
- **The property table takes the address of every accessor thunk**, so a table generated over the
  whole SDK needs the whole SDK to link. The tests generate a **reduced** table from an explicit
  list of `.i` files (`gen-api-tables.py --modules`), which keeps the link small — and exercises
  the generator on the way.

Spec factories (`create`) are in the same position: they reference the constructors of every source
type, so they are device-verified rather than covered here.

## Adding a case

`api/ApiTest.cpp` is one `main` with `TEST_CHECK(condition, "what it means")`. A failure prints the
line and the binary exits non-zero, which is all `ctest` needs. If a new case needs another class,
add its `.i` to `TEST_MODULES` in `api/CMakeLists.txt` and its `.cpp` to `TEST_SDK_SOURCES` — if
that turns into a long list, the class probably belongs behind a device check instead.
