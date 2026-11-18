# AGENTS.md: Haze

Why the code is the way it is. `CLAUDE.md` is the command reference; the design is
`~/Projects/resolume/specs/SPEC-haze.md`, whose "As built" section lists where this repo
departs from it. Built 2026-11-16 from the spec, in one session, local only.

## The idea

**A beam is only visible because the haze scatters a little of it at you, and how much
depends on the angle between the beam and your eye.**

Each fixture is a Clay Paky Mythos (the one fixture whose manual gives a lens size, a flux,
a zoom range and full-range pan and tilt times): a lamp, a gobo and a lens on a pan/tilt
yoke. For every pixel the air pass integrates, along the eye's ray, σ(x)·p(θ)·E(x)·exp(−σ̄
(|x − F| + t)) for each beam: the local density times the Henyey–Greenstein phase function
times the illuminance (I₀·K/(d² + a²), with K the gobo blurred by the defocus disc
D|1/r − 1/f|) times Beer–Lambert along both legs. The final pass adds the lit back wall and
floor (albedo/π, the cosine, the same transmittance), the lenses where the eye ray meets one
(I(toward the eye)/πa²), exposure and the Over's composite.

The motors are a time-optimal plan per axis under |v| ≤ V, |a| ≤ A, solved in closed form in
double from wherever the yoke is (x₀, v₀); pan and tilt are synchronised by scaling the
faster axis's limits by (k, k²), k by bisection. The chase steps at the instant inside the
frame where the beat, an onset or a press fell, and the plan is redone from there. Audio
moves the mechanism (an onset is a step); nothing wobbles a parameter.

What falls out (the README lists it): beams faint from the side and blazing down the beam,
the clip sharp on the wall and smeared along the beam, chases late at high BPM, beams
thinning together where the haze does, a beam brightest at σ = 1/L, spots blowing out
before beams do.

## Decisions taken without asking (the brief says decide and write it down)

- **The Over's default Clip As is Screen, not the spec's Gobo.** All 21 of Resolume's
  bundled demo clips on this Mac (Shop74's 15, AV's six `001`/`004`) went through the Over
  for 150 frames at 640×360 via `--pipe`, onto a contact sheet. Under Gobo, a dark clip makes
  dark beams (the clip *is* the beams' light) and the clip itself is never seen as a
  picture: most of the sheet was black. Backdrop shows the clip only where beams land, and
  the default Fan lands on the floor. Screen survived all 21: beams readable, the clip on the
  screen behind them as dark as it is. Gobo and Backdrop stay one click away.
- **The camera's orbit radius is 13 m, not the spec's 18 m.** At 18 m the six-fixture rig
  filled about a third of the frame's width; at 13 m the rig and the screen fill it.
- **The chase is console-style; inverse kinematics only for Audience.** The spec had every
  pattern place a lean (a direction) and turn it into pan/tilt by IK, choosing among
  (pan + 360k, tilt) and (pan + 180 + 360k, −tilt) the one with the shortest synced move.
  That choice flips between the two solutions from step to step and makes nonsense at high
  BPM (see the traps). Lean patterns now give pan and tilt directly, the way a lighting
  console's effects engine does: pan = atan2(across, out), unwrapped to the turn nearest the
  last target and kept inside ±270°; tilt = min(hypot(across, out), 122°). Audience's odd
  steps still aim at the camera by IK (a direction is the point there), nearest solution.
- **Quality's default is Medium.** High and Medium are hard to tell apart at 1080p (the air
  is smooth; surfaces and lenses are full raster at every Quality); Low is visibly blocky at
  the beams' edges. Medium costs 0.34 ms at 1080p.
- **The own clock follows the host's last BPM.** When the host's bar phase never moves the
  plugin runs its own clock, at the last BPM the host sent (120 if it never sent one). The
  spec said 120 flat; this is ferrofluid's behaviour and keeps a stopped transport at the
  set tempo.
- **A fresh instance starts with every beam already at the first step's place**, not
  swinging in from home.
