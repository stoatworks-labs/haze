/**
 * Haze — browser demo.
 *
 * Moving heads in a hazed room. The one idea, from `AGENTS.md`: **a beam is
 * only visible because the haze scatters a little of it at you, and how much
 * depends on the angle between the beam and your eye.** For every pixel the
 * plugin integrates, along the eye's ray, the light each beam scatters toward
 * it (Henyey–Greenstein, dimmed by Beer–Lambert both ways), adds what the
 * beams light on the back wall and the floor and the lenses where you look
 * into one; the heads swing on servo-limited pan and tilt, chasing on the beat.
 *
 * Three files, and they are not equally faithful:
 *
 *   **render.js runs the plugin's own GLSL**, unedited: the noise, gobo, air
 *   and final passes, into buffers of the plugin's sizes and formats.
 *   shaders.js is spliced from source/Shaders.cpp by
 *   demo/tools/check_shaders.py --write, and the same script fails
 *   tools/verify.sh if a character, or the way the pieces are joined, drifts.
 *
 *   **port.js is a hand port of the CPU half, and only a reader checks it**:
 *   Controls.cpp, Motion.cpp (the stage, the servo plan and its sync, the
 *   inverse kinematics, the chase) and the CPU half of Haze.cpp's
 *   ProcessOpenGL (the clock, the beat, the stir's lattice offsets, the
 *   uniforms).
 *
 *   **This file is the page**: the parameter panel, the host's stand-in (its
 *   tempo and bar phase), the harness's clip, the line under the picture and
 *   the page's presets. None of it is the plugin.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page.** SW Haze (HZ01) is a source and SW Haze Over
 * (HZ02) an effect: one class with a flag, the same controls and defaults but
 * for the Over's Clip As and Mix. The Plugin dropdown is a new instance, as it
 * would be in Resolume: the chase, the yokes and the stir start again, the
 * panel shows only that plugin's declarations, and every control goes back to
 * its constructor's default. `?plugin=over` opens the effect; Copy link
 * carries it. The source reads no clip, so its Clip picker is hidden.
 *
 * **Nothing audio.** Audio is an FFT buffer Resolume fills, read only by Step
 * On's Onset. A browser has no Resolume FFT, so Audio is absent from the panel
 * rather than present and dead; with no spectrum the plugin's analyser never
 * fires, so Onset never steps the chase here, as in a layer with no audio.
 *
 * **The host is the page.** Resolume sends its BPM and bar phase every frame;
 * here the tempo is a menu in the host row and the bar phase runs on the page's
 * clock in 4/4, continuous across a tempo change. The clock is the kit's:
 * seconds from frame deltas, capped at a tenth of a second a frame (the plugin
 * caps a frame there too). The plugin's vote on seconds or milliseconds is not
 * ported, because this host's unit is known. Restart starts a new instance
 * (the page never sends the plugin a clock that runs backwards).
 *
 * **Photosensitivity.** The Audience pattern flashes (index.html says how
 * much, at the top of the page). The page opens on the plugin's defaults,
 * which do not; no preset uses Audience; and a link that opens on Audience
 * opens paused, with Play to start it.
 *
 * **The About block is not a parameter here**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { SOURCES } from './vendor/sources.js';
import { createPasses } from './render.js';
import {
  f32, PT, Kind, InfoOf, HostOrder, OptionIndex, Haze,
  Layout, Gobo, Colour, Pattern, StepOn, ClipAs,
  FixturesFromParam, ZoomFromParam, ParamFromZoom, FocusFromParam, ParamFromFocus,
  HazeFromParam, ParamFromHaze, AnisotropyFromParam, StirFromParam, SpreadFromParam,
  MotorSpeedFromParam, OrbitFromParam, ParamFromOrbit, ExposureFromParam,
} from './port.js';

//===========================================================================
// The harness's clip: tools/hztest/main.cpp's buildCard (schlieren's look
// card), ported to GLSL, so the Over's defaults look here as `hztest --over
// --out` does. A sunset sky, a sun, a skyline with lit windows, a stripe of
// 75% bars and a white title block. Rows bottom first, as there.
//===========================================================================
const LOOK = `
uniform float Time;
uniform vec2 Resolution;
in vec2 uv;
out vec4 fragColor;

uint pcgCard( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

float hash01( uint a, uint b )
{
	return float( pcgCard( a * 2654435761u ^ pcgCard( b + 0x9e3779b9u ) ) ) * ( 1.0 / 4294967296.0 );
}

void main()
{
	vec2 px  = floor( gl_FragCoord.xy );
	float w  = Resolution.x;
	float h  = Resolution.y;
	float s  = min( w, h );
	float u  = ( px.x + 0.5 - 0.5 * w ) / s;
	float v  = ( px.y + 0.5 - 0.5 * h ) / s;
	float k  = clamp( 0.5 + v, 0.0, 1.0 );
	vec3 c   = vec3( 0.95 - 0.8 * k, 0.45 - 0.3 * k, 0.25 + 0.5 * k );
	float d  = sqrt( ( u + 0.25 ) * ( u + 0.25 ) + ( v + 0.02 ) * ( v + 0.02 ) );
	if( d < 0.2 )
		c = vec3( 1.0, 0.85 - 0.5 * d, 0.35 );
	int block = int( floor( ( u + 2.0 ) * 11.0 ) );
	float top = -0.12 + 0.16 * hash01( uint( block ), 7u );
	if( v < top )
	{
		c = vec3( 0.05 );
		int wx = int( floor( ( u + 2.0 ) * 70.0 ) );
		int wy = int( floor( v * 60.0 + 100.0 ) );
		if( ( wx % 3 ) != 0 && ( wy % 2 ) == 0 && hash01( uint( wx * 131 + wy ), 9u ) > 0.55 )
			c = vec3( 1.0, 0.9, 0.55 );
	}
	if( v < -0.36 )
	{
		int bar = int( clamp( floor( ( px.x + 0.5 ) / w * 7.0 ), 0.0, 6.0 ) );
		vec3 b  = vec3( 0.0, 0.0, 1.0 );
		if( bar == 0 ) b = vec3( 1.0, 1.0, 1.0 );
		else if( bar == 1 ) b = vec3( 1.0, 1.0, 0.0 );
		else if( bar == 2 ) b = vec3( 0.0, 1.0, 1.0 );
		else if( bar == 3 ) b = vec3( 0.0, 1.0, 0.0 );
		else if( bar == 4 ) b = vec3( 1.0, 0.0, 1.0 );
		else if( bar == 5 ) b = vec3( 1.0, 0.0, 0.0 );
		c = 0.75 * b;
	}
	if( abs( u - 0.42 ) < 0.28 && abs( v - 0.3 ) < 0.06 )
		c = vec3( 0.95 );
	fragColor = vec4( c, 1.0 );
}
`;

if (!SOURCES.some((s) => s.id === 'look')) {
  SOURCES.unshift({
    id: 'look',
    name: 'Look card (the harness’s clip)',
    hint: 'The plugin harness’s own clip (buildCard in hztest), drawn in the page: a sunset sky, a sun, a skyline, a stripe of bars and a white block. It is what `hztest --over` puts on the screen.',
    fragment: LOOK,
  });
}

//===========================================================================
// The host's tempo. Resolume sends its BPM and bar phase every frame; here the
// page is the host, and its bar phase runs on the page's clock at the tempo in
// the host row, continuous when the tempo changes, in 4/4. Paused, it stands
// still. 40 and 41 are there because Sweep lands at one and not the other.
//===========================================================================
const TEMPOS = [40, 41, 60, 90, 120, 128, 140, 174];
const host = { bpm: 120, phase: 0, lastTime: null };

//===========================================================================
// The renderer the kit drives.
//===========================================================================
/** The live instance, for the line under the picture (and a headless check). */
export const current = { cpu: null, variant: null, frame: null, passes: null };

