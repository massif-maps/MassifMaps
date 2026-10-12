/* SDK only, listed after style.mss: glTF models at the tree and wind turbine points
   (docs/features/3d-models.md). MapLibre has no model layer, so the reference styles draw none. */

#poi[zoom >= 15][class = 'tree']['param::models_3d' = 1]::model_tree {
  model-file: step(random(0, 3), (0, url('models/tree-spreading.glb')), (1, url('models/tree-layered.glb')), (2, url('models/tree-clumped.glb')));
  model-scale: random(0.8, 1.2);
  model-rotation: random(0, 360);
  model-color: #b4dcb4;
  model-color-mix: random(0, 0.3);
  model-opacity: linear([view::zoom], (15, 0), (16, 1));
}

/* One draw for the tower, one for the rotor turning on its hub: random() draws the same heading for
   both, seeded by the point. Turbines face the prevailing westerlies. */
#poi[zoom >= 13][class = 'wind_turbine']['param::models_3d' = 1] {
  ::model_turbine_tower {
    model-file: url('models/wind-turbine-tower.glb');
    model-rotation: random(250, 290);
    model-opacity: linear([view::zoom], (13, 0), (14, 1));
  }
  ::model_turbine_rotor {
    model-file: url('models/wind-turbine-rotor.glb');
    model-rotation: random(250, 290);
    model-z-offset: 81.4;
    model-spin: 14;
    model-spin-axis: y;
    model-opacity: linear([view::zoom], (13, 0), (14, 1));
  }
}
