/**
 * Haze's CPU half, ported by hand to JavaScript. ONLY A READER CHECKS THIS.
 *
 *   Controls.cpp   every control's name, group, kind, option list and
 *                  default (kTable), HostOrder, OptionIndex and every 0..1
 *                  conversion, ColourOf
 *   Motion.cpp/.h  the stage (Place, the camera), the fixture (its limits,
 *                  Direction, the gobo's frame), the servo (Plan, Profile::At,
 *                  the vectorial sync's bisection), the yoke, the inverse
 *                  kinematics (Aim), and the chase (Lean, retarget, Advance)
 *   Haze.cpp       the CPU half of ProcessOpenGL: the frame's clock and dt,
 *                  the beat, the chase's inputs, the beams' I0, tan(alpha) and
 *                  the rest of FrameData, the stir's lattice offsets
 *                  (drawNoise), and the float arithmetic of sceneUniforms and
 *                  the passes' uniforms
 *
 * Numbers: JavaScript's are doubles, as the plugin's CPU state is (Motion.cpp
 * is double throughout). Where the C++ holds or computes a `float` -- the host's
 * parameters, the beat, every value handed to glUniform -- this rounds with
 * Math.fround at the same place. std::lround and std::round are half away from
 * zero and Math.round is not, so they are ported by hand. splitmix64 (Random's
 * lean) is 64-bit unsigned arithmetic, done here in BigInt.
 *
 * Not ported: the seconds-or-milliseconds vote on the host's clock (this host's
 * unit is known: seconds), the audio analyser (Audio.cpp; a browser has no
 * Resolume FFT, and with an empty spectrum the analyser never fires, so Onset
 * is false every frame here as in a layer with no audio), the About block, the
 * diagnostics, and the test hooks (0 in a shipped instance; the motion hooks'
 * branches are kept so the code reads as the C++ does, and never set).
 *
 * No DOM and no GL in this file, so node can import it for a crosscheck.
 */

export const f32 = Math.fround;

//===========================================================================
// Controls.h
//===========================================================================

export const kPi = 3.14159265358979323846;

export const PT = {
  FIXTURES: 0,
  LAYOUT: 1,
  ZOOM: 2,
  GOBO: 3,
  COLOUR: 4,
  FOCUS: 5,

  HAZE: 6,
  ANISOTROPY: 7,
  STIR: 8,

  PATTERN: 9,
  STEP_ON: 10,
  SPREAD: 11,
  MOTOR_SPEED: 12,
  STEP: 13,
  AUDIO: 14,

  ORBIT: 15,
  EXPOSURE: 16,
  QUALITY: 17,
  CLIP_AS: 18,
  MIX: 19,

  ABOUT_TEXT: 20,
};

export const Kind = { Standard: 0, Option: 1, Event: 2, Buffer: 3 };

export const Layout = { Line: 0, Arc: 1, Floor: 2, Split: 3, Count: 4 };
export const Gobo = { Open: 0, Dots: 1, Bars: 2, Star: 3, Ring: 4, Breakup: 5, Count: 6, Clip: 100 };
export const Colour = { White: 0, Red: 1, Amber: 2, Green: 3, Cyan: 4, Blue: 5, Magenta: 6, Count: 7 };
export const Pattern = { Fan: 0, Cross: 1, Sweep: 2, Audience: 3, Wave: 4, Random: 5, Hold: 6, Count: 7 };
export const StepOn = { FourABeat: 0, TwoABeat: 1, Beat: 2, TwoBeats: 3, Bar: 4, Onset: 5, StepOnly: 6, Count: 7 };
export const Quality = { High: 0, Medium: 1, Low: 2, Count: 3 };
export const ClipAs = { Gobo: 0, Backdrop: 1, Screen: 2, Count: 3 };

//===========================================================================
// Controls.cpp
//===========================================================================

/** std::clamp( v, lo, hi ): v < lo ? lo : hi < v ? hi : v, NaN passing through. */
const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
/** std::lround (and std::round): half away from zero. */
export const lround = (x) => (x < 0 ? -Math.round(-x) : Math.round(x));

