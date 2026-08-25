---
title: PMTiles data source
description: "How PMTilesTileDataSource navigates a v3 archive: Hilbert ids, directory lookup, caching, and what is still O(n)."
sidebar_position: 3
---

# PMTiles data source

Scope: the internals of `PMTilesTileDataSource` — how a `MapTile` becomes bytes out of a single
[PMTiles v3](https://github.com/protomaps/PMTiles/blob/main/spec/v3/spec.md) file. For how an app
uses it, see [the feature page](/docs/features/pmtiles).

Code: `all/native/datasources/PMTilesTileDataSource.{h,cpp}` and
`all/native/datasources/components/PMTilesUtils.{h,cpp}`. Compiled only with
`_MASSIF_OFFLINE_SUPPORT`.

## What an archive looks like

| Region | Size | Read when |
|---|---|---|
| Header | 127 bytes | construction |
| Root directory | ≤ 16 KB compressed | construction, kept in memory for the object's life |
| Metadata (JSON) | small | first `getContainerMetaData()`, then cached |
| Leaf directories | optional, many | on demand, cached forever |
| Tile data | the rest | per tile |

Directories are lists of `DirectoryEntry {tileId, offset, length, runLength}`. `runLength == 0`
marks a **pointer to a leaf directory**; anything else is a run of `runLength` consecutive tile ids
sharing one blob.

## Looking up one tile

```
MapTile(z,x,y)
  └─ zxyToTileId  ── Hilbert curve, not Z-order: better spatial locality,
                     so neighbouring tiles land near each other on disk
  └─ FindTileEntry
       ├─ scan the root directory
       │    ├─ tile entry whose run contains the id  → hit
       │    └─ leaf pointer with tileId <= id        → load (or reuse) that leaf, scan it
       └─ miss  → z > minZoom: TileData{replaceWithParent}   (the layer overzooms the parent)
                  z == minZoom: null
  └─ read entry.length bytes at tileDataOffset + entry.offset
  └─ decompressData(header.tileCompression)
```

Overzoom is short-circuited before any I/O: past `getMaxZoomWithOverzoom()` the source returns an
empty `TileData` flagged `isOverZoom`.

## Compression

`pmtiles::decompressData` handles all four v3 modes — `0x01` none, `0x02` gzip (zlib streaming,
`windowBits = 15 + 16`), `0x03` brotli, `0x04` zstd — for **both** the internal directories
(`header.internalCompression`) and the tile payloads (`header.tileCompression`). They are
independent; an archive commonly gzips directories and leaves already-compressed PNG tiles raw.

## Concurrency and caching

One `std::recursive_mutex` guards every public entry point, because the object owns a single
`std::ifstream` and a seek/read pair is not atomic. Tiles are therefore fetched **serialised** even
though `TileLayer` calls from a pool — the default pool size is 1, so this is not currently the
bottleneck.

Cached for the object's lifetime, none of it bounded:

- the root directory (decoded once at construction),
- `_cachedMetadata` and `_cachedDataExtent` (first access),
- `_leafDirectoryCache`, keyed by leaf offset.

The source caches **no tile bytes**. Wrap it in `MemoryCacheTileDataSource` /
`PersistentCacheTileDataSource` when that matters.

## What could be better

Ordered by value, none of it measured yet on a real archive:

1. **Directory lookup is a linear scan, twice.** `FindTileEntry` walks the whole root vector, and
   `pmtiles::findTileEntry` walks the whole leaf vector. Entries are sorted by `tileId`, so both are
   a `std::lower_bound` away from O(log n). On a large archive the root directory alone is thousands
   of entries, scanned per tile request. *(An earlier version of this page claimed binary search was
   already used — it is not.)*
2. **Every leaf pointer with `tileId <= id` is loaded and scanned**, not just the one that can
   contain the id. Bounding the candidate by the *next* leaf pointer's `tileId` turns a
   worst-case multi-leaf read into a single one.
3. **`_leafDirectoryCache` never evicts.** Fine for a city-sized archive, unbounded for a planet one.
4. **`loadTile` logs at info level on every call** — noise in a normal session.
5. **HTTP archives** go through the generic HTTP source rather than PMTiles range requests, so the
   directory structure buys nothing remotely; only local files get the random-access win.

## Failure modes

Construction throws on a bad magic number, an unsupported version, or an unreadable header —
a broken archive fails loudly at `new`, not silently at the first tile. Per-tile failures
(short read, corrupt directory, decompression error) are caught, logged through `Log::` and turned
into a null `TileData`, so one bad tile does not take the map down.
