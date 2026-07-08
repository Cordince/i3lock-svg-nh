#!/usr/bin/env python3
"""Structured catalog for the generated i3lock-svg-nh theme gallery."""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import asdict, dataclass


VARIANTS = ["base", "calm", "dramatic", "mono", "warm"]

DEFAULT_RELEASE_NAMES = [
    "QuantumLatch",
    "NeuralMesh",
    "PacketStorm",
    "CryptoSeal",
    "CompilerPass",
    "DatacenterNight",
    "Observability",
    "ZeroTrust",
    "MainframeGreen",
    "PCBBlackGold",
    "FirmwareBoot",
    "ContainerOrbits",
    "GitGraph",
    "HexDump",
    "OrbitalMechanics",
    "DeepSpaceArray",
    "EventHorizon",
    "SpectralLines",
    "MoleculeLock",
    "CrystalLattice",
    "TesseractGate",
    "FractalBloom",
    "GoldenRatio",
    "CelestialChart",
    "WeatherRadar",
    "ParticleChamber",
    "MagneticField",
    "ArtDecoCipher",
    "BauhausGrid",
    "BlueprintVault",
    "Monolith",
    "ExecutiveGlass",
    "BrutalistConsole",
    "LuxuryWatch",
    "CRTGlow",
    "DialupHandshake",
    "TapeArchive",
    "VectorCabinet",
    "TerminalDungeon",
    "SynthGrid",
    "TinyRobots",
    "MoonGarden",
    "PocketUniverse",
    "ClockworkOwls",
    "LibraryAtMidnight",
    "RainWindow",
    "FireflyCircuit",
    "MechanicalKoi",
    "RunicGate",
    "Astrolabe",
    "AlchemyCircle",
    "UkiyoWave",
    "MosaicVault",
    "TempleMechanism",
    "NeonLightCycle",
    "SpaceOperaConsole",
    "SpyBriefing",
    "MechaHangar",
    "BlackVault",
    "EclipseGate",
]

ORIGINAL_THEME_NAMES = {"ComputeField", "MinimalCore", "SignalTrace"}

