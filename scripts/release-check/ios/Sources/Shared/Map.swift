import MassifMaps

/// An OpenStreetMap raster over Mont Blanc: enough to prove the map SDK links, starts and draws.
func mapCheckView(_ checks: Checks) -> MSFMapView {
    let mapView = MSFMapView()!
    checks.run("map SDK") {
        let tiles = MSFHTTPTileDataSource(minZoom: 0, maxZoom: 19, baseURL: "https://tile.openstreetmap.org/{zoom}/{x}/{y}.png")!
        let headers = MSFStringMap()!
        headers.set("User-Agent", x: "massif-release-check")
        tiles.setHTTPHeaders(headers)
        mapView.getLayers().add(MSFRasterTileLayer(dataSource: tiles))
        let focus = mapView.getOptions().getBaseProjection().fromWgs84(MSFMapPos(x: 6.8652, y: 45.8326))
        mapView.setFocus(focus, durationSeconds: 0)
        mapView.setZoom(11, durationSeconds: 0)
        return ""
    }
    return mapView
}
