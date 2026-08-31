# Style projects

Each folder here is zipped into `assets/styles/<name>.zip` by the `zipStyleProjects` gradle task
and read by `ZippedAssetPackage`. The **folder** is the source — diffable and editable; the zip is a
build product and is never committed.

`alpine` and `hybrid` are ours, written by hand.

## `mapbox-standard` and `maptiler-streets` are placeholders

Both were produced by the converter, and both carry the **upstream provider's sprite artwork**:

```sh
massif-style mapbox2css --live-light --label-emissive 0.6 --halo-emissive 0 \
    --sprite-key '?access_token=…' standard.json mapbox-standard
massif-style mapbox2css --live-light --label-emissive 0.45 --halo-emissive 0 \
    --sprite-key '?key=…' streets.json maptiler-streets
```

They are here so the `day-cycle-light` example can be run and the light curve judged against a real
map. **They are to be replaced by open-licensed equivalents**, for two reasons:

- the 595 icons in `mapbox-standard/icons` and `icons-glyph` are Mapbox's, and `maptiler-streets`
  carries MapTiler's — neither is ours to redistribute;
- they cost 2.9 MB of the APK (2.60 MB + 320 KB compressed), against 1.4 KB for the two hand-written
  projects beside them.

Nothing in the SDK depends on either: the example names them by folder, and a replacement keeping
the same two ids drops straight in.

Each also needs the tiles it was written against — `mapbox-standard` reads mapbox-streets-v8 source
layers and nothing else can feed it, `maptiler-streets` reads OpenMapTiles. See the example.