function createRenderer(gl, quad) {
  const passes = createPasses(gl, quad);
  current.passes = passes;

  return {
    render({ input, params, width, height, time, variant }) {
      const isEffect = variant === 'over';
      // A new instance: the first frame, the Plugin dropdown, or Restart (a
      // clock that ran backwards, which the page never hands the plugin).
      if (current.cpu === null || variant !== current.variant || (host.lastTime !== null && time < host.lastTime)) {
        current.cpu = new Haze(isEffect);
        current.variant = variant;
        passes.reset();
        // The host's bar phase is where the plugin's own clock would put it,
        // so its first beat frame does not step the chase through a bar.
        host.phase = (time * host.bpm / 240) % 1;
        host.lastTime = time;
      }
      const cpu = current.cpu;

      // The host restates every parameter each frame.
      for (const p of PARAMS) cpu.SetById(p.pid, params.get(p.id));
      // Step is a toggle here: the plugin counts its rising edge, and the page
      // lets go of it after this frame, as a host does with an event button.
      if (params.get('step') > 0.5) queueMicrotask(() => params.set('step', 0));

      if (time > host.lastTime) host.phase = (host.phase + (time - host.lastTime) * host.bpm / 240) % 1;
      host.lastTime = time;
      cpu.SetBeatInfo(host.bpm, host.phase);

      const frame = cpu.ProcessFrame(time, width, height);
      current.frame = frame;
      passes.draw(frame, isEffect ? input : null, width, height);
    },
  };
}

