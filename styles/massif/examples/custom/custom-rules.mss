/* Listed AFTER style.mss: new rules, drawn over the base ones. */
#transportation['param::highlight_cycleways' = 1][zoom >= 13][class = 'path'][subclass = 'cycleway']::custom_cycleway {
  line-color: #d02f8c;
  line-width: linear([view::zoom], (13, 1.5), (18, 4));
  line-cap: round;
}
