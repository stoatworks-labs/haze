#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run at
# CI's raster, under a time limit (a mutant can hang: honeydew's lesson; a
# timeout counts as caught).
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The scratch tree goes under TMPDIR when it is set (a heredoc into python
# hung once with the system's /var/folders TMPDIR: the fleet's trap).
WORK="$(mktemp -d "${TMPDIR:-/tmp}/hzmutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Shaders.cpp|float d = 1.0 + G * G - 2.0 * G * c;|float d = 1.0 + G * G + 2.0 * G * c;|--phase|GLSL: Henyey-Greenstein's denominator with + 2 g cos, the peak thrown backward, - -> +"
	"source/Shaders.cpp|return ( 1.0 - G * G ) / ( 4.0 * kPi * d * sqrt( d ) );|return ( 1.0 + G * G ) / ( 4.0 * kPi * d * sqrt( d ) );|--phase|GLSL: Henyey-Greenstein's normalisation 1 + g^2, - -> +"
	"source/Shaders.cpp|float weight = span / n * h * ( 1.0 + s * s );|float weight = span / n * h * ( 1.0 - s * s );|--phase|GLSL: the equiangular sampler's Jacobian 1 - tan^2, + -> -"
	"source/Shaders.cpp|return exp( -Sigma * ( l + e ) );|return exp( -Sigma * ( l - e ) );|--extinction|GLSL: the eye's path subtracted from the light's, + -> -"
	"source/Shaders.cpp|float cosI  = -dot( nrm, v ) / dist;|float cosI  = +dot( nrm, v ) / dist;|--extinction|GLSL: a surface's cosine with the wrong sign, - -> +"
	"source/Shaders.cpp|sum += FixI0[ i ] * K * ( transmit( 0.0, tl ) / ( kPi * LensRadius * LensRadius ) );|sum += FixI0[ i ] * K * ( transmit( 0.0, tl ) / ( kPi * LensRadius + LensRadius ) );|--lens|GLSL: the lens's area pi a + a, * -> +"
	"source/Shaders.cpp|2.0 * LensRadius * abs( 1.0 / r - 1.0 / Focus );|2.0 * LensRadius * abs( 1.0 / r + 1.0 / Focus );|--focus|GLSL: the defocus from 1/r + 1/f, - -> +"
	"source/Shaders.cpp|float rad = sqrt( ( float( k ) + 0.5 ) / 16.0 ) * 0.5 * texels / 512.0;|float rad = sqrt( ( float( k ) + 0.5 ) / 16.0 ) * 0.6 * texels / 512.0;|--focus|GLSL: the surface's blur disc 20% too wide (sharp at the focus still), 5 -> 6"
	"source/Shaders.cpp|return vec2( 0.5 ) + g / ( 4.0 * TanAlpha );|return vec2( 0.6 ) + g / ( 4.0 * TanAlpha );|--aim|GLSL: the gobo's centre off the beam's axis, 5 -> 6"
	"source/Shaders.cpp|vec2 g = vec2( dot( v, FixRight[ i ] ), dot( v, FixUp[ i ] ) ) / r;|vec2 g = vec2(-dot( v, FixRight[ i ] ), dot( v, FixUp[ i ] ) ) / r;|--gobo|GLSL: the gobo mirrored left to right, ' ' -> -"
	"source/Shaders.cpp|float local  = Sigma * max( 1.0 + Stir * stirAt( x + shift ), 0.0 );|float local  = Sigma * max( 1.0 - Stir * stirAt( x + shift ), 0.0 );|--density|GLSL: the beam thick where the haze is thin, + -> -"
	"source/Shaders.cpp|vec2 o1  = vec2( mod( z1, 8.0 ) * 64.0, floor( z1 / 8.0 ) * 32.0 );|vec2 o1  = vec2( mod( z0, 8.0 ) * 64.0, floor( z1 / 8.0 ) * 32.0 );|--density|GLSL: the haze's upper slice read from the wrong tile, 1 -> 0"
	"source/Shaders.cpp|L += -log( vec3( 1.0 ) - min( lit, vec3( 0.98 ) ) ) / Gain * transmit( 0.0, tFar );|L += -log( vec3( 1.0 ) - min( lit, vec3( 0.98 ) ) ) / Gain * transmit( 1.0, tFar );|--alpha|GLSL: the screen's light through a metre of haze it does not cross, 0 -> 1"
	"source/Shaders.cpp|col.a   = hooked( kHookAlphaThrough ) ? clip.a : mix( clip.a, 1.0, MixAmount );|col.a   = hooked( kHookAlphaThrough ) ? clip.a : mix( clip.a, 0.0, MixAmount );|--alpha|GLSL: the Over's alpha mixed toward clear, 1 -> 0"
	"source/Motion.cpp|if( peak <= vmax )|if( peak >= vmax )|--servo|C++: the servo cruising when it never reaches full speed, < -> >"
	"source/Motion.cpp|const double peak = std::sqrt( std::max( amax * D + 0.5 * u0 * u0, 0.0 ) );|const double peak = std::sqrt( std::max( amax * D - 0.5 * u0 * u0, 0.0 ) );|--servo|C++: a moving yoke's peak speed forgetting its head start, + -> -"
	"source/Motion.cpp|const double stop  = v0 * std::fabs( v0 ) / ( 2.0 * amax );|const double stop  = v0 * std::fabs( v0 ) / ( 3.0 * amax );|--servo|C++: the stopping distance v^2 / 3a, 2 -> 3"
	"source/Motion.cpp|if( Plan( x0, v0, target, l.v * k, l.a * k * k ).Duration() > duration )|if( Plan( x0, v0, target, l.v * k, l.a * k / k ).Duration() > duration )|--servo|C++: the sync's bisection on unscaled acceleration, * -> /"
	"source/Motion.cpp|const double a = v / ( kRampFraction * kPanTime );|const double a = v / ( kRampFraction + kPanTime );|--servo|C++: pan's acceleration from the ramp plus the time, * -> +"
	"source/Motion.h|kPanTime = 3.517;|kPanTime = 3.617;|--servo|C++: the Mythos's pan time misread from the manual, 5 -> 6"
	"source/Motion.cpp|const double pc0 = sign == 0 ? p : p + 180.0;|const double pc0 = sign == 0 ? p : p + 170.0;|--servo|C++: the yoke's over-the-top solution at p + 170, 8 -> 7"
	"source/Motion.cpp|( boundary - p0 ) / ( p1 - p0 )|( boundary + p0 ) / ( p1 - p0 )|--chase|C++: the beat's place in its frame from the sum of phases, - -> +"
	"source/Motion.cpp|across = even ? W : -W;|across = even ? W : +W;|--late|C++: Sweep never swinging back, - -> +"
	"source/Audio.cpp|			flux = 0.0f;|			flux = 1.0f;|--audio|C++: the first frame primed with a flux of 1, not 0, 0 -> 1"
	"source/Haze.cpp|positiveMod( -kDrift * stirTime / kCells[ o ], kLatticePeriod )|positiveMod( +kDrift * stirTime / kCells[ o ], kLatticePeriod )|--clock|C++: the haze drifting up instead of settling, - -> +"
	"source/Haze.cpp|frame.intensity    = fixture::kLumens / ( 2.0 * kPi * ( 1.0 - std::cos( alpha ) ) );|frame.intensity    = fixture::kLumens / ( 2.0 * kPi * ( 2.0 - std::cos( alpha ) ) );|--lens|C++: the beam's solid angle 2 pi ( 2 - cos ), 1 -> 2"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target hztest -j6 >/dev/null 2>&1
	status=0
	limit 300 "$tree/build/hztest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