//===========================================================================
// The parameters: the constructor's, in HostOrder()'s order and kTable's
// groups, with the plugin's names, element lists and defaults. `only` says
// which plugin declares it.
//===========================================================================
const sig = (n) => (n >= 100 ? n.toFixed(0) : n >= 10 ? n.toFixed(1) : n >= 1 ? n.toFixed(2) : n.toFixed(3));
const signed = (n, digits = 0) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(digits)}`;

/** id, ParamId, display, hint. Audio (PT.AUDIO) is not here: see the differences. */
const SPEC = [
  ['fixtures', PT.FIXTURES, (v) => `${FixturesFromParam(f32(v))}`,
    'How many moving heads, 1 to 12, side by side across the stage (at most 3 m apart and 16 m across in all). Each is a Clay Paky Mythos: a lamp, a gobo and a 157 mm lens on a pan/tilt yoke.'],
  ['layout', PT.LAYOUT, null,
    'Line: a straight truss 7 m up. Arc: the truss bowed toward the audience. Floor: on the deck at the back, pointing up. Split: alternately truss and floor.'],
  ['zoom', PT.ZOOM, (v) => `${ZoomFromParam(f32(v)).toFixed(1)}°`,
    'The beam’s full angle, 2.5° to 30.8° (the Mythos’s range). The same 24,000 lumens spread wider is a dimmer beam.'],
  ['gobo', PT.GOBO, null,
    'The gobo the lens images: Open, Dots, Bars, Star, Ring, Breakup. In the Over it acts in Clip As Screen and Backdrop; in Clip As Gobo the clip is the gobo.'],
  ['colour', PT.COLOUR, null,
    'The colour filter: chosen colours, not measured dichroics. White is sRGB white, not the lamp’s 7,800 K.'],
  ['focus', PT.FOCUS, (v) => `${sig(FocusFromParam(f32(v)))} m`,
    'Where the lens images the gobo sharp, 2 to 30 m from the lens, geometrically. Nearer and farther the gobo blurs by the lens’s defocus disc, on the wall and floor and along the beam in the air.'],

  ['haze', PT.HAZE, (v) => `${HazeFromParam(f32(v)).toPrecision(2)} m⁻¹`,
    'The haze’s extinction coefficient σ, 0.001 to 0.1 per metre, geometrically: 0.015 is a heavy concert haze, the top decade a fog. The scattered light grows with σ and is dimmed by exp(−σL), so a beam seen across a path L is brightest at σ = 1/L.'],
  ['anisotropy', PT.ANISOTROPY, (v) => `g ${AnisotropyFromParam(f32(v)).toFixed(2)}`,
    'Henyey–Greenstein’s g, 0 to 0.95: how strongly the haze scatters forward. 0.70 is the Mie asymmetry of a hazer’s oil droplets, so a beam seen side-on is faint and the same beam swung toward you flares.'],
  ['stir', PT.STIR, (v) => StirFromParam(f32(v)).toFixed(2),
    'How much the density varies through the room, 0 to 1: σ(1 + Stir · n), with n a slowly drifting field of three octaves of noise. Two beams crossing a thin patch dim there together. Subtle in stills.'],

  ['pattern', PT.PATTERN, null,
    'Fan: out and in. Cross: the two halves crossing. Sweep: every head side to side together. Audience: every other step aims the beams straight into the camera — this one flashes; see the note at the top. Wave: a travelling wave. Random. Hold: no steps at all.'],
  ['stepOn', PT.STEP_ON, null,
    'When the chase steps. 4 a Beat, 2 a Beat, Beat, 2 Beats and Bar follow the host’s bar phase (here the page’s, at the tempo in the host row), each step at the instant inside the frame where its beat fell. Onset steps on a kick in the Audio input, which a browser does not have, so here Onset never steps. Step Only: only Step.'],
  ['spread', PT.SPREAD, (v) => `${SpreadFromParam(f32(v)).toFixed(1)}°`,
    'How far the patterns lean the heads, 5° to 90°.'],
  ['motorSpeed', PT.MOTOR_SPEED, (v) => `${(100 * MotorSpeedFromParam(f32(v))).toFixed(0)}%`,
    'The motors’ speed limit, 5% to 150% of the Mythos at Fast (pan 540° in 3.517 s, tilt 244° in 2.180 s), geometrically; the acceleration goes with its square. Pan and tilt arrive together. A head sent somewhere else before it arrives turns back from wherever it is.'],
  ['step', PT.STEP, null,
    'One step now, in any Step On mode, on the press. FF_TYPE_EVENT in the plugin; a toggle the page lets go of after one frame here.'],

  ['orbit', PT.ORBIT, (v) => `${signed(OrbitFromParam(f32(v)), 0)}°`,
    'The camera round the stage, −90° to 90°, 13 m from the point under the truss’s centre, at eye height.'],
  ['exposure', PT.EXPOSURE, (v) => `${signed(ExposureFromParam(f32(v)), 1)} stops`,
    'Radiance over a 20 cd/m² reference, times 2^Exposure, then the tone curve. The lit floor is far brighter than a beam seen side-on, so spots blow out before beams do.'],
  ['quality', PT.QUALITY, null,
    'The air’s raster and samples a beam: High, full and 32; Medium, half and 24; Low, a quarter and 16. The wall, the floor and the lenses are always drawn at the full raster.'],
  ['clipAs', PT.CLIP_AS, null,
    'Over only. Screen: the back wall shows the clip, dimmed by the haze in front of it, with the beams playing in front. Gobo: every beam carries the clip as its gobo, cropped square and masked by the round gate. Backdrop: the back wall is the clip, revealed only where a beam lands.'],
  ['mix', PT.MIX, (v) => f32(v).toFixed(2),
    'Over only. The clip against the rig: at 0 the clip comes back untouched.'],
];

const sourceOrder = HostOrder(false);
const overOrder = HostOrder(true);

const PARAMS = SPEC.map(([id, pid, display, hint]) => {
  const info = InfoOf(pid);
  if (info.id !== pid) throw new Error(`${id}: not a declared parameter`);
  const only = sourceOrder.includes(pid) ? undefined : 'over';
  const p = { id, pid, name: info.name, group: info.group, hint, only };
  switch (info.kind) {
    case Kind.Option: return { ...p, type: 'option', elements: info.options.slice(), default: info.defaultValue };
    case Kind.Event: return { ...p, type: 'boolean', default: 0 };
    case Kind.Standard: return { ...p, type: 'standard', display, default: info.defaultValue };
    default: throw new Error(`${id}: the kit has no control for this kind`);
  }
});
{
  const pids = PARAMS.map((p) => p.pid);
  const want = overOrder.filter((id) => id !== PT.AUDIO);
  if (pids.join() !== want.join()) throw new Error('the panel must carry every control but Audio, in the plugin’s order');
}

// Presets: whole states on top of the constructor's defaults. None uses
// Audience, which flashes.
const PRESETS = {
  'The defaults: six heads fanning out and in on the beat': {},
  'The README’s picture: a blue wave on a split rig': {
    layout: Layout.Split, pattern: Pattern.Wave, colour: Colour.Blue, fixtures: 0.64, spread: 0.45,
  },
  'Sweep: too far to land at 120 BPM (try 40 and 41)': { pattern: Pattern.Sweep },
  'Amber cross from the floor, wide': {
    layout: Layout.Floor, pattern: Pattern.Cross, colour: Colour.Amber, zoom: ParamFromZoom(20.0),
  },
  'A fog: the far ends of the beams fade': { haze: ParamFromHaze(0.06) },
  'Breakup at random, seen from the side': {
    pattern: Pattern.Random, gobo: Gobo.Breakup, stepOn: StepOn.TwoBeats, orbit: ParamFromOrbit(45.0),
  },
  'A still rig to walk round (Hold)': { pattern: Pattern.Hold, orbit: ParamFromOrbit(-30.0), focus: ParamFromFocus(6.0) },
  'Over: the clip as every beam’s gobo': { clipAs: ClipAs.Gobo, zoom: ParamFromZoom(24.0), layout: Layout.Arc },
  'Over: the beams light the clip (Backdrop)': { clipAs: ClipAs.Backdrop, pattern: Pattern.Cross, zoom: ParamFromZoom(22.0) },
};

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================
const DIFFERENCES = [
  'The CPU half is a PORT that only a reader checks: demo/port.js is a hand translation of Controls.cpp (every name, group, option list, default and conversion), Motion.cpp (the stage, the camera, each yoke’s time-optimal servo plan from where it is, the vectorial sync’s bisection, the inverse kinematics for Audience, every chase pattern including Random’s splitmix64, and the chase’s steps placed at the instant inside the frame where the beat fell) and the CPU half of Haze.cpp (the frame’s clock, the beat, each beam’s I₀ and angle, the stir’s lattice offsets reduced in double, and the uniforms as floats). JavaScript numbers are doubles, as the plugin’s motors are; every value handed to the GPU is rounded to float32 where the plugin rounds it. The repository’s hztest --servo, --late and --chase check the C++ and have never heard of this page.',
  'The GPU half is not a port: the noise, gobo, air and final passes are the plugin’s own GLSL, joined as the plugin joins them, into the plugin’s buffers (a 512 × 256 R16F stir atlas, a 512 × 512 mipmapped RGBA16F gobo, an RGBA32F air buffer at Quality’s raster). That needs EXT_color_buffer_float, and without it the page stops and says so. Sampling the RGBA32F air buffer bilinearly needs OES_texture_float_linear; a browser without it gets an RGBA16F air buffer instead, and the line under the picture says which this one got. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders or the way they are joined drifts from the plugin’s.',
  'No audio. The plugin declares an Audio buffer (Resolume’s FFT) that only Step On’s Onset reads. A browser has no Resolume FFT, so Audio is not on the panel; with no spectrum the plugin’s onset detector never fires, so Onset never steps the chase here, as in a layer with no audio routed. Step still steps it.',
  'The tempo and bar phase are the page’s: Resolume sends its own every frame. The tempo is chosen in the host row (120 BPM by default, as the plugin’s harness runs); the bar phase runs on the page’s clock, 4/4, continuous across a tempo change, and stands still when paused. The clock is seconds from frame deltas, capped at a tenth of a second a frame, as the plugin caps a frame; the plugin’s vote on whether its host sends seconds or milliseconds is not ported. Restart, and switching the Plugin dropdown, start a new instance: the yokes at their first step’s place, the stir from the beginning, every control at its default for the dropdown.',
  'A frame here is one of the display’s refreshes, not Resolume’s output frame. The plugin places each step at the instant inside the frame where its beat fell and replans the servo from there, so the heads move the same at any frame rate (its harness finds the same pan at 24, 30, 60 and 240 fps to 9.2e-14°); only where the frames fall differs.',
  'Step is FF_TYPE_EVENT in the plugin, here a toggle the page lets go of after one frame. The About block is not on the panel.',
  'Over: the clip is the kit’s generated one, or your own, premultiplied, and the plugin multiplies it by its alpha again for Gobo, Backdrop and Screen, so a half-transparent edge counts as its alpha squared here where Resolume’s own clips may arrive straight. GetMaxGLTexCoords is 1 here: the page’s textures are exactly the raster. The look card is the harness’s clip, ported to GLSL for the page; the plugin never sees it.',
  'The note at the top, the host row, the line under the picture and the Presets menu are the page’s, not the plugin’s. The heads-still-moving count is the page’s arithmetic on the ported yokes. A link that opens on Audience opens paused.',
  'The output is the page’s 8-bit canvas. The plugin writes code values through its tone curve and sRGB, so nothing above white is lost here that it keeps. The plugin’s test hooks (its harness’s deliberately wrong models) are 0 here, as in a shipped instance.',
  'Nothing here is measured. The plugin’s harness, hztest, recovers Henyey–Greenstein’s phase function from the picture to 0.83%, the haze’s dimming as a Beer–Lambert slope equal to the light’s path to 5e-8, the lens as a disc of luminance I₀/πa² to 1.3e-6, the servo’s moves against their closed form to 7e-15 s and against an independent integrator, Sweep landing at 40 BPM and missing at 41, and the gobo sharpest at its focus, with 17 wrong models and 26 one-character mutants caught. That harness, not this page, is the reason to believe the light. The plugin itself has never been loaded into Resolume.',
];

//===========================================================================
// Mount.
//===========================================================================
const query = new URLSearchParams(window.location.search);
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';
const embed = query.has('embed') && query.get('embed') !== '0';

export const mounted = mountDemo({
  name: 'Haze',
  pluginId: 'HZ01 · HZ02',
  kind: ['source', 'effect'],
  tagline:
    'Moving heads in a hazed room. Each beam is only visible because the haze scatters a little of it toward you: ray-marched through stirred stage haze with Henyey–Greenstein scattering and Beer–Lambert extinction, carrying a gobo (or, in SW Haze Over, your clip) sharp only at its focus distance, on servo-limited pan and tilt that chase on the beat and arrive late when the tempo is too fast. None of it is drawn: faint from the side, blazing down the beam, spots blowing out before beams do.',
  repo: 'https://github.com/stoatworks-labs/haze',

  blurb:
    'It is Haze’s own four GLSL passes, ported from the repository to WebGL2 unedited and driven by a JavaScript port of the plugin’s CPU half — the servo planner and its sync, the chase and its patterns, the stage, the stir’s drift and the beat — which only a reader checks. There is no audio here, so Step On’s Onset never fires. SW Haze reads no video; SW Haze Over plays the rig in front of a generated clip, or carries it as the gobo.',

  // R16F, RGBA16F and RGBA32F render targets, as in the plugin.
  needFloat: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Haze (source)', hint: 'HZ01, FF_SOURCE: the rig in a hazed room, the gobo wheel’s gobo. Reads no clip.' },
      { id: 'over', name: 'SW Haze Over (effect)', hint: 'HZ02, FF_EFFECT: the same rig in front of the clip on the back wall (Screen), or carrying it as the gobo, or lighting it (Backdrop).' },
    ],
  },

  // For the Over only. The harness's own card first: `hztest --over` uses it.
  sources: ['look', 'scene', 'grid', 'bars', 'spot', 'ramp'],

  params: PARAMS,
  presets: PRESETS,
  differences: DIFFERENCES,
  createRenderer,
});

//---------------------------------------------------------------------------
// After the mount: which controls belong to which plugin, the host row, the
// line under the picture.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//---------------------------------------------------------------------------
if (mounted) {
  const params = mounted.params;
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (name) => [...document.querySelectorAll('.prow')]
    .find((row) => row.querySelector('.prow__name')?.textContent === name);

  function showForVariant(variant) {
    for (const p of PARAMS) {
      if (p.only) show(rowOf(p.name), p.only === variant);
    }
    const effect = variant === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const variant = mounted.state.variant;
    if (variant !== shownVariant) {
      shownVariant = variant;
      // A new instance: that constructor's defaults.
      params.reset();
    }
    if (!embed) showForVariant(variant);
  });

  // A link that opens on Audience opens paused: it flashes.
  if (!embed && OptionIndex(params.get('pattern'), Pattern.Count) === Pattern.Audience) {
    mounted.state.playing = false;
    const play = document.querySelector('.btn--play');
    if (play) play.textContent = 'Play';
  }

  if (!embed) {
    showForVariant(shownVariant);
    buildHostRow();
    buildStatusLine();
  }
}

//===========================================================================
// The host row: the stand-in for what Resolume would hand the plugin.
//===========================================================================
function field(label, select, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  wrap.append(span, select);
  return wrap;
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport hz-host';
  const tag = document.createElement('span');
  tag.className = 'hz-host__tag';
  tag.textContent = 'The host';
  tag.title = 'A stand-in for what Resolume would hand the plugin. Not a plugin control.';

  const tempo = document.createElement('select');
  tempo.className = 'transport__select';
  for (const bpm of TEMPOS) tempo.append(new Option(`${bpm} BPM`, String(bpm)));
  tempo.value = String(host.bpm);
  tempo.addEventListener('change', () => {
    host.bpm = Number(tempo.value);
    mounted.redraw();
  });

  row.append(
    tag,
    field('Tempo', tempo, 'Resolume’s BPM. The bar phase runs at it, in 4/4, on the page’s clock. Sweep swings each head about 127° of pan a step, whatever the Spread: at Beat it lands at 40 BPM and not at 41, as the plugin’s harness measures.'),
  );
  transport.after(row);
}

//===========================================================================
// The line under the picture. It reports; it measures nothing.
//===========================================================================
const STEP_BEATS = [0.25, 0.5, 1, 2, 4];

function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status hz-status';
  line.id = 'hz-status';
  line.setAttribute('aria-live', 'off');
  const caution = document.createElement('p');
  caution.className = 'stage__status hz-caution';
  caution.id = 'hz-caution';
  caution.hidden = true;
  stage.append(line, caution);

  setInterval(() => {
    const frame = current.frame;
    if (!frame) return;
    const p = mounted.params;
    const pattern = OptionIndex(f32(p.get('pattern')), Pattern.Count);
    const stepOn = OptionIndex(f32(p.get('stepOn')), StepOn.Count);
    const name = current.variant === 'over' ? 'SW Haze Over' : 'SW Haze';
    let chase;
    if (pattern === Pattern.Hold) chase = 'Hold: the chase does not step';
    else if (stepOn === StepOn.Onset) chase = 'Onset: no audio reaches this page, so only Step steps the chase';
    else if (stepOn === StepOn.StepOnly) chase = 'Step Only: press Step';
    else {
      const perSecond = host.bpm / 60 / STEP_BEATS[stepOn];
      chase = `${perSecond.toFixed(perSecond < 1 ? 2 : 1)} steps a second at ${host.bpm} BPM`;
    }
    const moving = frame.count > 0 ? `${frame.moving} of ${frame.count} heads still on their way` : '';
    const air = current.passes && !current.passes.floatLinear ? ' · air buffer RGBA16F (no OES_texture_float_linear here; the plugin’s is RGBA32F)' : '';
    line.textContent = `${name} · ${chase} · step ${frame.steps} · ${moving}${air}`;
    const audience = pattern === Pattern.Audience;
    caution.hidden = !audience;
    caution.textContent = audience
      ? 'Audience flashes: every other step the lenses look straight into the camera and the picture flares. At 120 BPM on Beat that is past three flashes a second in part of the picture; at 4 a Beat, past three over the whole of it. Pause stops it at once.'
      : '';
  }, 250);
}
