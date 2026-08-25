---
name: add-example
description: Add or change a gallery example. Use when the user asks for a new example, a demo of a feature, or an example fixed/updated — an example is THREE files (Android, iOS, NativeScript) plus two generators, and one platform alone is a broken change.
---

An example is **one id on three platforms**. Adding it to one is a half-change: the generators cross-check the set and the website builds from the same manifest.

Read [`references/example-anatomy.md`](references/example-anatomy.md) before writing the first file — it holds the file layout, the host API on each platform, the spec cheatsheet, and the traps that have already cost a round.

## Steps

1. **Pick the id and section.** Kebab-case, unique across every section; sections are in `examples/Sections.java`. The id names the screenshot and the website URL, so renaming later breaks both.

2. **Write the Android file** — `scripts/android-dev/app/src/main/java/com/massifmaps/MassifDemo/examples/<section>/<Name>Example.java`. It carries the `@ExampleInfo` annotation, which is the single source of truth for the title, description and section on all three platforms.

3. **Port it to iOS** — `scripts/ios-dev/MassifDemo/Examples/MSF<Name>Example.m`, same id from `+ (NSString *)exampleId`. No registration: the catalogue enumerates classes at runtime.

4. **Port it to NativeScript** — `integrations/nativescript/demo-snippets/svelte/examples/<Name>.svelte`, `<ExampleShell id="…">` carrying the same id.

5. **Regenerate both manifests.**
   ```sh
   cd scripts && python3 gen-examples.py
   cd integrations/nativescript && node ./scripts/examples/index.mjs
   ```
   `gen-examples.py` reports every id that is missing a platform or a screenshot — read its output, it is the completion check.

6. **Verify.** Every one of these, not a subset:
   ```sh
   cd scripts/android-dev && ./gradlew :app:assembleDebug -x lint
   cd integrations/nativescript && node ./scripts/examples/check.mjs
   cd website && npm run build 2>&1 | tail -30
   ```
   `check.mjs` typechecks every NativeScript example against the plugin typings. When it reports a property the SDK has but the typings do not, the typings are stale: `node ./scripts/api-typings/index.mjs`.

7. **Capture the screenshot**, or say plainly that it is missing.
   ```sh
   cd scripts && python3 capture-examples.py --example <id>
   ```
   The rules are in [`references/example-anatomy.md`](references/example-anatomy.md#screenshots) — the stored file IS the wide vignette, so a subject drifting to the top of the frame is cropped out of it.

## Completion criterion

`gen-examples.py` reports the example on **three** platforms, all three verification commands pass, and the screenshot is either captured or explicitly reported as missing. Anything less is stated as not done — never implied.