const clamp01 = (v) => clamp(v, 0.0, 1.0);
const geometric = (v, low, high) => low * Math.pow(high / low, clamp01(v));
function inverseGeometric(value, low, high) {
  const lo = Math.min(low, high);
  const hi = Math.max(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}
const linear = (v, low, high) => low + (high - low) * clamp01(v);
const inverseLinear = (value, low, high) => f32(clamp((value - low) / (high - low), 0.0, 1.0));

const kFixturesHigh = 12;
// The Clay Paky Mythos: 2.5 degrees in its beam mode, 30.8 at the wide end of
// its fixed-gobo zoom (the manual's specifications, SPEC-haze.md).
const kZoomLow = 2.5;
const kZoomHigh = 30.8;
const kFocusLow = 2.0;
const kFocusHigh = 30.0;
const kHazeLow = 0.001;
const kHazeHigh = 0.1;
const kGHigh = 0.95;
const kSpreadLow = 5.0;
const kSpreadHigh = 90.0;
const kMotorLow = 0.05;
const kMotorHigh = 1.5;
const kOrbitHigh = 90.0;
const kStops = 4.0;

const kLayoutNames = ['Line', 'Arc', 'Floor', 'Split'];
const kGoboNames = ['Open', 'Dots', 'Bars', 'Star', 'Ring', 'Breakup'];
const kColourNames = ['White', 'Red', 'Amber', 'Green', 'Cyan', 'Blue', 'Magenta'];
const kPatternNames = ['Fan', 'Cross', 'Sweep', 'Audience', 'Wave', 'Random', 'Hold'];
const kStepOnNames = ['4 a Beat', '2 a Beat', 'Beat', '2 Beats', 'Bar', 'Onset', 'Step Only'];
const kQualityNames = ['High', 'Medium', 'Low'];
const kClipAsNames = ['Gobo', 'Backdrop', 'Screen'];

export function FixturesFromParam(v) {
  return clamp(lround(linear(v, 1.0, kFixturesHigh)), 1, kFixturesHigh);
}
export const ParamFromFixtures = (count) => inverseLinear(count, 1.0, kFixturesHigh);
export const ZoomFromParam = (v) => linear(v, kZoomLow, kZoomHigh);
export const ParamFromZoom = (degrees) => inverseLinear(degrees, kZoomLow, kZoomHigh);
export const FocusFromParam = (v) => geometric(v, kFocusLow, kFocusHigh);
export const ParamFromFocus = (metres) => inverseGeometric(metres, kFocusLow, kFocusHigh);
export const HazeFromParam = (v) => geometric(v, kHazeLow, kHazeHigh);
export const ParamFromHaze = (sigma) => inverseGeometric(sigma, kHazeLow, kHazeHigh);
export const AnisotropyFromParam = (v) => linear(v, 0.0, kGHigh);
export const ParamFromAnisotropy = (g) => inverseLinear(g, 0.0, kGHigh);
export const StirFromParam = (v) => clamp01(v);
export const SpreadFromParam = (v) => linear(v, kSpreadLow, kSpreadHigh);
export const ParamFromSpread = (degrees) => inverseLinear(degrees, kSpreadLow, kSpreadHigh);
export const MotorSpeedFromParam = (v) => geometric(v, kMotorLow, kMotorHigh);
export const ParamFromMotorSpeed = (fraction) => inverseGeometric(fraction, kMotorLow, kMotorHigh);
export const OrbitFromParam = (v) => linear(v, -kOrbitHigh, kOrbitHigh);
export const ParamFromOrbit = (degrees) => inverseLinear(degrees, -kOrbitHigh, kOrbitHigh);
export const ExposureFromParam = (v) => linear(v, -kStops, kStops);
export const ParamFromExposure = (stops) => inverseLinear(stops, -kStops, kStops);

/** Linear-sRGB filter colours: chosen, not measured (SPEC-haze.md). */
const kColourTable = [
  [1.0, 1.0, 1.0], // White
  [1.0, 0.02, 0.01], // Red
  [1.0, 0.45, 0.04], // Amber
  [0.05, 1.0, 0.08], // Green
  [0.04, 0.85, 1.0], // Cyan
  [0.03, 0.12, 1.0], // Blue
  [1.0, 0.04, 0.75], // Magenta
];
export function ColourOf(colour) {
  const i = clamp(colour | 0, 0, Colour.Count - 1);
  return kColourTable[i].slice();
}

const standard = (id, name, group, value) => ({ id, name, group, kind: Kind.Standard, defaultValue: value, options: null });
const option = (id, name, group, value, names) => ({ id, name, group, kind: Kind.Option, defaultValue: value, options: names });
const event = (id, name, group) => ({ id, name, group, kind: Kind.Event, defaultValue: 0.0, options: null });

// The defaults: six Mythos on a straight truss, 12 degree zoom, focused at
// 9 m, a heavy concert haze (0.015 /m, g 0.70, stirred), a fan chase on every
// beat at the motors' full speed, seen from front of house.
const kTable = [
  standard(PT.FIXTURES, 'Fixtures', 'Fixtures', ParamFromFixtures(6)),
  option(PT.LAYOUT, 'Layout', 'Fixtures', 0.0, kLayoutNames),
  standard(PT.ZOOM, 'Zoom', 'Fixtures', ParamFromZoom(12.0)),
  option(PT.GOBO, 'Gobo', 'Fixtures', 1.0, kGoboNames),
  option(PT.COLOUR, 'Colour', 'Fixtures', 0.0, kColourNames),
  standard(PT.FOCUS, 'Focus', 'Fixtures', ParamFromFocus(9.0)),

  standard(PT.HAZE, 'Haze', 'Haze', ParamFromHaze(0.015)),
  standard(PT.ANISOTROPY, 'Anisotropy', 'Haze', ParamFromAnisotropy(0.70)),
  standard(PT.STIR, 'Stir', 'Haze', f32(0.5)),

  option(PT.PATTERN, 'Pattern', 'Chase', 0.0, kPatternNames),
  option(PT.STEP_ON, 'Step On', 'Chase', 2.0, kStepOnNames),
  standard(PT.SPREAD, 'Spread', 'Chase', ParamFromSpread(30.0)),
  standard(PT.MOTOR_SPEED, 'Motor Speed', 'Chase', ParamFromMotorSpeed(1.0)),
  event(PT.STEP, 'Step', 'Chase'),
  { id: PT.AUDIO, name: 'Audio', group: 'Chase', kind: Kind.Buffer, defaultValue: 0.0, options: null },

  standard(PT.ORBIT, 'Orbit', 'View', ParamFromOrbit(0.0)),
  standard(PT.EXPOSURE, 'Exposure', 'View', ParamFromExposure(0.0)),
  option(PT.QUALITY, 'Quality', 'View', 1.0, kQualityNames),
  option(PT.CLIP_AS, 'Clip As', 'View', 2.0, kClipAsNames),
  standard(PT.MIX, 'Mix', 'View', 1.0),
];
if (kTable.length !== PT.ABOUT_TEXT) throw new Error('every control needs a row in the table');

export function InfoOf(id) {
  return kTable[Math.min(id, PT.ABOUT_TEXT - 1)];
}

/** Host index -> ParamId for one plugin: the source has no Clip As or Mix. The About run is not here. */
export function HostOrder(effect) {
  const order = [];
  for (let id = 0; id < PT.ABOUT_TEXT; id += 1) {
    const overOnly = id === PT.CLIP_AS || id === PT.MIX;
    if (!effect && overOnly) continue;
    order.push(id);
  }
  return order;
}

export function OptionIndex(value, count) {
  if (!Number.isFinite(value)) return 0;
  return clamp(lround(value), 0, count - 1);
}

//===========================================================================
// Motion.h / Motion.cpp: vectors
//===========================================================================

const kDeg = kPi / 180.0;

const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
export function normalise(v) {
  const l = Math.sqrt(dot(v, v));
  return l > 0.0 ? scale(v, 1.0 / l) : [0.0, 0.0, 0.0];
}

//===========================================================================
// The stage (metres; x to the audience's right, y up, z toward the audience).
//===========================================================================

export const stage = {
  kBackWall: -8.0,
  kBoxMin: [-20.0, 0.0, -8.0],
  kBoxMax: [20.0, 12.0, 20.0],
  kTrussY: 7.0,
  kTrussZ: -4.0,
  kFloorY: 0.4,
  kFloorZ: -7.0,
  kBackAlbedo: 0.5,
  kFloorAlbedo: 0.15,
  kEyeHeight: 1.7,
  kOrbitRadius: 13.0,
  kTarget: [0.0, 3.5, -4.0],
  kFieldOfView: 55.0,
  kScreenHeight: 9.0,
  kScreenBottom: 0.5,
  kMaxFixtures: 12,

  Place(layout, index, count) {
    count = Math.max(count, 1);
    const span = count > 1 ? Math.min(3.0, 16.0 / (count - 1)) : 0.0;
    const x = (index - 0.5 * (count - 1)) * span;
    const truss = { position: [x, stage.kTrussY, stage.kTrussZ], home: [0.0, -1.0, 0.0] };
    const deck = { position: [x, stage.kFloorY, stage.kFloorZ], home: [0.0, 1.0, 0.0] };
    switch (layout) {
      case Layout.Arc: {
        const bow = 1.0 - (x / 8.0) * (x / 8.0);
        return { position: [x, stage.kTrussY, stage.kTrussZ + 2.0 * bow], home: [0.0, -1.0, 0.0] };
      }
      case Layout.Floor: return deck;
      case Layout.Split: return (index % 2) === 0 ? truss : deck;
      default: return truss;
    }
  },

  CameraLooking(eye, target, tanHalfV) {
    const c = { eye, forward: null, right: null, up: null, tanHalfV };
    c.forward = normalise(sub(target, eye));
    c.right = normalise(cross(c.forward, [0.0, 1.0, 0.0]));
    if (dot(c.right, c.right) < 0.5) c.right = [1.0, 0.0, 0.0];
    // Right x up points at the viewer: a right-handed view.
    c.up = cross(c.right, c.forward);
    return c;
  },

  CameraAt(orbitDegrees) {
    const phi = orbitDegrees * kDeg;
    const eye = [stage.kOrbitRadius * Math.sin(phi), stage.kEyeHeight, stage.kTarget[2] + stage.kOrbitRadius * Math.cos(phi)];
    return stage.CameraLooking(eye, [stage.kTarget[0], stage.kTarget[1], stage.kTarget[2]], Math.tan(0.5 * stage.kFieldOfView * kDeg));
  },
};

//===========================================================================
// The fixture: the Clay Paky Mythos (manual p.30; Mythos2 datasheet).
//===========================================================================

export const fixture = {
  kLensDiameter: 0.157,
  kLumens: 24000.0,
  kPanRange: 540.0,
  kPanTime: 3.517,
  kTiltRange: 244.0,
  kTiltTime: 2.180,
  kPanLimit: 270.0,
  kTiltLimit: 122.0,
  kRampFraction: 0.25,

  PanLimits(speed = 1.0) {
    const v = fixture.kPanRange / ((1.0 - fixture.kRampFraction) * fixture.kPanTime);
    const a = v / (fixture.kRampFraction * fixture.kPanTime);
    return { v: v * speed, a: a * speed * speed };
  },
  TiltLimits(speed = 1.0) {
    const v = fixture.kTiltRange / ((1.0 - fixture.kRampFraction) * fixture.kTiltTime);
    const a = v / (fixture.kRampFraction * fixture.kTiltTime);
    return { v: v * speed, a: a * speed * speed };
  },

  /** The beam's direction at ( pan, tilt ), degrees. */
  Direction(home, pan, tilt) {
    const p = pan * kDeg;
    const t = tilt * kDeg;
    const lean = [Math.sin(p), 0.0, Math.cos(p)];
    return normalise(add(scale(home, Math.cos(t)), scale(lean, Math.sin(t))));
  },

  /** { right, up } for a bare direction; `fallback` stands in for up near vertical. */
  GoboFrameOf(d, fallback) {
    const world = [0.0, 1.0, 0.0];
    const square = sub(world, scale(d, dot(world, d)));
    let up;
    if (Math.sqrt(dot(square, square)) >= Math.sin(3.0 * kDeg)) up = normalise(square);
    else up = normalise(sub(fallback, scale(d, dot(fallback, d))));
    return { right: cross(d, up), up };
  },

  /** { d, right, up }: near vertical, the yoke's tilt direction stands in for up. */
  GoboFrame(home, pan, tilt) {
    const d = fixture.Direction(home, pan, tilt);
    const p = pan * kDeg;
    const t = tilt * kDeg;
    const lean = [Math.sin(p), 0.0, Math.cos(p)];
    const { right, up } = fixture.GoboFrameOf(d, add(scale(home, -Math.sin(t)), scale(lean, Math.cos(t))));
    return { d, right, up };
  },
};

//===========================================================================
// One axis: the time-optimal plan from ( x0, v0 ) to ( target, 0 ).
//===========================================================================

export class Profile {
  constructor() {
    this.x0 = 0.0;
    this.v0 = 0.0;
    this.target = 0.0;
    this.seg = [{ duration: 0.0, accel: 0.0 }, { duration: 0.0, accel: 0.0 }, { duration: 0.0, accel: 0.0 }];
  }

  Duration() {
    return this.seg[0].duration + this.seg[1].duration + this.seg[2].duration;
  }

  /** [ x, v ] tau seconds after the plan began (held at the target after). */
  At(tau) {
    if (tau >= this.Duration()) return [this.target, 0.0];
    let x = this.x0;
    let v = this.v0;
    tau = Math.max(tau, 0.0);
    for (const s of this.seg) {
      const dt = Math.min(tau, s.duration);
      x += v * dt + 0.5 * s.accel * dt * dt;
      v += s.accel * dt;
      tau -= dt;
      if (tau <= 0.0) break;
    }
    return [x, v];
  }
}

export function Plan(x0, v0, target, vmax, amax) {
  const p = new Profile();
  p.x0 = x0;
  p.v0 = v0;
  p.target = target;
  const d = target - x0;
  if (!(vmax > 0.0) || !(amax > 0.0) || (Math.abs(d) < 1e-12 && Math.abs(v0) < 1e-12)) {
    p.x0 = target;
    p.v0 = 0.0;
    return p;
  }
  // Which way: past where stopping now would leave it, or short of it.
  const stop = v0 * Math.abs(v0) / (2.0 * amax);
  const sigma = d - stop >= 0.0 ? 1.0 : -1.0;
  const u0 = sigma * v0;
  const D = sigma * d;
  let t1 = 0.0;
  let t2 = 0.0;
  let t3 = 0.0;
  let a1 = sigma * amax;
  if (u0 > vmax) {
    // Over speed (Motor Speed turned down mid-move): slow to vmax first.
    t1 = (u0 - vmax) / amax;
    a1 = -sigma * amax;
    const d1 = (u0 * u0 - vmax * vmax) / (2.0 * amax);
    const d3 = vmax * vmax / (2.0 * amax);
    t2 = Math.max(D - d1 - d3, 0.0) / vmax;
    t3 = vmax / amax;
  } else {
    const peak = Math.sqrt(Math.max(amax * D + 0.5 * u0 * u0, 0.0));
    if (peak <= vmax) {
      t1 = Math.max(peak - u0, 0.0) / amax;
      t3 = peak / amax;
    } else {
      t1 = (vmax - u0) / amax;
      const d1 = (vmax * vmax - u0 * u0) / (2.0 * amax);
      const d3 = vmax * vmax / (2.0 * amax);
      t2 = Math.max(D - d1 - d3, 0.0) / vmax;
      t3 = vmax / amax;
    }
  }
  p.seg[0] = { duration: t1, accel: a1 };
  p.seg[1] = { duration: t2, accel: 0.0 };
  p.seg[2] = { duration: t3, accel: -sigma * amax };
  return p;
}

/** Rest-to-rest time for a distance under ( v, a ). */
function restTime(distance, l) {
  distance = Math.abs(distance);
  if (distance <= 0.0) return 0.0;
  return distance >= l.v * l.v / l.a ? distance / l.v + l.v / l.a : 2.0 * Math.sqrt(distance / l.a);
}

/** The plan that takes `duration`, the axis's limits scaled to ( v k, a k^2 ): bisection on k. */
function stretched(x0, v0, target, l, duration) {
  let lo = 1e-4;
  let hi = 1.0;
  if (Plan(x0, v0, target, l.v * lo, l.a * lo * lo).Duration() <= duration) return Plan(x0, v0, target, l.v * lo, l.a * lo * lo);
  for (let i = 0; i < 100 && hi - lo > 1e-15; i += 1) {
    const k = 0.5 * (lo + hi);
    if (Plan(x0, v0, target, l.v * k, l.a * k * k).Duration() > duration) lo = k;
    else hi = k;
  }
  return Plan(x0, v0, target, l.v * hi, l.a * hi * hi);
}

/** Hooks for the negative controls. A shipped instance sets 0, and so does this page. */
export const MotionHook = { UnboundedAccel: 1 << 0, NoSync: 1 << 1, StepAtFrameStart: 1 << 2 };

/** Pan and tilt, synced. */
export class Yoke {
  constructor() {
    this.panPlan = new Profile();
    this.tiltPlan = new Profile();
    this.start = 0.0;
  }

  Reset(pan, tilt) {
    this.panPlan = Plan(pan, 0.0, pan, 1.0, 1.0);
    this.tiltPlan = Plan(tilt, 0.0, tilt, 1.0, 1.0);
    this.start = 0.0;
  }

  Retarget(at, pan, tilt, panLimits, tiltLimits, hooks) {
    const [p, t, pv, tv] = this.At(at);
    this.start = at;
    const lp = { ...panLimits };
    const lt = { ...tiltLimits };
    if ((hooks & MotionHook.UnboundedAccel) !== 0) {
      lp.a = 1e9;
      lt.a = 1e9;
    }
    this.panPlan = Plan(p, pv, pan, lp.v, lp.a);
    this.tiltPlan = Plan(t, tv, tilt, lt.v, lt.a);
    if ((hooks & MotionHook.NoSync) !== 0) return;
    const tp = this.panPlan.Duration();
    const tt = this.tiltPlan.Duration();
    if (tp < tt) this.panPlan = stretched(p, pv, pan, lp, tt);
    else if (tt < tp) this.tiltPlan = stretched(t, tv, tilt, lt, tp);
  }

  /** [ pan, tilt, panRate, tiltRate ] at host time t. */
  At(t) {
    const [pan, pv] = this.panPlan.At(t - this.start);
    const [tilt, tv] = this.tiltPlan.At(t - this.start);
    return [pan, tilt, pv, tv];
  }

  ArrivalTime() {
    return this.start + Math.max(this.panPlan.Duration(), this.tiltPlan.Duration());
  }

  TargetPan() { return this.panPlan.target; }
  TargetTilt() { return this.tiltPlan.target; }
}

/** [ pan, tilt ] for a direction: the solution with the shortest synced move from ( pan0, tilt0 ). */
export function Aim(home, direction, pan0, tilt0) {
  const u = normalise(direction);
  const t = Math.acos(clamp(dot(u, home), -1.0, 1.0)) / kDeg;
  const across = u[0];
  const out = u[2];
  const vertical = Math.hypot(across, out) < 1e-12;
  const p = vertical ? pan0 : Math.atan2(across, out) / kDeg;
  const lp = fixture.PanLimits();
  const lt = fixture.TiltLimits();
  let best = 1e300;
  let bestPan = pan0;
  let bestTilt = tilt0;
  let bestDp = 1e300;
  for (let sign = 0; sign < 2; sign += 1) {
    const tc = clamp(sign === 0 ? t : -t, -fixture.kTiltLimit, fixture.kTiltLimit);
    const pc0 = sign === 0 ? p : p + 180.0;
    for (let k = -2; k <= 2; k += 1) {
      const pc = vertical ? pan0 : pc0 + 360.0 * k;
      if (Math.abs(pc) > fixture.kPanLimit + 1e-9) continue;
      const cost = Math.max(restTime(pc - pan0, lp), restTime(tc - tilt0, lt));
      const dp = Math.abs(pc - pan0);
      if (cost < best - 1e-12 || (cost < best + 1e-12 && dp < bestDp)) {
        best = cost;
        bestDp = dp;
        bestPan = pc;
        bestTilt = tc;
      }
    }
  }
  return [bestPan, bestTilt];
}

//===========================================================================
// The chase.
//===========================================================================

const M64 = (1n << 64n) - 1n;
/** splitmix64: a step's random lean, the same on every machine. */
function mix64(z) {
  z = (z + 0x9E3779B97F4A7C15n) & M64;
  z = ((z ^ (z >> 30n)) * 0xBF58476D1CE4E5B9n) & M64;
  z = ((z ^ (z >> 27n)) * 0x94D049BB133111EBn) & M64;
  return z ^ (z >> 31n);
}
const unit = (h) => Number(h >> 11n) * (1.0 / 9007199254740992.0);

/** The lean [ across, toward the audience ], degrees, step k gives fixture i of n. */
export function Lean(pattern, k, i, n, W) {
  const u = n > 1 ? 2.0 * i / (n - 1) - 1.0 : 0.0;
  const even = (k % 2) === 0;
  switch (pattern) {
    case Pattern.Fan: return [even ? u * W : 0.0, 0.3 * W];
    case Pattern.Cross: return [even ? u * W : -u * W, 0.5 * W];
    case Pattern.Sweep: return [even ? W : -W, 0.5 * W];
    // The odd steps aim at the camera; Chase does that, not the lean.
    case Pattern.Audience: return [0.5 * u * W, W];
    case Pattern.Wave: return [0.5 * u * W, 0.5 * W * (1.0 + Math.sin(2.0 * kPi * ((k % 8) / 8.0 + 0.5 * u)))];
    case Pattern.Random: {
      const h = mix64((BigInt(k) * 16n + BigInt(i)) & M64);
      const r = W * Math.sqrt(unit(h));
      const ang = 2.0 * kPi * unit(mix64(h));
      return [r * Math.sin(ang), r * Math.cos(ang)];
    }
    default: return [0.5 * u * W, 0.5 * W];
  }
}

export class Chase {
  static kMaxStepsPerFrame = 8;

  constructor() {
    this.yokes = [];
    this.count = 0;
    this.steps = 0;
    this.lastPhase = -1.0;
    this.frameEnd = 0.0;
    this.lastSpeed = -1.0;
    this.started = false;
    this.hooks = 0;
    this.stepTimes = [];
  }

  Count() { return this.count; }
  YokeOf(i) { return this.yokes[i]; }
  Steps() { return this.steps; }

  /** [ pan, tilt ] at the end of the last frame. */
  Pose(i) {
    if (i < 0 || i >= this.count) return [0.0, 0.0];
    const [pan, tilt] = this.yokes[i].At(this.frameEnd);
    return [pan, tilt];
  }

  retarget(at, input, initial) {
    const lp = fixture.PanLimits(input.speed);
    const lt = fixture.TiltLimits(input.speed);
    const speedChanged = this.lastSpeed >= 0.0 && input.speed !== this.lastSpeed;
    for (let i = 0; i < this.count; i += 1) {
      const y = this.yokes[i];
      const place = stage.Place(input.layout, i, this.count);
      let pan = 0.0;
      let tilt = 0.0;
      if (input.pattern === Pattern.Audience && (this.steps % 2) === 1) {
        [pan, tilt] = Aim(place.home, normalise(sub(input.camera, place.position)), y.TargetPan(), y.TargetTilt());
      } else {
        // A console's chase stores pan and tilt, not directions: pan at the
        // turn nearest the last target that stays inside the yoke's range.
        const [across, out] = Lean(input.pattern, this.steps, i, this.count, input.spread);
        tilt = Math.min(Math.hypot(across, out), fixture.kTiltLimit);
        pan = Math.atan2(across, out) / kDeg;
        pan += 360.0 * lround((y.TargetPan() - pan) / 360.0);
        while (pan > fixture.kPanLimit) pan -= 360.0;
        while (pan < -fixture.kPanLimit) pan += 360.0;
      }
      if (initial) y.Reset(pan, tilt);
      else if (speedChanged || Math.abs(pan - y.TargetPan()) > 1e-9 || Math.abs(tilt - y.TargetTilt()) > 1e-9) {
        y.Retarget(at, pan, tilt, lp, lt, this.hooks);
      }
    }
  }

  /** One host frame [ now - dt, now ]: steps whatever is due inside it, at the instant it is due. */
  Advance(now, dt, input) {
    const frameStart = dt > 0.0 ? now - dt : now;
    this.stepTimes = [];

    const want = clamp(input.count, 1, stage.kMaxFixtures);
    if (want !== this.count) {
      // New fixtures start at home and move to their places.
      if (this.yokes.length > want) this.yokes.length = want;
      while (this.yokes.length < want) {
        const y = new Yoke();
        y.Reset(0.0, 0.0);
        this.yokes.push(y);
      }
      this.count = want;
    }
    if (!this.started) {
      // A fresh instance starts with every beam already at the first step's place.
      this.retarget(frameStart, input, true);
      this.started = true;
    } else this.retarget(frameStart, input, false);
    this.lastSpeed = input.speed;

    const due = [];
    for (let i = 0; i < input.presses; i += 1) due.push(frameStart);
    if (input.stepOn === StepOn.Onset && input.onset) due.push(frameStart);

    const tempo = input.bpm > 1.0 ? input.bpm : 120.0;
    const phaseNow = input.hostBeat ? clamp(input.barPhase, 0.0, 1.0) : (now * tempo / 240.0) % 1.0;
    let grid = 0.0;
    switch (input.stepOn) {
      case StepOn.FourABeat: grid = 1.0 / 16.0; break;
      case StepOn.TwoABeat: grid = 1.0 / 8.0; break;
      case StepOn.Beat: grid = 0.25; break;
      case StepOn.TwoBeats: grid = 0.5; break;
      case StepOn.Bar: grid = 1.0; break;
      default: break;
    }
    if (grid > 0.0 && this.lastPhase >= 0.0 && dt > 0.0) {
      const p0 = this.lastPhase;
      let p1 = phaseNow;
      if (p1 < p0) p1 += 1.0;
      if (p1 > p0) {
        let boundary = (Math.floor(p0 / grid + 1e-9) + 1.0) * grid;
        for (let n = 0; boundary <= p1 + 1e-12 && n < Chase.kMaxStepsPerFrame; n += 1, boundary += grid) {
          const inside = (this.hooks & MotionHook.StepAtFrameStart) !== 0 ? 0.0 : (boundary - p0) / (p1 - p0);
          due.push(frameStart + inside * dt);
        }
      }
    }
    this.lastPhase = phaseNow;

    due.sort((a, b) => a - b);
    if (input.pattern !== Pattern.Hold) {
      for (const at of due) {
        this.steps += 1;
        this.stepTimes.push(at);
        this.retarget(at, input, false);
      }
    }
    this.frameEnd = now;
  }
}

//===========================================================================
// Haze.cpp: the CPU half of ProcessOpenGL, and the arithmetic of the uniforms.
//===========================================================================

/** The exposure's reference: radiance is written in units of 20 cd/m^2. */
export const kReferenceLuminance = 20.0;
/** Host seconds a single frame may advance by. */
const kMaxFrameDelta = 0.1;
/** The stir: drift across the stage, the evolution's time constant, the octaves' cells. */
const kDrift = 0.25;
const kStirTime = 12.0;
const kCells = [3.0, 1.5, 0.75];
const kLatticePeriod = 64.0;

/** Shaders.h */
export const kMaxFixtures = 12;
export const kGoboSize = 512;
export const kAtlasW = 512;
export const kAtlasH = 256;

function positiveMod(x, period) {
  const m = x % period; // std::fmod: the same exact remainder
  return m < 0.0 ? m + period : m;
}

/**
 * One plugin instance's CPU state: `new Haze( false )` is SW Haze (HZ01),
 * `new Haze( true )` SW Haze Over (HZ02).
 */
export class Haze {
  constructor(isEffect) {
    this.isEffect = isEffect;
    this.hostOrder = HostOrder(isEffect);
    this.declared = new Set(this.hostOrder);
    this.params = new Array(PT.ABOUT_TEXT).fill(0);
    for (let id = 0; id < PT.ABOUT_TEXT; id += 1) this.params[id] = InfoOf(id).defaultValue;

    this.chase = new Chase();
    this.stirTime = 0.0;
    this.stirOffsets = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];

    this.now = 0.0;
    this.lastNow = -1.0;

    this.bpm = f32(120.0);
    this.barPhase = f32(0.0);
    this.lastBarPhase = f32(-1.0);
    this.hostBeatSeen = false;

    this.stepPresses = 0;
    this.stepHeld = false;
  }

  /** SetById: the value a host sends (a float) to a control by ParamId; one this plugin does not declare is ignored. */
  SetById(id, value) {
    if (!this.declared.has(id)) return;
    value = f32(value);
    // Step acts on the press, once.
    if (id === PT.STEP) {
      const down = value >= 0.5;
      if (down && !this.stepHeld) this.stepPresses += 1;
      this.stepHeld = down;
    }
    this.params[id] = value;
  }

  GetById(id) { return this.params[id]; }

  SetBeatInfo(newBpm, newBarPhase) {
    newBpm = f32(newBpm);
    newBarPhase = f32(newBarPhase);
    if (newBpm > 0.0 && Number.isFinite(newBpm)) this.bpm = newBpm;
    if (Number.isFinite(newBarPhase)) this.barPhase = f32(newBarPhase - Math.floor(newBarPhase));
  }

  /**
   * One frame at host time `now` (seconds) on a `width` x `height` output:
   * everything ProcessOpenGL computes before it binds anything, and the values
   * its passes hand to glUniform, as floats.
   */
  ProcessFrame(now, width, height) {
    const P = this.params;
    this.now = now;
    const dt = this.lastNow >= 0.0 ? clamp(now - this.lastNow, 0.0, kMaxFrameDelta) : 0.0;
    this.lastNow = now;
    // UpdateAudio( dt ): no spectrum reaches a page, so the analyser never fires.
    const onset = false;
    if (this.barPhase !== this.lastBarPhase && this.lastBarPhase >= 0.0) this.hostBeatSeen = true;
    this.lastBarPhase = this.barPhase;
    this.stirTime += dt;

    // The camera, the chase, the yokes.
    const camera = stage.CameraAt(OrbitFromParam(P[PT.ORBIT]));
    const input = {
      pattern: OptionIndex(P[PT.PATTERN], Pattern.Count),
      stepOn: OptionIndex(P[PT.STEP_ON], StepOn.Count),
      presses: this.stepPresses,
      onset,
      bpm: this.bpm,
      barPhase: this.barPhase,
      hostBeat: this.hostBeatSeen,
      spread: SpreadFromParam(P[PT.SPREAD]),
      speed: MotorSpeedFromParam(P[PT.MOTOR_SPEED]),
      count: FixturesFromParam(P[PT.FIXTURES]),
      layout: OptionIndex(P[PT.LAYOUT], Layout.Count),
      camera: camera.eye,
    };
    this.stepPresses = 0;
    this.chase.Advance(now, dt, input);

    const alpha = 0.5 * ZoomFromParam(P[PT.ZOOM]) * kPi / 180.0;
    const frame = {
      camera,
      count: 0,
      position: [],
      direction: [],
      right: [],
      up: [],
      pose: [],
      tanAlpha: Math.tan(alpha),
      intensity: fixture.kLumens / (2.0 * kPi * (1.0 - Math.cos(alpha))),
      focus: FocusFromParam(P[PT.FOCUS]),
      sigma: HazeFromParam(P[PT.HAZE]),
      g: AnisotropyFromParam(P[PT.ANISOTROPY]),
      stir: StirFromParam(P[PT.STIR]),
      gain: Math.pow(2.0, ExposureFromParam(P[PT.EXPOSURE])),
      airScale: 1,
      samples: 0,
    };
    const quality = OptionIndex(P[PT.QUALITY], Quality.Count);
    frame.airScale = quality === Quality.High ? 1 : quality === Quality.Medium ? 2 : 4;
    frame.samples = quality === Quality.High ? 32 : quality === Quality.Medium ? 24 : 16;

    frame.count = this.chase.Count();
    for (let i = 0; i < frame.count; i += 1) {
      const place = stage.Place(input.layout, i, frame.count);
      const [pan, tilt] = this.chase.Pose(i);
      frame.pose.push([pan, tilt]);
      frame.position.push(place.position);
      const { d, right, up } = fixture.GoboFrame(place.home, pan, tilt);
      frame.direction.push(d);
      frame.right.push(right);
      frame.up.push(up);
    }

    // drawNoise: the drift and the evolution as lattice offsets, reduced in double.
    for (let o = 0; o < 3; o += 1) {
      this.stirOffsets[o][0] = positiveMod(-kDrift * this.stirTime / kCells[o], kLatticePeriod);
      this.stirOffsets[o][1] = 17.0 * (o + 1);
      this.stirOffsets[o][2] = 29.0 * (o + 1);
      this.stirOffsets[o][3] = positiveMod(this.stirTime / kStirTime + 13.0 * o, kLatticePeriod);
    }
    const offsets = new Float32Array(12);
    for (let o = 0; o < 3; o += 1) for (let k = 0; k < 4; k += 1) offsets[o * 4 + k] = this.stirOffsets[o][k];

    // Which gobo: the wheel's, or (the Over in Clip As Gobo) the clip.
    let goboKind = OptionIndex(P[PT.GOBO], Gobo.Count);
    const clipAs = OptionIndex(P[PT.CLIP_AS], ClipAs.Count);
    if (this.isEffect && clipAs === ClipAs.Gobo) goboKind = Gobo.Clip;

    // sceneUniforms, as floats.
    const pos = new Float32Array(36);
    const dir = new Float32Array(36);
    const rgt = new Float32Array(36);
    const upv = new Float32Array(36);
    const i0 = new Float32Array(36);
    const rgb = ColourOf(OptionIndex(P[PT.COLOUR], Colour.Count));
    for (let i = 0; i < frame.count; i += 1) {
      for (let k = 0; k < 3; k += 1) {
        pos[i * 3 + k] = frame.position[i][k];
        dir[i * 3 + k] = frame.direction[i][k];
        rgt[i * 3 + k] = frame.right[i][k];
        upv[i * 3 + k] = frame.up[i][k];
        i0[i * 3 + k] = frame.intensity * rgb[k] / kReferenceLuminance;
      }
    }
    const v3 = (v) => [f32(v[0]), f32(v[1]), f32(v[2])];
    const tanV = camera.tanHalfV;
    const airW = Math.floor((width + frame.airScale - 1) / frame.airScale);
    const airH = Math.floor((height + frame.airScale - 1) / frame.airScale);
    frame.uniforms = {
      scene: {
        Eye: v3(camera.eye),
        Forward: v3(camera.forward),
        Right: v3(camera.right),
        Up: v3(camera.up),
        Count: frame.count,
        FixPos: pos,
        FixDir: dir,
        FixRight: rgt,
        FixUp: upv,
        FixI0: i0,
        TanAlpha: f32(frame.tanAlpha),
        Focus: f32(frame.focus),
        LensRadius: f32(0.5 * fixture.kLensDiameter),
        Sigma: f32(frame.sigma),
        TanHalf: [f32(tanV * width / height), f32(tanV)],
      },
      noise: { Offset: offsets },
      gobo: { GoboKind: goboKind },
      air: {
        G: f32(frame.g),
        Stir: f32(frame.stir),
        Samples: frame.samples,
        AirScale: frame.airScale,
        FullSize: [width, height],
        size: [airW, airH],
      },
      final: {
        IsOver: this.isEffect ? 1 : 0,
        ClipAs: this.isEffect ? clipAs : 0,
        MixAmount: this.isEffect ? clamp(P[PT.MIX], 0.0, 1.0) : 1.0,
        Gain: f32(frame.gain),
        AirScale: frame.airScale,
        AirSize: [airW, airH],
        BackAlbedo: f32(stage.kBackAlbedo),
        FloorAlbedo: f32(stage.kFloorAlbedo),
        ScreenHeight: f32(stage.kScreenHeight),
        ScreenBottom: f32(stage.kScreenBottom),
      },
    };
    frame.goboKind = goboKind;
    frame.offsets = offsets;
    frame.steps = this.chase.Steps();
    frame.stepTimes = this.chase.stepTimes.slice();
    let moving = 0;
    for (let i = 0; i < frame.count; i += 1) if (this.chase.YokeOf(i).ArrivalTime() > now) moving += 1;
    frame.moving = moving;
    return frame;
  }
}
