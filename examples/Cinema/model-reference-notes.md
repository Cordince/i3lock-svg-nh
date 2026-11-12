# Anatomy refinement from the finished film

These are original study models shaped against final-film footage and design
references, rather than extracted production assets or an exact reconstruction.

| Reference | Clip time | Shape used in the model |
| --- | --- | --- |
| [First Contact — Movieclips](https://www.youtube.com/watch?v=dQlExOgF5eU) | 100–132 s | Upright proportions and unequal, flexed outside limbs |
| [First Contact with Aliens — Paramount Movies](https://www.youtube.com/watch?v=12BIfTgmSm4) | 100 and 148 s | Seven limbs separated in depth, distinct elbows and longer middle limbs |
| [You Have the Weapon — Movieclips](https://www.youtube.com/watch?v=fOFGL7-qmqs) | 4 and 36 s | Thick neck, flexor masses and irregular skin folds |
| Same clip | 52 s | Curled digit silhouettes and tissue joining the proximal fingers |
| Same clip | 68 s | Rounded crown, broad neck and sloping shoulders |
| [The Heptapods Speak — Movieclips](https://www.youtube.com/watch?v=G6uZp-33qcY) | 90.5–97.5 s | Side-on folded appendage, recurved wrist and angular forearm silhouette |

## Foreground surface and optics

The active foreground uses three continuous signed-distance sculpts: folded,
writing and contact. The forearm, palm and seven curved fingers merge into one
skin surface. Shallow fold channels are carved into that surface. It has no
separate finger caps, intersecting joint discs or sheet-like web panels.

`tools/build_foreground_volume.py` bakes the original sculpts, using the authored
loft profiles from `tools/build_heptapod_models.py`. NumPy is needed only for
baking. The checked-in `.sdf` assets load directly in the C application.

`cinema_foreground.c` blends the three poses, twists the wrist while preserving
the upper attachment, and traces the continuous surface. Gradient normals,
broad wrapped lighting, surface grain and fine hide folds replace polygon-joint
shading. Four subpixel rays provide fractional silhouette coverage. The hand
opens and turns its mouth toward the glass to emit a turbulent ink plume,
folds back as the plume gathers into a logogram, spreads against the glass on failure, and
withdraws upward on acceptance without a white fade.

Background and foreground have independent color/depth layers. The compositor
filters the foreground's premultiplied coverage across its outline, then
integrates both surfaces into the existing mist in depth order. This preserves
the background through blurred edges and avoids a bright halo. The glass glyph
is drawn afterward and stays sharp.

The older hand OBJ remains a loft study. It is no longer the active foreground
model. The approved background torso, limb geometry and mist field are retained.
Mist lighting is cached in a view-aligned volume before depth integration;
the foreground uses a nine-tap optical filter and occupied-volume trace bounds.
The ink remains a guided 2D fluid rather than a full 3D fluid simulation.

## Checks and reference preparation

Native checks cover pose changes, perspective during the strike, withdrawal,
fractional foreground coverage, unchanged background pixels during a strike,
and the mist's lateral structure and motion. The model baker checks that the
surface stays inside every storage boundary, including fully spread fingers.
Render reviews use an isolated display and do not open desktop viewers.

`tools/prepare_cinema_reference.py` downloads the public clips and extracts the
selected frames into ignored `tmp/cinema-reference/anatomy`. Reference film
imagery remains copyright Paramount Pictures (2016).
