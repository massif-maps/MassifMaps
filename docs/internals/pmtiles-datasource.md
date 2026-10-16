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
  └─ findTileEntry     binary search of the root (sorted by tile id), then of the ONE leaf the
       │                 pointer names - at most 3 leaf levels, as the spec allows
       ├─ run holding the id                   → hit
       └─ none  → z > minZoom: TileData{replaceWithParent}   (the layer overzooms the parent)
                  z == minZoom: null
  └─ pread entry.length bytes at tileDataOffset + entry.offset
  └─ decompressData(header.tileCompression)
```

Overzoom is short-circuited before any I/O: past `getMaxZoomWithOverzoom()` the source returns an
empty `TileData` flagged `isOverZoom`.

## Compression

`pmtiles::decompressData` handles all four v3 modes — `0x01` none, `0x02` gzip (zlib streaming,
`windowBits = 15 + 16`), `0x03` brotli, `0x04` zstd — for **both** the internal directories
(`header.internalCompression`) and the tile payloads (`header.tileCompression`). They are
independent; an archive commonly gzips directories and leaves already-compressed PNG tiles raw.

gzip inflates straight into one buffer sized from the stream's own trailer (the uncompressed size is
its last four bytes), growing only if that claim is wrong or wrapped; an uncompressed payload is
moved through, not copied.

## Concurrency and caching

Reads are `pread` on one descriptor (no shared file position), so the tile pool's threads read and
inflate in parallel; nothing holds a lock across I/O or decompression. Windows keeps a mutexed
`ifstream`. Only the two small caches below take a lock, and only for the lookup.

Cached for the object's lifetime: the root directory (decoded at construction), `_cachedMetadata`
and `_cachedDataExtent` (first access). Leaf directories are cached by offset in an LRU bounded at
`MAX_CACHED_LEAF_ENTRIES` (2^20 entries, ~25 MB; a Europe archive has ~4096 entries per leaf, so
~250 leaves), handed out as `shared_ptr<const>` so a scan outlives its eviction.

The source caches **no tile bytes**. Past its max zoom every overzoomed child tile loads the same
parent again; wrap the source in `MemoryCacheTileDataSource` when that matters.

:::note What the lookup used to cost
The first version scanned the root linearly and, for **every** leaf pointer at or before the id,
loaded, inflated and linearly scanned that leaf until it found the tile — all under one recursive
mutex. A z14 tile of a 6.3 M-tile archive sits half way through 1538 leaves: the first load decoded
~770 leaves (55 ms, ~75 MB of directories that never left the cache), and every later one still
walked all of them. Measured on `europe.pmtiles` (z14, dense city tiles, Apple M5 Pro, Release):
2.9 ms per warm tile and 468 tiles/s on four threads before, 0.14 ms and ~30 000 tiles/s after
(the terrain archive 0.31 -> 0.002 ms). Same bytes: 20 000 random tiles of three archives hash
identically before and after.
:::

## What could be better

1. **Two leaves can decode the same directory at once** on a cold start; the first insert wins. It
   costs one redundant inflate, not correctness.
2. **HTTP archives** go through the generic HTTP source rather than PMTiles range requests, so the
   directory structure buys nothing remotely; only local files get the random-access win.
3. `loadTile` still logs one info line per tile ([performance-log](performance-log.md): ~0.6 us
   through `os_log`, not a cost worth removing).

## Failure modes

Construction throws on a bad magic number, an unsupported version, or an unreadable header —
a broken archive fails loudly at `new`, not silently at the first tile. Per-tile failures
(short read, corrupt directory, decompression error) are caught, logged through `Log::` and turned
into a null `TileData`, so one bad tile does not take the map down.
