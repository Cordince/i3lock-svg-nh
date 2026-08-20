TRON Ares 2 style notes
========================

This gallery uses the local `tron_ares_examples` screenshots as visual reference,
but avoids reproducing their visible text. The lockscreen SVGs use abstract
geometric interpretations only.

Observed visual vocabulary:

- Dark blue-black backgrounds with a muted red floor or horizon bias.
- Bright red-orange interface geometry on top of very low-contrast glass panels.
- Long horizontal rails, segmented bars, and thin calibration lines.
- Shallow perspective: slanted planes, trapezoids, and skewed console surfaces.
- Dense technical fields made from tiny repeated rectangles, points, and ticks.
- Reticle language: concentric circles, diamonds, corner brackets, and target dots.
- Wireframe silhouettes and contour traces in the lower half of the screen.
- Sparse cyan accents used as a secondary measurement layer, never dominant.
- Most action lives in the side and lower zones, leaving the upper center quiet.
- Motion should feel like slow scanning, calibration, buffering, and signal lock.

Implementation rules for this set:

- Visible text is limited to the LOGIN lock affordance.
- LOGIN is rendered with IBM Plex Mono only.
- Verify, fail, backspace, and typing states are geometric signals, not words.
- SVG frames are generated from `generate_tron_ares2.py` so the gallery remains
  reproducible and tunable.