- **The Over's Gobo acts only in Backdrop and Screen.** In Clip As Gobo the clip is the gobo.
- **Audience whites the frame out at the default exposure**, six lenses staring at the
  camera. Kept as specified: that's what it looks like. One beam on the camera at −2 stops is
  a gobo star.
- **Stir is s = the control (0..1), not more.** A Stir of 2 was tried for visibility and
  reverted: n's range is [−1, 1], so σ̄(1 + s·n) stays ≥ 0 and keeps the room's mean at σ̄
  only for s ≤ 1, and `--density` measures both.
- **Provisional hand copies, until registration:** `source/StoatworksAbout*.h` (adapted from
  xenon's, `guide` empty) and `ATTRIBUTIONS.md`, as graticule did. Registered on 2026-11-18:
  `sync-about.py` and `sync-attributions.py` generate both now, and the guide button took the
  About block to 23 and 25 parameters.
- **The workflows were files only** until the repo went public on 2026-11-18: `ci.yml` and
  `release.yml` had been sed-adapted from ferrofluid and never run. Before the first push
  they took xenon's current shape (macos-15, which GitHub still runs; the DLLs loaded by
  `tools/wincheck.ps1`; ATTRIBUTIONS.md in each zip; the installer's `--ffgl` folder) and
  isohypse's two-plugin parts (vcpkg's applocal copy off, which two plugins once raced).

## The traps actually hit

- **The phase-plane integrator never terminated.** The harness's independent check on the
  servo integrates the motion in small steps with a bang-bang rule (brake when the stopping
  distance reaches what's left). After a rounding overshoot the sign of what was left
  flipped, it accelerated back, overshot again and chattered forever. Braking is now
  latched: once it starts braking it brakes to rest, and only then re-decides.
- **IK with a target-relative shortest move made nonsense.** At 120 BPM the "shortest synced
  move" picked the over-the-top solution on one step and the direct one on the next, so
  many steps were 0.03 s moves between two solutions of nearly the same direction. The chase
  "kept up" at any tempo because it never really moved. Console-style pan/tilt fixed it (a
  decision above). A check that only counted arrivals would have passed it.
- **The spec's `--late` example didn't hold for Sweep.** "40° at 120 BPM needs 0.83 s" is
  a 40° move, but Sweep at the default Spread (30° across, 15° out) swings pan from
  atan2(30, 15) = 63.43° to −63.43°: 126.87° a step, 2√(D/A) = 1.4763 s from rest at full
  speed. The check now brackets the closed-form threshold, 60/1.4763 = 40.64 BPM: 6 of 6
  land at 40 BPM, 0 of 6 at 41. At 120 BPM 4 of 22 land: only where a head, sent back
  before arriving, overshoots toward the mark it was sent back to.
- **At 120 BPM the beats fall on frame boundaries.** 0.5 s is a whole number of frames at
  24, 30, 60 and 240 fps, so "step at the frame's start" (the negative control) is the same
  as "step where the beat fell", and the frame-rate check couldn't fail. The checks use
  128 BPM, whose beat (0.46875 s) falls inside frames at every rate tried.
- **A random hi-hat over-fired the onset detector.** The first `--kicks` feed added random
  high-band noise; the detector found 34 onsets in 7 kicks. The feed is now xenon's: a kick
  every half second from frame 20, alternately soft and hard, over a steady floor; `--audio`
  requires exactly 7 steps on the kicks' own frames, and none from a cold start into loud
  music.
- **Two checks were circular.** `--servo`'s full-range check first compared the plan's time
  against `kPanTime`, the shipped constant it was derived from; it now compares with the
  manual's literals, 3.517 and 2.180 s (and a mutant changes `kPanTime`). `--lens` compared
  the lens's luminance with I₀ from the plugin's own formula; it now also checks I₀ against
  24,000 lm over the beam's solid angle, computed independently (and a mutant changes the
  solid angle).
- **The v²/3a mutant survived the first mutation run** (24 of 25). No retarget started fast
  enough to have to overshoot, so a wrong stopping distance never mattered. A pan move to
  120° retargeted to 112° at 0.8 s, already braking and unable to stop short of 112°, now
  forces the overshoot; the mutant's plan is 0.40 s off.
