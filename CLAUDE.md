# haze

Moving heads in a hazed room, for Resolume Arena/Avenue, as two FFGL plugins from one
core: single scattering by stage haze (Henyey–Greenstein, Beer–Lambert) ray-marched per
beam, the clip as the gobo, servo-limited pan/tilt chasing on the beat. C++/GLSL, CMake
MODULE → universal `.bundle` (macOS) + Windows `.dll` (CI-built, Arena-gated). MIT.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the rig in a hazed room | `HZ01` | `SW Haze` | `Haze.bundle`, `com.stoatworks.ffgl.haze` | source |
| the rig in front of the clip | `HZ02` | `SW Haze Over` | `Haze Over.bundle`, `com.stoatworks.ffgl.haze.over` | effect |

Read `AGENTS.md` before touching the servo plan (`source/Motion.cpp`), the chase's
pan/tilt, the air pass's sampler, the stir atlas, the Over's composite or its default.
The design is `~/Projects/resolume/specs/SPEC-haze.md`; its "As built" section says where
the code departs from it.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64` (iteration builds here are
  `build-arm64`)
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects)
- Render offline: `./build/hztest --out /tmp/haze.png` (the source on its defaults at
  120 BPM, 3 s in)
- The Over: `./build/hztest --over --out /tmp/over.png` (the look card as the clip),
  or `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/hztest --list` (`--over` for the effect's)
- Set anything by name: `./build/hztest --set "Pattern=Sweep" --set "Haze=0.7"`
  (a standard control takes the host's 0..1; an option takes its name; a bad name exits 2)
- Press something later: `./build/hztest --at 100 "Step=1"`; music in the Audio buffer:
  `--kicks` (a kick every half second from frame 20, alternately soft and hard)
- Clock and beat: `--fps N` (default 60), `--bpm N` (default 120; 0 sends no beat, and the
  plugin runs its own clock)
- Debug views: `--hooks 1` raw linear radiance, `2` air only, `4` surfaces only (bits)
- Film: `./build/hztest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/hztest --over --pipe --size WxH | ffmpeg …`
  (a partial last frame is dropped; a reader that hangs up ends the run with exit 1)
- Cue sheets: `frame Name value` per line; an option by its name; standard controls ramp
  between keys, options and events step.

## Verify
- Everything: `tools/verify.sh [BUILD_DIR]` (reserved-word and WebGL2 greps, `glslc`,
  `tools/mie.py --check`, the submodule pin, a fresh universal build in `build-verify` with
  no warnings, both bundles through lipo, nm, plist, ad-hoc signature, `oxbow probe` and
  `selftest`; every check below; seven on Apple's software renderer; `--offline`; `--pipe`
  for both; the negative controls; 26 mutants; the sweep; the bench): about three minutes
  on this Mac.
- **Set `TMPDIR`** to a writable scratch directory in a sandboxed Claude session, or the
  heredocs in `glslc.sh` and `mutate.sh` hang.
- **Set `OXBOW`** to a built `oxbow` binary. The default,
  `~/Projects/resolume/oxbow/build/oxbow`, isn't built on this Mac, and verify only says
  "skipped" without one. Build it out of tree: `cmake -S ~/Projects/resolume/oxbow -B
  <scratch>/oxbow-build && cmake --build <scratch>/oxbow-build` (never inside ~/Projects).
- **The light** (GL, 1280x720 and 320x180 each): `--phase` (HG recovered from the picture),
  `--extinction` (ln L against σ: the slope is the path), `--lens` (the lens's luminance
  I₀/πa²), `--aim` (the spot's centroid on the axis), `--focus` (the edge sharpest at the
  focus; the air's grating contrast peaks at r = f), `--gobo` (the clip upright, unmirrored),
  `--density` (the stir's statistics; crossing beams thin together), `--alpha` (Mix 0
  bit-exact; alpha mix(src.a, 1, Mix); Screen returns the clip).
- **The motors** (no GL): `--servo` (the manual's times, the plan against the closed form
  and an independent phase-plane integrator, vectorial sync, retargets, IK), `--late` (Sweep
  lands at 40 BPM and not at 41; frame-rate independence), `--chase` (steps on the beat's
  crossings, presses, onsets, own clock).
- **The rest**: `--beat`, `--audio`, `--clock` (a 499,000,000 ms host clock), `--resize`,
  `--state`, `--names` (no GL), `--cues` (no GL).
- `--offline` runs the no-GL checks and their negatives (what CI would run).
- The checks can fail: `./build/hztest --negative` (17 wrong models), `tools/mutate.sh`
  (26 one-character mutants, each built arm64-only and run at 320x180 under a 300 s alarm).
- No dead controls: `python3 tools/sweep.py --build build` (both plugins; `--bare` drops
  the per-control contexts and lists the controls that only act with another one set).
- Render cost: `./build/hztest --bench` (720p and 1080p at each Quality, 4K at the defaults).
- `HZTEST_RENDERER=software` runs any of it on Apple's software renderer (slow: every GL
  check passed there on 2026-11-16, but the whole set takes about 15 minutes, so verify
  runs `state alpha aim lens gobo extinction clock`).
- `HZ_DEBUG=1` with `--late` prints each step's planned move.
- Where the default anisotropy comes from: `python3 -I tools/mie.py` (numpy only).

## Notes
- **Units.** Metres; x right (audience's view), y up, z toward the audience; floor y = 0,
  back wall z = −8. Haze is σ in m⁻¹ (albedo 1: σ_t = σ_s). Radiance is cd/m² over a
  20 cd/m² reference times 2^Exposure, then the tone curve.
- **The motors run on the CPU in double** (`source/Motion.cpp`): a time-optimal
  three-segment plan per axis, solved in closed form from (x₀, v₀); vectorial sync by
  bisection on k (limits V·k, A·k²). A step's instant is found inside the frame and the
  servo replans there, so motion is the same at any frame rate.
- **The chase is console-style**: patterns give pan and tilt directly (pan unwrapped to the
  turn nearest the last target, inside ±270°); inverse kinematics is used only for
  Audience's aim at the camera. AGENTS.md says why.
- **The air pass** runs at Quality's raster (full, half, quarter; 32, 24, 16 samples a beam)
  with equiangular sampling about each lens; surfaces and lenses are always the full raster.
- **The stir** is a 64×32×64 R16F field rendered each frame into an 8×8 atlas of 64×32
  slices; its lattice offset is reduced in double on the CPU. Extinction uses the room's
  mean σ, so Beer–Lambert stays exact at any Stir.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element
  index; Step acts on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `haze_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Generated, never edit here:** `source/StoatworksAbout.h` (stoatworks-backend's
  `sync-about.py`, from the website's projects.json: the guide button makes the About block
  23 and 25 parameters), `ATTRIBUTIONS.md` and the README's attributions block
  (`sync-attributions.py`, from `attributions/derived.json`), `.github/FUNDING.yml` and the
  issue forms. Registered 2026-11-18.
