/*
 * The names Swift is supposed to see.
 *
 * NS_TYPED_ENUM turns each typed NSString group into a struct with static members, so this is
 * what an app writes instead of a string literal. Type-checking this file IS the test.
 */

func properties() -> [String] {
    let opacity: MassifProperty = .opacity
    let rangeStart: MassifProperty = .rangeStart
    let visible: MassifProperty = .visible
    return [opacity.rawValue, rangeStart.rawValue, visible.rawValue]
}

func names() -> [String] {
    let clicked: MassifEvent = .mapClicked
    let loadTile: MassifMethod = .loadTile
    let layer: MassifKind = .layer
    let raster: MassifSpecType = .layerRaster
    return [clicked.rawValue, loadTile.rawValue, layer.rawValue, raster.rawValue]
}