- **The focus check fitted its slope.** It asked for the sharpest edge at the wall's
  distance and an edge width linear in the defocus disc (R² > 0.99), with the slope free. A
  mutant that makes the surface's blur disc 20% too wide passed it: sharpest at 10 m still,
  R² 0.9998. The slope is now predicted: a uniform disc spreads a step's 10–90% over 0.687 of
  its diameter, bound 5% (the tap pattern below). The mutant is now caught at +21%.
- **`|` in a mutant's description** broke mutate.sh's field split (conway's trap). The
  defocus mutant's description says "1/r + 1/f", not "|1/r − 1/f|".
- **The rm safety check refuses `rm $VAR/...`.** Scripts use literal paths or `"${S:?}"/...`.
- **`oxbow` isn't built at the path verify.sh defaults to**, and verify said only "skipped".
  It was built out of tree in scratch and passed in with `OXBOW=`; CLAUDE.md says how.
- **`TMPDIR`**: in a sandboxed session a heredoc into python hangs under the system's
  `/var/folders` TMPDIR (the fleet's trap); glslc.sh and mutate.sh need it set.
- **macOS has no `timeout(1)`**: mutants run under `perl -e 'alarm N; exec @ARGV'`.
- **`kPi` defined twice** (the harness and a shared header) was ambiguous; the harness's
  copy went.
- **The stir is subtle in stills.** n's standard deviation over the room is 0.261 (three
  octaves of value noise average toward zero), so at the default Stir 0.5 the density varies
  by about ±13%, and a beam seen through several cells averages it further. `--density`
  shows it moves a beam's brightness by up to 47% along its length at Stir 1. Finer wisps
  would need a finer atlas than 64×32×64. Open.

## Verified, and assumed

Verified on this Mac (Apple M4 Max, macOS 26.4.1), 2026-11-16, by `tools/verify.sh` (all
green, 3 min 1 s) and the checks' own output:

- the phase function recovered from the picture is HG's to +0.70% (720p) and +0.83% (180p)
  over 64 cases (8 angles 20°–160°, g 0, 0.5, 0.7, 0.9, 6 and 12 m from the lens);
- Beer–Lambert: the wall's slope is the light's path, 24.317821 m, to 1.3e-8 (720p) and
  4.5e-8 (180p); the air's 17.948 m against 18 m (−0.29%), brightest at σ = 1/L;
- the lens's luminance is I₀/(πa²)·exp(−σ·6 m) to 1.3e-6; I₀ = 697,270 cd exactly
  24,000 lm over 2π(1 − cos 6°);
- the servo: the manual's 3.517 and 2.180 s full-range times to 1e-12 s; 48 rest-to-rest
  moves on the closed form to 7.1e-15 s and on the phase-plane integrator to 1.9e-5 s
  (its 1e-5 s step); vectorial skew 2.9e-15 s, the faster axis's shape scaled (1/k, k²) to
  1.4e-15; 5 retargets to 1.6e-5 s; 4000 IK directions to 9e-16, and the nearest turn taken;
- `--late`: 40 BPM 6/6, 41 BPM 0/6, 120 BPM 4/22; the pan at 3.217 s the same at 24, 30,
  60, 240 fps to 9.2e-14°;
- the chase on the host's beat crossings at 30 and 240 fps to 4.4e-16 s; through the whole
  plugin to 3.4e-8 s; presses, onsets and Hold counted exactly; the own clock's Bar 2 steps
  in 4 s;
- the aim: centroid 0.0000 px from the centre, wall and floor, both rasters;
- focus: the edge 1.63 px at the wall's distance (10 m), the sharpest of 10 focuses, 37.3 px
  at 4 m and 16.5 px at 30 m; slope 0.696 (720p) and 0.675 (180p) against the uniform
  disc's 0.687, R² 0.9995 and 0.9981; the air's grating contrast peaks within a bin of
  r = f at f = 7, 9, 12 m, both rasters;
- the gobo upright and unmirrored on the wall from behind the lens and on the floor from
  the audience (RGBW quadrants);
- density: mean +0.0025 against a bound of 0.0351 (3 sd/√498 base cells), sd 0.261, range
  −0.849 to +0.948; correlation 0.99999 a frame later and 0.022 a minute later; a beam's
  thinning follows the atlas to 0.0005 (720p) and 0.0008 (180p); crossing beams scaled by
  the same factor to 0.93% (720p) and 0.89% (180p);
- alpha: Mix 0 bit-exact (0 of 3,686,400 floats); alpha mix(0.3, 1, Mix) to 2.4e-8 in all
  three Clip As; Screen with nothing in front returns 1 − (1 − c)^T to 1.3e-7; the source's
  alpha 1 everywhere;
- `--audio` 7/7 on the kicks' frames, 0 from a cold start; `--clock` dt to 6.2e-11 s and the
  drift to 1.6e-11 m at 499,000,000 ms; `--resize` 0 bytes, twice; `--state` 0 wrong, both
  plugins; `--names` 22 and 24 parameters; `--cues`;
- `--pipe`, `--film`, a bad cue and a bad option refused, `| head -c 1` exits 1, both;
- 17 negative controls, 26 mutants, 0 dead controls, 5 shaders through glslc, both bundles
  universal and read by `oxbow` (`SW Haze`/`HZ01`/source, `SW Haze Over`/`HZ02`/effect)
  with `oxbow selftest` passing;
- `tools/mie.py --check` against Wiscombe's MIEV0 values (Q_back 2.925341 against 2.925340
  and 1.695064 against 1.695084; case 12's Q_ext, Q_sca and g to six figures).

Assumed, not measured: the droplet size distribution (g = 0.698–0.720 over the five
tabulated); the motors' acceleration (each ramp a quarter of a full-range move); the stir's
statistics, cell sizes, drift and time constant; the room's geometry and albedos (0.5 back
wall, 0.15 floor); the filter colours; the 20 cd/m² exposure reference; how Resolume sends
its beat and audio to this plugin (never loaded).