- **Public at https://github.com/stoatworks-labs/haze since 2026-11-18.** `ci.yml` builds
  macOS (`hztest --offline`, glslc) and Windows on every push, and loads both DLLs as a host
  does (`tools/wincheck.ps1`, whose `-Params` changes whenever a control or the About block
  does); `release.yml` builds both platforms, checks the DLLs the same way and publishes on a
  `v*` tag. Its macOS assets are unsigned: this Mac's autosign agent signs and notarises the
  published assets.

## Browser demo
- `demo/` is served at https://haze-demo.stoatworks-labs.com/ by the Worker in
  `wrangler.toml` (a route, plus a proxied AAAA `100::` record for `haze-demo`: delete the
  record and the page goes dark while deploys stay green). `.github/workflows/deploy.yml`
  redeploys it on every push to main; by hand: `cf-run npx wrangler deploy`.
- `demo/shaders.js` is a copy. After changing a shader, run
  `python3 demo/tools/check_shaders.py --write`, then `python3 demo/tools/check_shaders.py`;
  `tools/verify.sh` fails on any drift, in the pieces or in how they are joined.
- `demo/port.js` is a hand port of Controls.cpp, Motion.cpp and Haze.cpp's CPU half, and
  nothing checks it but a reader: change it by hand with the C++.
- `demo/vendor/` is the shared kit (`stoatworks-backend/resolume-demo`); never edit it.
- The page opens on the plugin's defaults, which do not flash; Audience does. Keep it out of
  the presets, keep the note at the top, and keep a link that opens on Audience paused.

## Not done yet
- Never loaded in Resolume on macOS; on Windows only on software rendering (win-lab's Arena
  gate, 2026-11-18: 15 passed, 0 failed, 4 skipped; expectation in plugin-bench
  `arena/expect/haze.json`, which holds Step On at Step Only and Stir at 0 so the picture
  stands still). No host has played it any music.
- No OpenFX port, no presets. The user guide is `docs/USER-GUIDE.md` (built to the site and
  `docs/USER-GUIDE.pdf` by the website's `build_guides.py haze`).
- **Audience flashes** (the lenses white out the frame every other step): over 3 flashes a
  second in part of the picture on Beat and faster at 120 BPM, on the line on 2 Beats. The
  guide, the demo page, the video and the reel all carry the warning; the defaults and every
  other pattern measure 0.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/haze/haze.YYYY-MM-DD.log
    %LOCALAPPDATA%\haze\logs\haze.YYYY-MM-DD.log

`HAZE_LOG_DIR` overrides the folder.
