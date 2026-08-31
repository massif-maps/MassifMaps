/* The palette of the converted style: every colour, font and shared size it uses.

   To make a variant (dark, eink, high-contrast), COPY this file, edit the values, and
   list the copy in place of this one in a project.json of its own:

     { "extends": "./project.json", "styles": ["dark.mss", "style.mss"] }

   The palette has to come FIRST: the compiler keeps the first declaration of a variable
   it reads. style.mss is generated and is not meant to be edited. */
/* Source style: Streets */

@font_roboto_bold: 'Roboto Bold';
@font_roboto_italic: 'Roboto Italic';
@font_roboto_medium: 'Roboto Medium';
@font_roboto_regular: 'Roboto Regular';
@airport_zone_fill: hsl(0,0%,93%);
@aqueduct_outline_stroke: hsl(0,0%,51%);
@background: hsl(42,49%,93%);
@background_2: hsl(47,79%,94%);
@bridge_outline_stroke: hsl(43, 50%, 93%);
@building_3d_fill: hsl(44,14%,79%);
@building_fill: hsl(30,6%,73%);
@building_stroke: hsla(35, 6%, 79%, 0.3);
@building_stroke_2: hsl(35, 6%, 79%);
@cablecar_dash_stroke: hsl(0,0%,64%);
@cemetery_fill: hsl(0,0%,88%);
@continent_labels_fill: hsl(0,0%,19%);
@country_border_stroke: hsl(0, 0%, 54%);
@crop_fill: hsl(50,67%,86%);
@culture_fill: hsl(315, 35%, 50%);
@education_fill: hsl(175, 50%, 40%);
@ferry_halo_fill: hsla(0, 0%, 100%, 0.15);
@ferry_line_stroke: hsl(205,61%,63%);
@ferry_line_stroke_2: hsl(205,67%,47%);
@food_fill: hsl(18, 24%, 44%);
@forest_fill: hsl(119,38%,76%);
@grass_fill: hsl(103, 40%, 85%);
@healthcare_fill: hsl(6, 96%, 35%);
@highway_junction_fill: hsl(0,0%,21%);
@highway_shield_halo_fill: rgba(255, 255, 255, 1);
@highway_shield_interstate_top_us_fill: hsl(21, 100%, 45%);
@hospital_fill: hsl(12,63%,94%);
@housenumber_fill: hsl(26,10%,44%);
@housenumber_halo_fill: hsl(21,64%,96%);
@industrial_fill: hsl(60, 31%, 87%);
@industrial_fill_2: hsl(40,67%,90%);
@industrial_fill_3: hsla(32, 47%, 87%, 0.2);
@industrial_fill_4: hsl(49,54%,90%);
@industrial_fill_5: hsla(32, 47%, 87%, 0.5);
@labels_fill: hsl(0,0%,20%);
@labels_fill_2: hsl(205,84%,39%);
@labels_icon_halo_fill: hsl(0, 0%, 29%);
@lake_labels_halo_fill: hsla(0, 100%, 100%, 0.45);
@line_stroke: hsl(35,100%,76%);
@line_stroke_2: hsl(0,0%,73%);
@line_stroke_3: hsl(0,0%,63%);
@line_stroke_4: hsl(0,0%,70%);
@major_rail_stroke: hsl(0,0%,72%);
@meadow_fill: hsl(75,51%,85%);
@ocean_labels_fill: hsl(203,54%,54%);
@ocean_labels_fill_2: hsl(203,72%,39%);
@ocean_labels_halo_fill: hsla(196, 72%, 80%, 0.05);
@ocean_labels_halo_fill_2: hsla(200, 100%, 88%, 0.75);
@oneway_color: hsl(0, 0%, 65%);
@outline_stroke: hsl(28,72%,69%);
@outline_stroke_2: hsl(36,5%,80%);
@park_fill: hsl(82, 83%, 25%);
@path_stroke: hsl(0, 0%, 79%);
@pedestrian_fill: hsl(43,100%,99%);
@place_labels_fill: hsl(0,0%,25%);
@polygon_fill: hsl(204,92%,75%);
@public_fill: hsl(51, 10%, 40%);
@residential_fill: hsl(44,34%,87%);
@residential_fill_2: hsl(54, 45%, 91%);
@river_labels_halo_fill: hsl(202, 76%, 82%);
@river_stroke: hsl(210,73%,78%);
@road_labels_fill: hsl(0, 0%, 16%);
@road_stroke: hsl(48,100%,83%);
@sand_fill: hsl(52,93%,89%);
@school_fill: hsl(194,52%,94%);
@scrub_fill: hsl(97,51%,80%);
@shield_fill: hsl(215, 83%, 53%);
@shield_halo_fill: hsl(0,0%,100%);
@shopping_fill: hsl(18, 17%, 30%);
@sport_fill: hsl(129, 65%, 30%);
@stadium_fill: hsl(94, 100%, 88%);
@state_labels_fill: hsl(48,4%,44%);
@state_labels_halo_fill: hsla(0,0%,100%,0.75);
@text_fill: hsl(0,0%,40%);
@tourism_fill: hsl(283, 55%, 35%);
@town_labels_fill: hsl(0,0%,0%);
@town_labels_icon_fill: hsl(0, 20%, 99%);
@transport_fill: hsl(215, 81%, 35%);
@tunnel_stroke: hsl(48,100%,88%);
@tunnel_stroke_2: hsl(0,0%,96%);
@water_intermittent_fill: hsl(205,91%,83%);
@wood_fill: hsl(87,46%,85%);
@highway_size: 9;
@highway_spacing: 200;
@highway_wrap_width: 90;
@labels_halo_radius: 0.8;
@line_stroke_opacity: 0.5;
@line_stroke_opacity_2: 0.4;
@polygon_fill_opacity: 0.85;
@river_labels_spacing: 400;
@shield_halo_radius: 2;
@text_halo_radius: 1;
@text_minimum_distance: 4;
@text_spacing: 250;