## Would this hold on another rasteriser, at another raster?

Every picture check runs at 1280×720 and 320×180 on this Mac's GPU. All twelve GL checks
then in the harness passed on Apple's software renderer at 320×180 on 2026-11-16 (about
15 minutes), and `--lens`, added later, passes there in verify's subset. Verify reruns
`state alpha aim lens gobo extinction clock` on it every time.

- `--phase`: 3%. Sources: the chord's second-order terms (1/r² and θ across the beam's
  chord, ≤ 0.4% each at 20°), the gobo disc's one-texel edge in 256 (0.8%), and the
  stratified samples' edges, averaged over a block of pixels that all see the same
  geometry. No pixel-exact dependence; holds on the software renderer.
- `--extinction`: wall 1e-4 relative on the slope (GLSL's exp is good to a few ULP; the
  slope is over five σ, so the error is far below), residual 1e-4; air 1% (the chord's
  terms and the sampler). Slopes of block means; both rasters; software renderer.
- `--lens`: 1e-4 relative (float luminance through exp; the lens disc's interior only,
  away from its edge pixels); the I₀ identity in double to 1e-12. Software renderer.
- `--servo`, `--late`, `--chase`, `--names`, `--cues`: no GL, double. 1e-9 s for the closed
  form (double over a few seconds), 5e-5 s against the integrator (five of its 1e-5 s
  steps), 1e-9° for the frame-rate spread.
- `--aim`: 0.05 px. Symmetry: a beam square to a surface seen from its own axis is symmetric
  about the frame's centre on any rasteriser that samples pixel centres, which GL requires;
  0.05 px covers float rounding in the centroid. Both rasters, software renderer.
- `--focus`: the wall's sharpest edge must fall exactly on the wall's distance (one sweep
  step either side is a different focus); the slope 5% against the uniform disc's 0.687:
  the shader's 16 golden-spiral taps give 0.663–0.698 as their box pre-filter goes from none
  to a third of the disc (3.5% either side; computed from the tap pattern on 2026-11-16),
  and the pixel grid the rest; R² 0.99. The air: within a 0.5 m bin plus the level-0
  plateau (where the blur is under one gobo texel the mip is level 0 and the contrast is
  flat, so the peak can sit anywhere on it). Both rasters.
