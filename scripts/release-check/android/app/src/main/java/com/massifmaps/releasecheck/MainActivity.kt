package com.massifmaps.releasecheck

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.FrameLayout
import android.widget.TextView
import com.massifmaps.core.MapPos
import com.massifmaps.core.StringMap
import com.massifmaps.datasources.HTTPTileDataSource
import com.massifmaps.layers.RasterTileLayer
import com.massifmaps.ui.MapView
import com.massifmaps.valhalla.ValhallaRoutingService

/** The published AARs used as an app would: a map from the SDK, and the routing library beside it. */
class MainActivity : Activity() {
    private val lines = mutableListOf<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val mapView = MapView(this)
        val status = TextView(this).apply { setBackgroundColor(Color.argb(230, 255, 255, 255)); setPadding(16, 16, 16, 16) }
        setContentView(FrameLayout(this).apply {
            addView(mapView)
            addView(status, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT))
        })

        check("map SDK") {
            val tiles = HTTPTileDataSource(0, 19, "https://tile.openstreetmap.org/{zoom}/{x}/{y}.png")
            tiles.httpHeaders = StringMap().apply { set("User-Agent", "massif-release-check") }
            mapView.layers.add(RasterTileLayer(tiles))
            mapView.setFocusPos(mapView.options.baseProjection.fromWgs84(MapPos(6.8652, 45.8326)), 0f)
            mapView.setZoom(11f, 0f)
        }
        check("routing library") {
            ValhallaRoutingService().use { "profile ${it.profile}" }
        }
        status.text = lines.joinToString("\n")
    }

    private fun check(name: String, block: () -> Any?) {
        val line = try {
            "ok    $name ${block() as? String ?: ""}"
        } catch (e: Throwable) {
            "FAIL  $name: $e"
        }
        Log.i("massif-release-check", line)
        lines += line
    }
}