PALETTES = {
    "technical": {
        "base": ["#070b10", "#2ed8f2", "#386df5", "#48e5b4", "#ff7a4d"],
        "calm": ["#080d12", "#9fc8d8", "#5d8aa8", "#b7ded4", "#d79d62"],
        "dramatic": ["#04070d", "#00f0ff", "#8a6cff", "#1fffb2", "#ff3f4d"],
        "mono": ["#050608", "#d5e1e8", "#8a98a2", "#f4f8fb", "#c96a5b"],
        "warm": ["#0d0907", "#ffd166", "#c9892d", "#ff9f1c", "#f25c54"],
    },
    "science": {
        "base": ["#070814", "#77d7ff", "#8478ff", "#d5f6ff", "#ff735f"],
        "calm": ["#080b14", "#b7d9e9", "#789ed0", "#d7edff", "#d99b6f"],
        "dramatic": ["#04040f", "#35f2ff", "#ba6cff", "#fff0a8", "#ff4f77"],
        "mono": ["#05060a", "#d8e4ef", "#8795a8", "#ffffff", "#c96868"],
        "warm": ["#100909", "#ffd97a", "#ff9c52", "#fff1c1", "#ff6347"],
    },
    "professional": {
        "base": ["#08090b", "#d6b66a", "#627487", "#f0ead9", "#df6b57"],
        "calm": ["#090a0c", "#c7d1d6", "#7f8f9c", "#f2f4f2", "#c78267"],
        "dramatic": ["#040405", "#ffd166", "#5ec5ff", "#ffffff", "#ff5c4d"],
        "mono": ["#050505", "#e7e7df", "#8a8a86", "#ffffff", "#c9645b"],
        "warm": ["#0e0905", "#f1b661", "#b47437", "#f7e2bd", "#d86a45"],
    },
    "retro": {
        "base": ["#05070d", "#4cf2ff", "#ff4fd8", "#78ff78", "#ff7040"],
        "calm": ["#07090d", "#89d7d7", "#c58ed9", "#a7e5a7", "#d18c63"],
        "dramatic": ["#02040a", "#00f5ff", "#ff2bd6", "#b4ff00", "#ff3c3c"],
        "mono": ["#050607", "#d0ffd0", "#79a879", "#eefcee", "#ff7878"],
        "warm": ["#0e0707", "#ffb347", "#ff6fb3", "#ffd166", "#ff5c39"],
    },
    "whimsical": {
        "base": ["#070912", "#ffe071", "#8ad7ff", "#c7a6ff", "#ff7c7c"],
        "calm": ["#080b12", "#f1df9d", "#a9ccdf", "#c7badf", "#d88a77"],
        "dramatic": ["#050613", "#fff066", "#44e4ff", "#ce7dff", "#ff4d6d"],
        "mono": ["#070707", "#f2f2e6", "#a8a89d", "#ffffff", "#d07c7c"],
        "warm": ["#100806", "#ffd37d", "#ffad66", "#fff1a8", "#ff7d57"],
    },
    "mythic": {
        "base": ["#0b0906", "#d5a64e", "#5ea0b8", "#f0dfb2", "#d85d46"],
        "calm": ["#0a0908", "#c9b07a", "#86a8b0", "#eadfc6", "#bd7863"],
        "dramatic": ["#050403", "#ffd15a", "#29d3ff", "#fff0bb", "#ff493d"],
        "mono": ["#070605", "#ded6c8", "#8b8477", "#fff9ef", "#bf6f5d"],
        "warm": ["#120805", "#ffc35c", "#d17838", "#f7dca2", "#e35d38"],
    },
    "cultural": {
        "base": ["#060812", "#ff4fd8", "#38e6ff", "#ffd166", "#ff7040"],
        "calm": ["#070912", "#c08fd7", "#8ac9da", "#e2c889", "#d28567"],
        "dramatic": ["#03040c", "#ff28d0", "#00e7ff", "#ffe45e", "#ff3b3b"],
        "mono": ["#050507", "#e6e6ea", "#8e90a0", "#ffffff", "#ca6c6c"],
        "warm": ["#100606", "#ffb13b", "#ff6f91", "#ffdb75", "#ff5c39"],
    },
    "nature": {
        "base": ["#050b0d", "#56f0b0", "#37a8ff", "#d7f7d2", "#ff8a3d"],
        "calm": ["#07100f", "#9ddfc1", "#83b9d8", "#d6e8cd", "#d6946b"],
        "dramatic": ["#03090b", "#32ff9a", "#00c2ff", "#f4ffb8", "#ff5a2e"],
        "mono": ["#050707", "#d9eadf", "#879b90", "#f7fff9", "#d17a61"],
        "warm": ["#110805", "#ffb36b", "#e67636", "#ffe1a6", "#ff6138"],
    },
    "dark": {
        "base": ["#030405", "#7d8cff", "#44d7ff", "#d7ddff", "#ff485e"],
        "calm": ["#050607", "#9aa2c8", "#5f7892", "#d7dcee", "#c77a70"],
        "dramatic": ["#010203", "#7b61ff", "#00d9ff", "#ffffff", "#ff244d"],
        "mono": ["#020202", "#e2e2e7", "#8b8b93", "#ffffff", "#d05a5a"],
        "warm": ["#100504", "#f1a64d", "#9c4f34", "#f5d49c", "#e1483e"],
    },
}