- `--gobo`: quadrant colours from block means, a classification, not a level. Both rasters,
  software renderer.
- `--density`: the atlas statistics are CPU reads of the field (raster-free) against 3 sd/√
  cells, the sampling error of the base cells; the beam's thinning against the atlas 0.01
  (R16F holds n to about 1e-3, plus bilinear interpolation between atlas texels); crossing
  beams 2%, the spec's bound: both beams sample the field over the same crossing but weight
  it by their own cross-sections, so they agree only as far as the field varies across a
  2.5° beam's 0.4 m width there (the finest cell is 0.75 m). Both rasters.
- `--alpha`: Mix 0 bit-exact, because it's texelFetch with no filtering, true on any
  rasteriser; the alpha 1e-6 (one float mix); Screen 1e-3 below 0.98 per channel (the
  log/exp round trip in float). Both rasters, software renderer.
- `--beat`: 1e-6 s through the plugin (the step instant is computed in double from float
  bar phases Resolume sends); `--audio`: frame-exact; `--clock`: 1e-9 s and 1e-6 m in
  double.
- `--resize`: 0 bytes, within one context on one GPU: a deterministic rasteriser is all it
  needs. `--state`: GL queries, exact.

## The negative controls and mutants

`hztest --negative`: 17 wrong models, each must fail its check (and each check also asserts
the thing measured happened: a beam in frame, a move made).

| check | the wrong model | failed |
| --- | --- | --- |
| phase | an isotropic phase function | 2 of 2 (+3351%) |
| phase | cos θ negated: the forward peak thrown backward | 2 of 2 |
| phase | the inverse square dropped | 2 of 2 |
| extinction | nothing lost between the lens and the droplet | 4 of 4 |
| extinction | nothing lost between the droplet and the eye | 4 of 4 |
| servo | acceleration unbounded: no ramps | 4 of 6 |
| servo | vectorial sync off | 2 of 6 |
| late | a step at its frame's start, not where the beat fell | 1 of 4 (the frame-rate spread, 3.8°) |
| late | acceleration unbounded | 2 of 4 |
| chase | a step at its frame's start | 3 of 4 |
| focus | the gobo sharp at every distance | 8 of 8 |
| gobo | the gobo mirrored | 4 of 4 |
| density | each beam sees its own haze | 2 of 6 |
| alpha | the clip's alpha passed through | 2 of 8 |
| beat | a step at its frame's start | 1 of 1 |
| audio | the analyser never primed | 1 of 2 |
| cues | every control ramped between keys, options and events too | 1 of 1 |

No natural wrong model for `--aim`, `--lens`, `--resize` or `--state`; mutants cover aim
and lens.

`tools/mutate.sh`: 26 one-character mutants of the shipped code, each built arm64-only in a
copy of the tree and run under a 300 s alarm at 320×180. All 26 caught on 2026-11-16.

| file | the mutant | caught by |
| --- | --- | --- |
| Shaders.cpp | HG's denominator 1 + g² + 2g cos θ | `--phase` |
| Shaders.cpp | HG's normalisation 1 + g² | `--phase` |
| Shaders.cpp | the equiangular sampler's Jacobian 1 − tan² | `--phase` |
| Shaders.cpp | the eye's path subtracted from the light's | `--extinction` |
| Shaders.cpp | a surface's cosine with the wrong sign | `--extinction` |
| Shaders.cpp | the lens's area πa + a | `--lens` |
| Shaders.cpp | the defocus from 1/r + 1/f | `--focus` |
| Shaders.cpp | the surface's blur disc 20% too wide (0.5 → 0.6) | `--focus` (the predicted slope; passed the old fitted check) |
| Shaders.cpp | the gobo's centre off the beam's axis (0.5 → 0.6) | `--aim` |
| Shaders.cpp | the gobo mirrored left to right | `--gobo` |
| Shaders.cpp | the beam thick where the haze is thin | `--density` |
| Shaders.cpp | the haze's upper slice read from the wrong tile | `--density` |
| Shaders.cpp | the screen's light through a metre of haze it doesn't cross | `--alpha` |
| Shaders.cpp | the Over's alpha mixed toward clear | `--alpha` |
| Motion.cpp | the servo cruising when it never reaches full speed | `--servo` |
| Motion.cpp | a moving yoke's peak speed forgetting its head start | `--servo` |
| Motion.cpp | the stopping distance v²/3a | `--servo` (survived until the overshooting retarget was added) |
| Motion.cpp | the sync's bisection on unscaled acceleration | `--servo` |
| Motion.cpp | pan's acceleration from the ramp plus the time | `--servo` |
| Motion.h | the Mythos's pan time 3.617 s | `--servo` |
| Motion.cpp | the yoke's over-the-top solution at p + 170 | `--servo` |
| Motion.cpp | the beat's place in its frame from the sum of phases | `--chase` |
| Motion.cpp | Sweep never swinging back | `--late` |
| Audio.cpp | the first frame primed with a flux of 1 | `--audio` |
| Haze.cpp | the haze drifting up instead of settling | `--clock` |
| Haze.cpp | the beam's solid angle 2π(2 − cos α) | `--lens` |

