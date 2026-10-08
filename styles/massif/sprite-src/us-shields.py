"""Writes the US shield SVGs beside it, one width per ref length, from MUTCD M1-1 (interstate) and
M1-4 (US route), redrawn to Mapbox Standard's proportions, and a `-mono` twin of each for the e-ink
variant: black and white, the interstate told by its black header band. Run from styles/massif
after editing.

    python3 sprite-src/us-shields.py
"""
import os
OUT = os.path.dirname(os.path.abspath(__file__))
PEAK, VALLEY = 0.9, 2.3


def crown(xs):
    """MUTCD's crown: small rounded peaks at xs, joined by valleys that run flat when there is room"""
    out = []
    for a, b in zip(xs, xs[1:]):
        out.append(f"Q{a + .9:.2f} {VALLEY} {a + 1.8:.2f} {VALLEY}L{b - 1.8:.2f} {VALLEY}Q{b - .9:.2f} {VALLEY} {b:.2f} {PEAK}"
                   if b - a > 3.6 else f"Q{(a + b) / 2:.2f} {VALLEY * 2 - PEAK} {b:.2f} {PEAK}")
    return ''.join(out)


def interstate(w, mono=False):
    m, flat = w / 2, 10 if w <= 20 else 9.5
    xs = [2.6, m, w - 2.6]
    body = (f"M1 3.8Q1.1 1.6 {xs[0]} {PEAK}{crown(xs)}Q{w - 1.1:.2f} 1.6 {w - 1} 3.8L{w - 1} {flat}"
            f"C{w - 1} {flat + 4.5:.2f} {m + (4 if w <= 20 else 5):.2f} 19.6 {m} 19.6"
            f"C{m - (4 if w <= 20 else 5):.2f} 19.6 1 {flat + 4.5:.2f} 1 {flat}Z")
    body_fill, band, line, ring, ring_width = ('#ffffff', '#000000', '#ffffff', '#000000', 1.3) if mono else (
        '#475bcb', '#ec3f42', '#ffffff', '#ffffff', 1.1)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="22" viewBox="0 0 {w} 22">
  <!-- MUTCD M1-1, redrawn: a federal design, public domain. One width per ref length. -->
  <defs><clipPath id="inside"><path d="{body}"/></clipPath></defs>
  <g clip-path="url(#inside)">
    <rect width="{w}" height="22" fill="{body_fill}"/>
    <rect width="{w}" height="5" fill="{band}"/>
    <rect y="5" width="{w}" height="1" fill="{line}"/>
  </g>
  <path d="{body}" fill="none" stroke="{ring}" stroke-width="{ring_width}" stroke-linejoin="round"/>
</svg>
'''


def highway(w, mono=False):
    m = w / 2
    xs = [3.4, m, w - 3.4]
    body = (f"M1 3.4Q1.4 1.6 {xs[0]} {PEAK}{crown(xs)}Q{w - 1.4:.2f} 1.6 {w - 1} 3.4"
            f"Q{w - .8:.2f} 4.8 {w - 1.9:.2f} 5.9Q{w - .9:.2f} 7.4 {w - .9:.2f} 10L{w - .9:.2f} 12.4"
            f"C{w - .9:.2f} 16.4 {m + 3.2:.2f} 18.4 {m} 19.3C{m - 3.2:.2f} 18.4 .9 16.4 .9 12.4L.9 10"
            f"Q.9 7.4 1.9 5.9Q.8 4.8 1 3.4Z")
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="20" viewBox="0 0 {w} 20">
  <!-- MUTCD M1-4, redrawn: a federal design, public domain. One width per ref length. -->
  <path d="{body}" fill="#ffffff" stroke="{'#000000' if mono else '#1b1d27'}" stroke-width="1.3" stroke-linejoin="round"/>
</svg>
'''


for n, w in (('2', 20), ('3', 26)):
    for suffix, mono in (('', False), ('-mono', True)):
        open(os.path.join(OUT, f'shield-us-interstate-{n}{suffix}.svg'), 'w').write(interstate(w, mono))
        open(os.path.join(OUT, f'shield-us-highway-{n}{suffix}.svg'), 'w').write(highway(w, mono))