MOTION_BY_MOTIF = {
    "circuit": "traces energize in sequence around a guarded center",
    "network": "nodes activate by graph distance with packet hops",
    "terminal": "scan bands resolve into stable diagnostic blocks",
    "orbit": "orbital points align through transfer arcs",
    "quantum": "interference rings collapse into a central latch",
    "lattice": "facets crystallize from outer edges toward the core",
    "fractal": "spiral branches bloom from a proportional seed",
    "deco": "stepped rays assemble in a clockwise cadence",
    "blueprint": "drafting arcs and construction lines lock into alignment",
    "monolith": "edge lights move across a stark central slab",
    "crt": "scanlines and phosphor pixels resolve into focus",
    "retro_grid": "neon grid segments brighten toward the horizon",
    "whimsical": "small friendly mechanisms converge around the core",
    "clockwork": "gears tick forward one tooth per frame",
    "garden": "soft nodes drift inward like fireflies or moonlit leaves",
    "astrolabe": "instrument rings rotate into calibrated alignment",
    "rune": "abstract glyph ticks illuminate around a gate",
    "mosaic": "tiles light up as an ordered geometric pattern",
    "ink": "brush-like arcs sweep inward as abstract strokes settle",
    "spy": "redacted bars and map arcs converge on authorization",
    "mecha": "panel seams and hangar lights power on in rows",
    "vault": "bolts rotate and the vault ring tightens",
    "eclipse": "corona arcs expand behind a dark center",
    "prism": "light splits through angular dark-glass facets",
    "nature": "contours, currents, or weather bands converge calmly",
}


@dataclass(frozen=True)
class ThemeSpec:
    name: str
    slug: str
    category: str
    short_description: str
    visual_keywords: list[str]
    palette: dict[str, list[str]]
    motif_family: str
    motion_model: str
    verify_model: str
    fail_model: str
    backspace_model: str
    success_model: str
    release_priority: int
    generated_by_default: bool
    trademark_safety_notes: str
    aliases: list[str]
    variants: list[str]


def slugify(name: str) -> str:
    parts = re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?=[A-Z]|$)|\d+", name)
    return "-".join(part.lower() for part in parts)


def split_words(name: str) -> list[str]:
    return re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?=[A-Z]|$)|\d+", name)


def category_palette(category: str) -> dict[str, list[str]]:
    return PALETTES[category]


def build_spec(index: int, name: str, category: str, motif: str, description: str) -> ThemeSpec:
    generated = name in DEFAULT_RELEASE_NAMES
    priority = DEFAULT_RELEASE_NAMES.index(name) + 1 if generated else 1000 + index
    words = [w.lower() for w in split_words(name)]
    keywords = list(dict.fromkeys([category, motif, *words]))
    return ThemeSpec(
        name=name,
        slug=slugify(name),
        category=category,
        short_description=description,
        visual_keywords=keywords,
        palette=category_palette(category),
        motif_family=motif,
        motion_model=MOTION_BY_MOTIF[motif],
        verify_model="focused scanner sweep and converging authorization geometry",
        fail_model="restrained fracture, dimming, and warning accents without text",
        backspace_model="reverse sweep, fading segments, and controlled rewind motion",
        success_model="future-compatible outward completion pulse and settled final core",
        release_priority=priority,
        generated_by_default=generated,
        trademark_safety_notes=(
            "Generated from abstract procedural geometry only; no client marks, logos, "
            "protected characters, proprietary layouts, external fonts, or raster sources."
        ),
        aliases=[],
        variants=list(VARIANTS),
    )