## The sweep's honest list

`python3 tools/sweep.py` sweeps all 17 of the source's controls that have pixels and 19 of
the Over's (the About block's four and the Audio buffer have none; Audio is driven through
Step On's Onset) and finds every one live. `--bare` drops the per-control contexts and finds
this dead, for the reason its context exists:

- **Step On**, both plugins: it's swept between Step Only and Onset, and with no music in
  the buffer Onset never fires, so both ends are still.

Under the spec's Gobo default the Over's Gobo control was dead in `--bare` too (the clip is
the gobo then); under Screen it acts.

## Demo clips and the stills

`--pipe` stills of the defaults through Resolume's bundled clips, judged by eye on contact
sheets, not by a metric. Screen (shipped): 21 of 21 show beams and the clip. Gobo: most
clips gave near-black beams and no picture. The clips' own darkness carries through
(Bass 004, Synth 004 and SpaceUniverse 04 are nearly black to begin with). The screen fills
about the middle half of the frame's width at the default camera; whether a VJ wants it
bigger is open.

## Render cost

`hztest --bench`, GPU time by GL_TIME_ELAPSED around the whole frame, median of 120 after
240 of warm-up (4K: 30 after 30, defaults only), verify's run on 2026-11-16 with other
builds on the machine:

| | 720p | 1080p | 4K |
| --- | --- | --- | --- |
| source, defaults (Medium) | 0.23 ms | 0.34 ms | 0.96 ms |
| source, High | 0.35 | 0.62 | |
| source, Low | 0.18 | 0.26 | |
| source, 12 fixtures, 30.8°, Medium | 0.45 | 0.73 | |
| source, 12 fixtures, 30.8°, Low | 0.32 | 0.48 | |
| Over, defaults (Medium) | 0.25 | 0.45 | 1.26 |

1080p at the defaults is 2% of a 60 fps frame. A run 20 minutes earlier gave up to 12%
less (the Over 0.40 ms at 1080p, twelve beams 0.65): the machine is shared. One GPU only
(an M4 Max); a small GPU or the software renderer is another matter.

## The browser demo (2026-11-18)

https://haze-demo.stoatworks-labs.com/, from `demo/`, deployed by `wrangler.toml` (a Worker
route: the zone is at Cloudflare's 100 custom domains, so `haze-demo` is a proxied AAAA
`100::` record made through the API) and `.github/workflows/deploy.yml` on every push to
main. Both plugins on one page: the kit's Plugin dropdown is SW Haze or SW Haze Over, and
`?plugin=over` opens the Over.

- **The GPU half is the plugin's.** `demo/shaders.js` is spliced from `Shaders.cpp` by
  `demo/tools/check_shaders.py --write`; the same script (run by `tools/verify.sh`) fails
  if a character drifts, if Shaders.cpp grows a piece it does not list, if `Assemble()` or
  InitGL join the pieces differently, or if `render.js` stops joining them the same way.
  `render.js` uses the plugin's buffers (R16F atlas, mipmapped RGBA16F gobo, RGBA32F air at
  Quality's raster), units and uniforms. WebGL2 needs EXT_color_buffer_float (without it the
  page stops and says so) and OES_texture_float_linear for a linear RGBA32F air buffer
  (without it the air buffer is RGBA16F and the line under the picture says so).
