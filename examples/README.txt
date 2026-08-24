Nontraditional i3lock-svg-nh scene-v1 collection
=================================================

Every file is a self-contained SVG scene. These are deliberately not variants of a central padlock.

Concepts
--------

taiko-pulse.scene.svg
  Locked: poised drumsticks over a taiko.
  Typing: each key becomes a drum strike and expanding skin vibration.
  Backspace: a reverse brush-like sweep across the drum.
  Verify: rapid rotating drum-roll pattern.
  Failure: cracked drum skin and red shockwave.

firefly-terrarium.scene.svg
  Locked: quiet moonlit terrarium.
  Typing: fireflies form an illuminated path.
  Key: firefly swarm rises.
  Backspace: swarm is recalled backward.
  Verify: spores orbit a central plant.
  Failure: fungal contamination blooms.

orbital-cartographer.scene.svg
  Locked: stable astronomical plotting grid.
  Typing: navigation beacons illuminate.
  Key: a star ignites.
  Backspace: a comet travels backward.
  Verify: satellite continuously orbits.
  Failure: orbital tracks fracture and collapse.

cellular-division.scene.svg
  Locked: a quiescent cell.
  Typing: membrane and nucleus become active.
  Key: mitosis produces two daughter cells.
  Backspace: a phagocyte sweeps across the field.
  Verify: DNA assembly rotates.
  Failure: contamination blooms around the cell.

pressure-line.scene.svg
  Locked: closed industrial valve and stable pressure line.
  Typing: pipeline becomes energized.
  Key: pressure pulse travels across the screen.
  Backspace: steam vents upward.
  Verify: turbine spins continuously.
  Failure: gauge overpressure and alarm sweep.

neural-canopy.scene.svg
  Locked: dim neural graph.
  Typing: nodes wake and illuminate.
  Key: synaptic wave travels through the network.
  Backspace: reverse pruning wave.
  Verify: inference loop circulates around the neural core.
  Failure: nodes disconnect and collapse.

cryptid-tracker.scene.svg
  Locked: passive moonlit field station and radar map.
  Typing: track markers appear.
  Key: a footprint is stamped into the field notes.
  Backspace: the map is wiped backward.
  Verify: radar sweep searches continuously.
  Failure: claw marks slash through the display.

deep-sea-sonar.scene.svg
  Locked: quiet sonar display.
  Typing: acoustic contacts illuminate.
  Key: expanding sonar ping.
  Backspace: bubbles retreat upward and left.
  Verify: active sonar sweep rotates.
  Failure: hull crack and red alarm ring.

clockwork-orrery.scene.svg
  Locked: aligned clockwork orrery.
  Typing: celestial nodes engage.
  Key: gear-mesh spark.
  Backspace: a gear visibly rewinds.
  Verify: planets orbit continuously.
  Failure: mechanism jams and fractures.

sumi-ink-gate.scene.svg
  Locked: quiet incomplete enso and ink landscape.
  Typing: brush becomes poised and the circle activates.
  Key: a broad sumi brush stroke appears.
  Backspace: a pale water wash erases backward.
  Verify: enso rotates continuously.
  Failure: red seal ink splashes and fractures.

Commands
--------

Validate:
  DISPLAY= ./i3lock --validate-scene FILE.scene.svg

Preview:
  ./scene-preview --scene FILE.scene.svg --size 1920x1080 --window

Lock:
  ./i3lock -n --scene-svg FILE.scene.svg
