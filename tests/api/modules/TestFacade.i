// Test-only facade declarations. Read by gen-api-tables.py for the REDUCED table only - this file
// is not in all/modules and reaches no build.
//
// The production aliases are declared on Options, TileLayer and VectorTileLayer, and none of those
// can be linked into the host suite (Options alone pulls the renderer). These stand in for them:
// one alias on a class in the reduced table, and one on a BASE so the chain walk is covered.

!alias(massif::FogOptions, blend, horizonBlend)
!alias(massif::Feature, props, properties)
