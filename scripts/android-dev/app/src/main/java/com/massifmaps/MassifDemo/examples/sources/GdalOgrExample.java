package com.massifmaps.MassifDemo.examples.sources;

import android.os.Environment;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

import java.io.File;

/**
 * A GeoTIFF read straight off disk by GDAL, with no tiling step in between.
 *
 * Only works on an SDK built with the `gdal` profile - GDAL is not vendored, so a stock AAR does
 * not contain the class and the spec below is an unknown type. The example says which of those it
 * hit, because "nothing happened" is the one outcome that tells you nothing.
 *
 * The OGR half (a shapefile through OGRVectorDataSource) is NOT here: its constructor needs a
 * Projection, and the Java facade has no way to read an object property as an object - TypeScript
 * has child(), Java has no equivalent. Add one and this example can grow the vector case.
 */
@ExampleInfo(
    id = "gdal-geotiff",
    title = "GeoTIFF (GDAL)",
    description = "Read a raster file directly with GDAL, no tiles in between. "
                + "Needs an SDK built with the gdal profile.",
    section = Sections.SOURCES,
    order = 60)
public class GdalOgrExample extends MapExample {

    /** Where this example looks for its data. Push your own file here with adb. */
    private static final String RASTER =
        new File(Environment.getExternalStorageDirectory(), "massif-gdal/raster.tif").getAbsolutePath();

    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();

        // A basemap underneath, so an empty screen reads as "no data" and not as "no map".
        map.addLayer("basemap", Spec.of("raster")
            .set("source", Spec.of("http")
                .set("url", "https://tile.openstreetmap.org/{z}/{x}/{y}.png")
                .set("maxZoom", 19)
                .set("HTTPHeaders", Spec.object().set("User-Agent", UA))));
        map.camera().moveTo(new Position(6.8652, 45.8326), 11);

        if (!new File(RASTER).exists()) {
            host.caption("No data: push a GeoTIFF to " + RASTER + " and reopen this example.");
            return;
        }

        try {
            // 'gdal' takes the file plus the zoom range it should serve; the source reprojects
            // into the map's tile grid on demand.
            map.addLayer("gdal-raster", Spec.of("raster")
                .set("source", Spec.of("gdal")
                    .set("path", RASTER)
                    .set("minZoom", 0)
                    .set("maxZoom", 18))
                .set("opacity", 0.8));
            host.toggle("GeoTIFF", true, on -> map.layer("gdal-raster").set("visible", on));
            host.caption("GeoTIFF loaded through GDAL. Toggle it against the basemap.");
        } catch (Exception e) {
            // An unknown spec type is what a build without the gdal profile looks like from here.
            host.caption("Could not build the GDAL source: " + e.getMessage()
                       + " - is this SDK built with the 'gdal' profile?");
        }
    }
}