THEME_ROWS = [
    ("ComputeField", "technical", "circuit", "Dark compute-core rings with grid traces and charging packets."),
    ("MinimalCore", "technical", "monolith", "Restrained thin-ring indicator for executive environments."),
    ("SignalTrace", "technical", "network", "Signal routing hub with packets and scanner energy."),
    ("KernelPanic", "technical", "terminal", "Abstract diagnostic fault geometry with controlled red shards."),
    ("QuantumLatch", "technical", "quantum", "Qubit orbit points and probability arcs lock into place."),
    ("NeuralMesh", "technical", "network", "Synaptic node graph with distance-based activation."),
    ("PacketStorm", "technical", "network", "Routing hops, packet dots, and converging trace paths."),
    ("CryptoSeal", "technical", "vault", "Cryptographic seal geometry with authorization scan rings."),
    ("CompilerPass", "technical", "network", "Parser branches and pipeline blocks resolving into a graph."),
    ("DatacenterNight", "technical", "terminal", "Dark aisle rack lights and quiet status LED cadence."),
    ("Observability", "technical", "circuit", "Metrics rings, trace spans, and stabilizing signal gauges."),
    ("ZeroTrust", "technical", "vault", "Segmented trust gates and inward authorization barriers."),
    ("MainframeGreen", "technical", "terminal", "Phosphor diagnostic blocks and scanline structure."),
    ("BlueScreenCalm", "technical", "terminal", "Calm diagnostic panel geometry in cool blues."),
    ("PCBBlackGold", "technical", "circuit", "Black circuit-board traces with gold pad highlights."),
    ("FirmwareBoot", "technical", "terminal", "Boot-stage ticks and vendor-neutral firmware panels."),
    ("ContainerOrbits", "technical", "orbit", "Orbiting block modules align like a scheduler."),
    ("GitGraph", "technical", "network", "Branch and merge graph with commit-node activation."),
    ("HexDump", "technical", "terminal", "Byte-block geometry and pseudo-hex decode rhythms."),
    ("AirGap", "technical", "vault", "Isolated nodes, shield arcs, and separated trust zones."),
    ("OrbitalMechanics", "science", "orbit", "Transfer orbits and synchronized celestial points."),
    ("DeepSpaceArray", "science", "orbit", "Abstract telescope-array signals converge on a dark core."),
    ("EventHorizon", "science", "eclipse", "Accretion ring, lensing arcs, and shadowed center."),
    ("SpectralLines", "science", "terminal", "Emission-line bands and calibrated spectral ticks."),
    ("MoleculeLock", "science", "network", "Molecular bond graph forming around the lock core."),
    ("CrystalLattice", "science", "lattice", "Faceted crystalline geometry with illuminating facets."),
    ("TesseractGate", "science", "lattice", "Projected dimensional cube lines fold into alignment."),
    ("FractalBloom", "science", "fractal", "Iterated spiral petals grow from a mathematical seed."),
    ("GoldenRatio", "science", "fractal", "Elegant proportion arcs and phi-like spiral movement."),
    ("CelestialChart", "science", "orbit", "Star-chart rings, declination geometry, and constellations."),
    ("Seismograph", "science", "nature", "Waveform traces stabilizing around a central pulse."),
    ("WeatherRadar", "science", "nature", "Radar sweep and abstract storm-cell contours."),
    ("GenomeHelix", "science", "network", "Abstract paired helix ticks with no visible letters."),
    ("ParticleChamber", "science", "orbit", "Bubble-chamber tracks and collision arcs."),
    ("MagneticField", "science", "nature", "Field lines polarize around a central core."),
    ("ArtDecoCipher", "professional", "deco", "Gold-on-black stepped rays and elegant cipher geometry."),
    ("BauhausGrid", "professional", "deco", "Strict circles, rectangles, and triangles in clean balance."),
    ("SwissTerminal", "professional", "terminal", "Clean grid panels and restrained terminal structure."),
    ("BlueprintVault", "professional", "blueprint", "Architectural lines and compass arcs around a vault."),
    ("Monolith", "professional", "monolith", "Stark slab geometry with precise edge light."),
    ("ExecutiveGlass", "professional", "monolith", "Frosted glass ring with quiet reflections."),
    ("MuseumLabel", "professional", "monolith", "Gallery-like spacing and quiet abstract cards."),
    ("BrutalistConsole", "professional", "terminal", "Heavy block geometry with sparse amber signals."),
    ("LuxuryWatch", "professional", "clockwork", "Mechanical dial ticks and jewel-like node details."),
    ("ArchitectDesk", "professional", "blueprint", "Drafting compass arcs and dark vellum construction lines."),
    ("CRTGlow", "retro", "crt", "Curved-screen glow, scanlines, and phosphor dots."),
    ("DialupHandshake", "retro", "terminal", "Waveform handshake bars and carrier-lock rhythm."),
    ("FloppyVault", "retro", "vault", "Abstract disk-sector geometry with safe vault motion."),
    ("TapeArchive", "retro", "clockwork", "Data-tape reels and analog signal cadence."),
    ("ArcadeBoot", "retro", "retro_grid", "Generic arcade boot grid and insert-energy feel."),
    ("VectorCabinet", "retro", "retro_grid", "Neon vector lines without specific game symbols."),
    ("TerminalDungeon", "retro", "terminal", "Abstract map boxes and paths without characters."),
    ("SynthGrid", "retro", "retro_grid", "Retro-futurist horizon grid and ring energy."),
    ("PixelNebula", "retro", "retro_grid", "Chunky pixel-like star field and nebula glow."),
    ("CassetteSignal", "retro", "clockwork", "Tape reels, equalizer bars, and analog signal arcs."),
    ("TinyRobots", "whimsical", "whimsical", "Small helper mechanisms repair and energize the ring."),
    ("MoonGarden", "whimsical", "garden", "Moonlit botanical arcs and soft firefly nodes."),
    ("PocketUniverse", "whimsical", "orbit", "Tiny planets inside a glassy cosmic core."),
    ("ClockworkOwls", "whimsical", "clockwork", "Mechanical owl-like symmetry built from gears and rings."),
    ("TeaAndLightning", "whimsical", "nature", "Steam curls and electric arcs around a calm center."),
    ("LibraryAtMidnight", "whimsical", "monolith", "Quiet archive cards and floating shelf geometry."),
    ("RainWindow", "whimsical", "nature", "Rain streaks, reflection arcs, and droplet motion."),
    ("FireflyCircuit", "whimsical", "garden", "Fireflies acting as circuit nodes in a dark field."),
    ("PaperLanterns", "whimsical", "garden", "Soft lantern orbs rising through night geometry."),
    ("MechanicalKoi", "whimsical", "clockwork", "Koi-like clockwork arcs in abstract motion."),
    ("CloudMachine", "whimsical", "clockwork", "Gentle mechanical weather with clouds and gear arcs."),
    ("StarshipCat", "whimsical", "orbit", "Abstract constellation cockpit with playful star nodes."),
    ("PotionLab", "whimsical", "garden", "Bubbles, glassware arcs, and safe laboratory magic."),
    ("ForestConsole", "whimsical", "nature", "Terminal panels softened by vine-like contour lines."),
    ("MiniatureCity", "whimsical", "terminal", "Tiny city-light windows under a glassy core."),
    ("RunicGate", "mythic", "rune", "Abstract rune-like gate ticks with no copied symbols."),
    ("Astrolabe", "mythic", "astrolabe", "Navigation instrument rings and calibrated brass geometry."),
    ("AlchemyCircle", "mythic", "rune", "Generic geometric circle and triangle diagram."),
    ("SamuraiMoon", "mythic", "ink", "Ink moon and blade-like abstract arcs with no marks."),
    ("UkiyoWave", "mythic", "nature", "Woodblock-adjacent wave geometry without copying a print."),
    ("DesertCaravan", "mythic", "nature", "Dunes, stars, and brass compass motion."),
    ("NordicFrost", "mythic", "lattice", "Frost crystals and generic knot-like linework."),
    ("CelticCircuit", "mythic", "circuit", "Interlaced circuit-knot pattern built from abstract traces."),
    ("MosaicVault", "mythic", "mosaic", "Geometric non-religious mosaic tiles and vault arcs."),
    ("PapyrusCipher", "mythic", "terminal", "Manuscript-like abstract glyph bands without text."),
    ("TempleMechanism", "mythic", "vault", "Stone-door puzzle rings and hidden mechanism motion."),
    ("MythicCompass", "mythic", "astrolabe", "Compass and constellation geometry in adventure mood."),
    ("LanternFestival", "mythic", "garden", "Floating lantern orbs and gentle night sky motion."),
    ("InkDragon", "mythic", "ink", "Abstract brush-stroke energy with no recognizable character."),
    ("OracleGlass", "mythic", "prism", "Crystal-glass interface and faint abstract symbols."),
    ("NeonLightCycle", "cultural", "retro_grid", "Abstract neon trails and grid loops."),
    ("SpaceOperaConsole", "cultural", "terminal", "Generic starship console panels and stellar arcs."),
    ("WizardArchive", "cultural", "rune", "Generic archive cards and sigils without crests."),
    ("SpyBriefing", "cultural", "spy", "Redacted bars, safe map arcs, and briefing geometry."),
    ("NoirDetective", "cultural", "nature", "Rainy blinds geometry and restrained noir lighting."),
    ("SuperheroSignal", "cultural", "eclipse", "Generic rooftop beacon ring with no emblem."),
    ("KaijuRadar", "cultural", "nature", "Radar pings and abstract large-scale silhouette energy."),
    ("MechaHangar", "cultural", "mecha", "Generic panel seams and hangar-light power-up."),
    ("TreasureMap", "cultural", "astrolabe", "Compass route geometry and abstract map islands."),
    ("CyberNinja", "cultural", "spy", "Stealth arcs and geometric blades without clan symbols."),
    ("HauntedTerminal", "cultural", "terminal", "Spectral scan flicker and ghosted terminal geometry."),
    ("PirateCompass", "cultural", "astrolabe", "Nautical compass and storm-line geometry."),
    ("DragonHoard", "cultural", "mosaic", "Abstract scale and gem geometry."),
    ("TimeTravelerDesk", "cultural", "clockwork", "Clock rings and temporal drafting arcs."),
    ("RetroHeroArcade", "cultural", "retro_grid", "Generic action-panel arcade geometry."),
    ("AuroraVault", "nature", "nature", "Northern-light bands around a vault-like center."),
    ("OceanDepths", "nature", "nature", "Sonar rings and bioluminescent particles."),
    ("VolcanicCore", "nature", "eclipse", "Obsidian center with lava-like fracture arcs."),
    ("GlacierSignal", "nature", "lattice", "Ice facets, polar grid, and cold refractions."),
    ("CanopyNetwork", "nature", "network", "Forest canopy graph with living signal paths."),
    ("StormCell", "nature", "nature", "Lightning branches and weather contour rings."),
    ("SolarFlare", "nature", "eclipse", "Corona and plasma arcs around a bright core."),
    ("DesertNight", "nature", "nature", "Dunes, stars, and amber night glow."),
    ("CoralCircuit", "nature", "circuit", "Coral branches expressed as circuit traces."),
    ("MountainBeacon", "nature", "nature", "Topographic rings and summit beacon pulse."),
    ("BlackVault", "dark", "vault", "Matte black vault door with rotating bolt geometry."),
    ("ObsidianKey", "dark", "lattice", "Obsidian shards and ruby edge glow."),
    ("EclipseGate", "dark", "eclipse", "Shadow ring and corona gate expansion."),
    ("RedlineProtocol", "dark", "spy", "High-security red scan lines without insignia."),
    ("GhostProtocol", "dark", "terminal", "Ghosted overlays and trace vanish/reveal motion."),
    ("NightOps", "dark", "spy", "Abstract tactical-map aesthetic without insignia."),
    ("ColdIron", "dark", "mecha", "Industrial steel, rivets, and amber edge light."),
    ("DeepVault", "dark", "vault", "Underground depth rings and heavy core geometry."),
    ("ShadowCircuit", "dark", "circuit", "Nearly black circuit traces that reveal on input."),
    ("PrismLock", "dark", "prism", "Dark glass prism and controlled refraction arcs."),
]


CATALOG = [build_spec(index, *row) for index, row in enumerate(THEME_ROWS, start=1)]


def all_themes() -> list[ThemeSpec]:
    return list(CATALOG)


def default_release_themes() -> list[ThemeSpec]:
    by_name = {theme.name: theme for theme in CATALOG}
    return [by_name[name] for name in DEFAULT_RELEASE_NAMES]


def themes_by_category(category: str) -> list[ThemeSpec]:
    category = category.lower()
    return [theme for theme in CATALOG if theme.category == category]


def get_theme(name_or_slug: str) -> ThemeSpec:
    needle = name_or_slug.lower()
    for theme in CATALOG:
        if theme.name.lower() == needle or theme.slug == needle:
            return theme
    raise KeyError(name_or_slug)


def catalog_as_dicts() -> list[dict]:
    return [asdict(theme) for theme in CATALOG]


def catalog_hash() -> str:
    payload = json.dumps(catalog_as_dicts(), sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def categories() -> list[str]:
    return sorted({theme.category for theme in CATALOG})


if __name__ == "__main__":
    print(json.dumps(catalog_as_dicts(), indent=2, sort_keys=True))