- **The CPU half is a port, and only a reader checks it.** `demo/port.js`: Controls.cpp
  whole, Motion.cpp whole (stage, camera, Plan and the sync's bisection, Yoke, Aim, Lean
  with Random's splitmix64 in BigInt, the chase), and Haze.cpp's CPU half (the clock's dt
  and its cap, the beat, the stir's offsets, the uniforms as floats). It was compared once
  with the C++ when written: a scratch printer linking Controls.cpp and Motion.cpp, with
  ProcessOpenGL's glue and sceneUniforms' casts copied line for line, against the same
  printout from port.js, over every control row, both host orders, every conversion on
  1,005 host values and the inverses, and ten runs (3,060 frames: every pattern, every
  Step On but Onset, 40/41/90/120/128/140/174 BPM, 30/50/60 fps with a jittery clock, a
  stall, mid-run changes of count, layout, speed and pattern, Step presses). Every step
  count and step time the same, every conversion equal, every pose within 4e-14 relative
  (libm's and V8's sin, cos and atan2 differ in the last ulps), every float the GPU is
  handed bit-identical but near-zero vector components (1e-17 against 0, at most 1.1e-16
  apart). The one structural difference is meant: the port's HostOrder leaves out the
  About run. That comparison is not in the repository and nothing repeats it. A matched
  frame (180 frames at 60 fps, 120 BPM, the page's clock driven frame by frame) looks the
  same as `hztest --out` for the source, the Over on the look card and Arc/Blue/orbit 36°.
- **What the page stands in for**: Resolume's BPM and bar phase are a Tempo menu (120 by
  default) and the page's clock, in 4/4; the Over's clip is the kit's generated clips, led
  by hztest's look card ported to GLSL, or a file the visitor picks (premultiplied, so the
  plugin's own multiply by alpha makes an edge alpha squared). No audio: Audio is not on
  the panel, Onset never fires. Step is a toggle released after a frame. Restart and the
  Plugin dropdown start a new instance with that plugin's defaults. Every gap is listed on
  the page.
- **Photosensitivity**: a note at the top of the page, no Start gate. The panel opens on
  the plugin's defaults (Fan), and no preset uses Audience. A link that opens on Audience
  opens paused, and while Pattern is Audience a caution sits under the picture. The release
  measurements behind the note, at 120 BPM: Audience on Beat up to 3 transitions a second
  over the whole frame and 7 in a third-of-frame window; on 4 a Beat 7 and 9; every other
  pattern at every step rate 0.
- **Traps**: headless Chrome draws this with `--use-gl=angle --use-angle=metal` (cdpshot's
  `GL_FLAGS`), no SwiftShader needed. Paused screenshots of the canvas differ in the bottom
  three rows by 2 levels from one capture to the next (the clip's rounding, not the render),
  so compare them with a tolerance. The kit's panel rows have a `display` of their own, so
  hiding Clip As, Mix and the Clip picker is by inline style, not `hidden`. The noise and
  gobo programs have no `Hooks` (the compiler drops it there, in the plugin too), so
  `missing()` lists it for those two.

## Open questions

- **Is Screen the right default for the Over?** It survives every demo clip; Gobo is the
  idea in the name ("the clip as the gobo") and is one click away.
- **Audience whites the frame out** at the default exposure. Right for a photograph of a
  show, maybe not for a VJ who picks it from a list.
- **The stir is subtle.** More visible haze structure needs either a finer field or a
  different control law (s > 1 breaks σ ≥ 0 and the mean).
- **The screen's size in frame** (about half the width at the default camera).
- **Never in Resolume.** Nothing is known about Arena's beat, clock or FFT reaching it, or
  how it reads at a show.
- **Never built on Windows.** MSVC's `far`/`near` and the GLEW path are untested here.
