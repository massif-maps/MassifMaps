Map { param-selected_peak: ''; }
@selected: [name] + '|' + [ele] = [param::selected_peak];
#mountain_peak['class'='peak'][zoom>=0] {
  text-name: [name];
  text-face-name: @selected ? 'Roboto-Bold, HelveticaNeue-Bold' : 'Roboto, Helvetica Neue';
  text-secondary-name: [ele]+'m';
  text-wrap-width: 70;
  text-secondary-scale: 0.62;
  text-secondary-fill: #6b7280;
  text-secondary-dx: 3;
  text-secondary-dy: 0;
  text-size: 13.0;
  text-fill: @selected ? #2f4f9e : #000000;
  text-halo-fill: #ffffff;
  text-halo-radius: 1.5;
  text-background-fill: #ffffff;
  text-background-opacity: 0.85;
  text-background-radius: 6;
  text-background-padding-x: 5;
  text-background-padding-y: 2;
  text-placement: callout;
  text-placement-priority: @selected ? 100000 : 0;
  text-min-distance: 1;
  text-rank: 1000 * ([ele] - @eye_elevation) / ([view::distance] + 1);
  text-orientation: 45;
  text-callout-line-anchor: bottom-left;
  text-callout-align: bottom-left;
  text-callout-screen-anchor: @label_band;
  text-callout-offset: 10;
  text-callout-step: 60;
  text-callout-max-rows: 1;
  text-callout-persist: 10;
  text-callout-line-width: 1;
}
