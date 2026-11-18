// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
const float kPi = 3.14159265358979;
const vec3 kBoxMin = vec3( -20.0, 0.0, -8.0 );
const vec3 kBoxMax = vec3( 20.0, 12.0, 20.0 );
const int kMaxFixtures = 12;

const int kHookRaw          = 1;
const int kHookAirOnly      = 2;
const int kHookSurfacesOnly = 4;
const int kHookIsotropic    = 8;
const int kHookBackward     = 16;
const int kHookNoLightExt   = 32;
const int kHookNoEyeExt     = 64;
const int kHookNoFocus      = 128;
const int kHookNoInvSquare  = 256;
const int kHookMirror       = 512;
const int kHookStirPerBeam  = 1024;
const int kHookAlphaThrough = 2048;

uniform int Hooks;

bool hooked( int bit )
{
	return ( Hooks & bit ) != 0;
}

//The PCG output mix: integer, so every GPU draws the same noise.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

//sRGB, both ways: light adds and multiplies in linear, the host sees code.
vec3 toLinear( vec3 code )
{
	vec3 c = max( code, vec3( 0.0 ) );
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 light )
{
	vec3 l = max( light, vec3( 0.0 ) );
	return mix( l * 12.92, 1.055 * pow( l, vec3( 1.0 / 2.4 ) ) - 0.055, step( vec3( 0.0031308 ), l ) );
}
`;

// kScene, source/Shaders.cpp
export const SCENE = `
uniform vec3 Eye;
uniform vec3 Forward;
uniform vec3 Right;
uniform vec3 Up;
uniform vec2 TanHalf;//horizontal, vertical

uniform int Count;
uniform vec3 FixPos[ kMaxFixtures ];
uniform vec3 FixDir[ kMaxFixtures ];
uniform vec3 FixRight[ kMaxFixtures ];
uniform vec3 FixUp[ kMaxFixtures ];
uniform vec3 FixI0[ kMaxFixtures ];//candela x colour, over the 20 cd/m^2 reference
uniform float TanAlpha;  //the gobo's half-angle's tangent
uniform float Focus;     //m
uniform float LensRadius;//m
uniform float Sigma;     //1/m
uniform sampler2D Gobo;

vec3 rayThrough( vec2 ndc )
{
	return normalize( Forward + ndc.x * TanHalf.x * Right + ndc.y * TanHalf.y * Up );
}

//The room's box along a ray: where it is inside, and which face it leaves
//by (1 the floor, 2 the back wall, 0 a black one).
void boxSpan( vec3 o, vec3 w, out float tNear, out float tFar, out int face )
{
	vec3 safe = vec3( abs( w.x ) < 1e-8 ? 1e-8 : w.x, abs( w.y ) < 1e-8 ? 1e-8 : w.y, abs( w.z ) < 1e-8 ? 1e-8 : w.z );
	vec3 a    = ( kBoxMin - o ) / safe;
	vec3 b    = ( kBoxMax - o ) / safe;
	vec3 lo   = min( a, b );
	vec3 hi   = max( a, b );
	tNear     = max( max( lo.x, lo.y ), max( lo.z, 0.0 ) );
	tFar      = min( hi.x, min( hi.y, hi.z ) );
	face      = 0;
	if( hi.y <= hi.x && hi.y <= hi.z && w.y < 0.0 )
		face = 1;
	else if( hi.z <= hi.x && hi.z <= hi.y && w.z < 0.0 )
		face = 2;
}

//The gobo's blur at axial distance r: a gobo point imaged at Focus by an
//aperture 2 LensRadius lands as a disc this wide in tan-space.
float defocus( float r )
{
	return hooked( kHookNoFocus ) ? 0.0 : 2.0 * LensRadius * abs( 1.0 / r - 1.0 / Focus );
}

//Tan-space ( q / r ) to the gobo texture: the disc |g| <= TanAlpha is the
//central half of it.
vec2 goboUV( vec2 g )
{
	return vec2( 0.5 ) + g / ( 4.0 * TanAlpha );
}

//In the air: the blur as a mip level (trilinear stands in for the disc).
vec3 goboAir( vec2 g, float r )
{
	float texels = defocus( r ) * 128.0 / TanAlpha;
	float lod    = clamp( log2( max( texels, 1.0 ) ), 0.0, 9.0 );
	return textureLod( Gobo, goboUV( g ), lod ).rgb;
}

//On a surface: a 16-tap golden-spiral disc of the blur's diameter, each tap
//pre-filtered at a quarter of it.
vec3 goboSurface( vec2 g, float r )
{
	vec2 uv      = goboUV( g );
	float texels = defocus( r ) * 128.0 / TanAlpha;
	if( texels <= 1.0 )
		return textureLod( Gobo, uv, 0.0 ).rgb;
	float lod = clamp( log2( 0.25 * texels ), 0.0, 9.0 );
	vec3 sum  = vec3( 0.0 );
	for( int k = 0; k < 16; ++k )
	{
		float rad = sqrt( ( float( k ) + 0.5 ) / 16.0 ) * 0.5 * texels / 512.0;
		float ang = float( k ) * 2.39996323;
		sum += textureLod( Gobo, uv + rad * vec2( cos( ang ), sin( ang ) ), lod ).rgb;
	}
	return sum / 16.0;
}

//A point's place in fixture i's beam: tan-space, and the axial distance.
vec2 tanSpace( int i, vec3 v, out float r )
{
	r      = dot( v, FixDir[ i ] );
	vec2 g = vec2( dot( v, FixRight[ i ] ), dot( v, FixUp[ i ] ) ) / r;
	if( hooked( kHookMirror ) )
		g.x = -g.x;
	return g;
}

//Illuminance's geometry: 1 / ( d^2 + a^2 ), exact on axis for a Lambertian
//disc of the lens's radius, finite at the lens.
float spread( float dist2 )
{
	return hooked( kHookNoInvSquare ) ? 1.0 / ( 1.0 + LensRadius * LensRadius )
	                                  : 1.0 / ( dist2 + LensRadius * LensRadius );
}

float transmit( float lightPath, float eyePath )
{
	float l = hooked( kHookNoLightExt ) ? 0.0 : lightPath;
	float e = hooked( kHookNoEyeExt ) ? 0.0 : eyePath;
	return exp( -Sigma * ( l + e ) );
}
`;

// kNoiseFragment, source/Shaders.cpp
export const NOISE_FRAGMENT = `
uniform vec4 Offset[ 3 ];//per octave: the lattice offset ( x, y, z ) and the time coordinate w, mod 64

out vec4 fragColour;

const int kPeriod = 64;

float lattice( ivec4 c )
{
	ivec4 m = ( ( c % kPeriod ) + kPeriod ) % kPeriod;
	uint h  = pcg( uint( m.x ) + pcg( uint( m.y ) + pcg( uint( m.z ) + pcg( uint( m.w ) ) ) ) );
	return float( h & 0xFFFFFFu ) / 16777215.0 * 2.0 - 1.0;
}

vec4 fade( vec4 t )
{
	return t * t * t * ( t * ( t * 6.0 - 15.0 ) + 10.0 );
}

//4D value noise: interpolated lattice values, so |v| <= 1 and E v = 0.
float vnoise( vec4 q )
{
	vec4 f  = floor( q );
	ivec4 c = ivec4( f );
	vec4 u  = fade( q - f );
	float acc = 0.0;
	for( int k = 0; k < 16; ++k )
	{
		ivec4 o  = ivec4( k & 1, ( k >> 1 ) & 1, ( k >> 2 ) & 1, ( k >> 3 ) & 1 );
		vec4 wt  = mix( vec4( 1.0 ) - u, u, vec4( o ) );
		acc += wt.x * wt.y * wt.z * wt.w * lattice( c + o );
	}
	return acc;
}

void main()
{
	ivec2 p = ivec2( gl_FragCoord.xy );
	int z   = ( p.y / 32 ) * 8 + p.x / 64;
	vec3 c  = ( vec3( float( p.x % 64 ), float( p.y % 32 ), float( z ) ) + 0.5 ) / vec3( 64.0, 32.0, 64.0 );
	vec3 x  = kBoxMin + c * ( kBoxMax - kBoxMin );
	float n = ( 4.0 / 7.0 ) * vnoise( vec4( x / 3.0 + Offset[ 0 ].xyz, Offset[ 0 ].w ) )
	        + ( 2.0 / 7.0 ) * vnoise( vec4( x / 1.5 + Offset[ 1 ].xyz, Offset[ 1 ].w ) )
	        + ( 1.0 / 7.0 ) * vnoise( vec4( x / 0.75 + Offset[ 2 ].xyz, Offset[ 2 ].w ) );
	fragColour = vec4( n, 0.0, 0.0, 1.0 );
}
`;

// kGoboFragment, source/Shaders.cpp
export const GOBO_FRAGMENT = `
uniform int GoboKind;//0 Open, 1 Dots, 2 Bars, 3 Star, 4 Ring, 5 Breakup, 100 the clip
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform float ClipAspect;

out vec4 fragColour;

float cellValue( ivec2 c )
{
	return float( pcg( uint( c.x + 4096 ) * 7919u + pcg( uint( c.y + 4096 ) ) ) & 0xFFFFu ) / 65535.0;
}

float breakup( vec2 g )
{
	vec2 q  = g * 3.2 + vec2( 11.3, 7.7 );
	vec2 f  = floor( q );
	vec2 u  = q - f;
	u       = u * u * ( 3.0 - 2.0 * u );
	ivec2 c = ivec2( f );
	float a = mix( cellValue( c ), cellValue( c + ivec2( 1, 0 ) ), u.x );
	float b = mix( cellValue( c + ivec2( 0, 1 ) ), cellValue( c + ivec2( 1, 1 ) ), u.x );
	return step( 0.52, mix( a, b, u.y ) );
}

vec3 pattern( vec2 g )
{
	float rho = length( g );
	if( rho > 1.0 )
		return vec3( 0.0 );
	if( GoboKind == 100 )
	{
		vec2 crop = ClipAspect >= 1.0 ? vec2( 0.5 / ClipAspect, 0.5 ) : vec2( 0.5, 0.5 * ClipAspect );
		vec4 clip  = texture( InputTexture, ( vec2( 0.5 ) + g * crop ) * MaxUV );
		return toLinear( clip.rgb ) * clamp( clip.a, 0.0, 1.0 );
	}
	if( GoboKind == 1 )
	{
		float best = length( g );
		for( int k = 0; k < 6; ++k )
		{
			float a = float( k ) * kPi / 3.0;
			best    = min( best, length( g - 0.58 * vec2( cos( a ), sin( a ) ) ) );
		}
		return vec3( step( best, 0.2 ) );
	}
	if( GoboKind == 2 )
		return vec3( step( fract( g.x * 2.5 + 0.25 ), 0.5 ) );
	if( GoboKind == 3 )
	{
		float a = atan( g.y, g.x ) + 0.5 * kPi;
		float f = fract( a / ( 2.0 * kPi / 5.0 ) );
		float s = abs( f - 0.5 ) * 2.0;
		return vec3( step( rho, 0.38 + 0.57 * s * s ) );
	}
	if( GoboKind == 4 )
		return vec3( ( rho > 0.55 && rho < 0.8 ) || rho < 0.2 ? 1.0 : 0.0 );
	if( GoboKind == 5 )
		return vec3( breakup( g ) );
	return vec3( 1.0 );
}

void main()
{
	vec3 sum = vec3( 0.0 );
	for( int k = 0; k < 4; ++k )
	{
		vec2 offs = vec2( float( k & 1 ), float( k >> 1 ) ) * 0.5 - 0.25;
		vec2 g    = ( ( gl_FragCoord.xy + offs ) / 512.0 - 0.5 ) * 4.0;
		sum += pattern( g );
	}
	fragColour = vec4( sum * 0.25, 1.0 );
}
`;

// kAirFragment, source/Shaders.cpp
export const AIR_FRAGMENT = `
uniform float G;   //Henyey-Greenstein's g
uniform float Stir;
uniform int Samples;
uniform int AirScale;
uniform ivec2 FullSize;
uniform sampler2D Density;//the noise pass's atlas

out vec4 fragColour;

float phase( float c )
{
	if( hooked( kHookIsotropic ) )
		return 1.0 / ( 4.0 * kPi );
	float d = 1.0 + G * G - 2.0 * G * c;
	return ( 1.0 - G * G ) / ( 4.0 * kPi * d * sqrt( d ) );
}

float stirAt( vec3 x )
{
	vec3 c   = ( x - kBoxMin ) / ( kBoxMax - kBoxMin ) * vec3( 64.0, 32.0, 64.0 );
	float zf = clamp( c.z - 0.5, 0.0, 63.0 );
	float z0 = floor( zf );
	float z1 = min( z0 + 1.0, 63.0 );
	vec2 xy  = clamp( c.xy, vec2( 0.5 ), vec2( 63.5, 31.5 ) );
	vec2 o0  = vec2( mod( z0, 8.0 ) * 64.0, floor( z0 / 8.0 ) * 32.0 );
	vec2 o1  = vec2( mod( z1, 8.0 ) * 64.0, floor( z1 / 8.0 ) * 32.0 );
	float a  = texture( Density, ( o0 + xy ) / vec2( 512.0, 256.0 ) ).r;
	float b  = texture( Density, ( o1 + xy ) / vec2( 512.0, 256.0 ) ).r;
	return mix( a, b, zf - z0 );
}

//The eye ray's stretch inside the cone that bounds fixture i's light: the
//gobo cone grown by the widest defocus disc, apex behind the lens.
bool coneSpan( int i, vec3 o, vec3 w, inout float t0, inout float t1 )
{
	vec3 d     = FixDir[ i ];
	float k    = ( TanAlpha + LensRadius / Focus ) * 1.02;
	float r0   = LensRadius / k;
	vec3 m     = o - ( FixPos[ i ] - r0 * d );
	float dd   = dot( w, d );
	float md   = dot( m, d );
	float kk   = 1.0 + k * k;
	float A    = 1.0 - kk * dd * dd;
	float B    = 2.0 * ( dot( m, w ) - kk * md * dd );
	float C    = dot( m, m ) - kk * md * md;
	//In front of the lens.
	if( abs( dd ) < 1e-7 )
	{
		if( md < r0 )
			return false;
	}
	else if( dd > 0.0 )
		t0 = max( t0, ( r0 - md ) / dd );
	else
		t1 = min( t1, ( r0 - md ) / dd );
	float disc = B * B - 4.0 * A * C;
	if( abs( A ) < 1e-7 )
	{
		if( abs( B ) < 1e-12 )
		{
			if( C > 0.0 )
				return false;
		}
		else if( B > 0.0 )
			t1 = min( t1, -C / B );
		else
			t0 = max( t0, -C / B );
	}
	else if( A > 0.0 )
	{
		if( disc <= 0.0 )
			return false;
		float sq = sqrt( disc );
		t0       = max( t0, ( -B - sq ) / ( 2.0 * A ) );
		t1       = min( t1, ( -B + sq ) / ( 2.0 * A ) );
	}
	else if( disc > 0.0 )
	{
		//The ray runs along the cone: inside beyond the far root, on the
		//forward nappe's side.
		float sq = sqrt( disc );
		if( dd > 0.0 )
			t0 = max( t0, ( -B - sq ) / ( 2.0 * A ) );
		else
			t1 = min( t1, ( -B + sq ) / ( 2.0 * A ) );
	}
	return t1 > t0;
}

vec3 beam( int i, vec3 o, vec3 w, float tNear, float tFar, float jitter )
{
	float t0 = tNear, t1 = tFar;
	if( !coneSpan( i, o, w, t0, t1 ) )
		return vec3( 0.0 );
	vec3 F      = FixPos[ i ];
	float delta = dot( F - o, w );
	float h     = max( length( F - o - delta * w ), 1e-4 );
	float thA   = atan( t0 - delta, h );
	float thB   = atan( t1 - delta, h );
	float span  = thB - thA;
	float n     = float( Samples );
	vec3 shift  = hooked( kHookStirPerBeam ) ? vec3( 1.7 * float( i ), 0.0, 0.0 ) : vec3( 0.0 );
	vec3 acc    = vec3( 0.0 );
	for( int j = 0; j < 64; ++j )
	{
		if( j >= Samples )
			break;
		float th = thA + span * ( float( j ) + jitter ) / n;
		float s  = tan( th );
		float t  = delta + h * s;
		vec3 x   = o + t * w;
		vec3 v   = x - F;
		float r;
		vec2 g = tanSpace( i, v, r );
		if( r <= 0.0 )
			continue;
		vec3 K = goboAir( g, r );
		if( K.r + K.g + K.b <= 0.0 )
			continue;
		float dist2 = dot( v, v );
		float dist  = sqrt( dist2 );
		float c     = -dot( v, w ) / dist;
		if( hooked( kHookBackward ) )
			c = -c;
		float local  = Sigma * max( 1.0 + Stir * stirAt( x + shift ), 0.0 );
		//Equiangular: dt / d( sample ) = span ( h^2 + ( t - delta )^2 ) / h.
		float weight = span / n * h * ( 1.0 + s * s );
		acc += K * ( local * phase( c ) * spread( dist2 ) * transmit( dist, t ) * weight );
	}
	return acc * FixI0[ i ];
}

void main()
{
	vec2 pix  = gl_FragCoord.xy * float( AirScale );
	vec2 ndc  = pix / vec2( FullSize ) * 2.0 - 1.0;
	vec3 o    = Eye;
	vec3 w    = rayThrough( ndc );
	float tNear, tFar;
	int face;
	boxSpan( o, w, tNear, tFar, face );
	vec3 sum = vec3( 0.0 );
	if( tFar > tNear )
	{
		uint seed    = pcg( uint( gl_FragCoord.x ) * 1973u + pcg( uint( gl_FragCoord.y ) * 9277u + 26699u ) );
		float jitter = float( seed & 0xFFFFu ) / 65536.0;
		for( int i = 0; i < kMaxFixtures; ++i )
		{
			if( i >= Count )
				break;
			sum += beam( i, o, w, tNear, tFar, jitter );
		}
	}
	fragColour = vec4( sum, 1.0 );
}
`;

// kFinalFragment, source/Shaders.cpp
export const FINAL_FRAGMENT = `
uniform sampler2D Air;
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform int IsOver;
uniform int ClipAs;//0 Gobo, 1 Backdrop, 2 Screen
uniform float ClipAspect;
uniform float MixAmount;
uniform float Gain;//2^Exposure
uniform ivec4 Viewport;
uniform int AirScale;
uniform ivec2 AirSize;
uniform float BackAlbedo;
uniform float FloorAlbedo;
uniform float ScreenHeight;
uniform float ScreenBottom;

out vec4 fragColour;

//Per channel 1 - exp( -x ), after a fifth of whatever the brightest channel
//exceeds 4 is added to all three: blown cores go white.
vec3 tone( vec3 x )
{
	float m = max( x.r, max( x.g, x.b ) );
	return vec3( 1.0 ) - exp( -( x + vec3( 0.2 * max( m - 4.0, 0.0 ) ) ) );
}

//The clip on the back wall, where it hangs; alpha 0 elsewhere.
vec4 wallClip( vec3 x )
{
	float h  = ScreenHeight;
	float wd = min( h * ClipAspect, 40.0 );
	vec2 uv  = vec2( ( x.x + 0.5 * wd ) / wd, ( x.y - ScreenBottom ) / h );
	if( uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 )
		return vec4( 0.0 );
	vec4 c = texture( InputTexture, uv * MaxUV );
	return vec4( toLinear( c.rgb ), clamp( c.a, 0.0, 1.0 ) );
}

vec3 surfaceLight( vec3 x, vec3 nrm, float tEye )
{
	vec3 sum = vec3( 0.0 );
	for( int i = 0; i < kMaxFixtures; ++i )
	{
		if( i >= Count )
			break;
		vec3 v = x - FixPos[ i ];
		float r;
		vec2 g = tanSpace( i, v, r );
		if( r <= 0.0 )
			continue;
		float dist2 = dot( v, v );
		float dist  = sqrt( dist2 );
		float cosI  = -dot( nrm, v ) / dist;
		if( cosI <= 0.0 )
			continue;
		if( length( g ) > ( TanAlpha + 0.5 * defocus( r ) ) * 1.05 + 1e-3 )
			continue;
		vec3 K = goboSurface( g, r );
		sum += FixI0[ i ] * K * ( spread( dist2 ) * cosI * transmit( dist, tEye ) );
	}
	return sum / kPi;
}

vec3 lenses( vec3 o, vec3 w, float tFar )
{
	vec3 sum = vec3( 0.0 );
	for( int i = 0; i < kMaxFixtures; ++i )
	{
		if( i >= Count )
			break;
		vec3 d   = FixDir[ i ];
		float dw = dot( w, d );
		if( dw >= 0.0 )
			continue;//from behind: the fixture's body
		float tl = dot( FixPos[ i ] - o, d ) / dw;
		if( tl <= 0.0 || tl >= tFar )
			continue;
		vec3 p = o + tl * w - FixPos[ i ];
		if( dot( p, p ) > LensRadius * LensRadius )
			continue;
		//What the fixture sends toward the eye, over the lens's area.
		float r;
		vec2 g = tanSpace( i, o - FixPos[ i ], r );
		vec3 K = goboAir( g, r );
		sum += FixI0[ i ] * K * ( transmit( 0.0, tl ) / ( kPi * LensRadius * LensRadius ) );
	}
	return sum;
}

void main()
{
	ivec2 ip  = ivec2( gl_FragCoord.xy ) - Viewport.xy;
	vec4 clip = vec4( 0.0, 0.0, 0.0, 1.0 );
	if( IsOver != 0 )
	{
		clip = texelFetch( InputTexture, ip, 0 );
		if( MixAmount <= 0.0 )
		{
			fragColour = clip;
			return;
		}
	}
	vec2 pix = vec2( ip ) + 0.5;
	vec2 ndc = pix / vec2( Viewport.zw ) * 2.0 - 1.0;
	vec3 o   = Eye;
	vec3 w   = rayThrough( ndc );
	float tNear, tFar;
	int face;
	boxSpan( o, w, tNear, tFar, face );

	vec3 L = vec3( 0.0 );
	if( !hooked( kHookSurfacesOnly ) )
		L += texture( Air, pix / float( AirScale ) / vec2( AirSize ) ).rgb;
	if( !hooked( kHookAirOnly ) && tFar > tNear )
	{
		if( face != 0 )
		{
			vec3 x      = o + tFar * w;
			vec3 nrm    = face == 1 ? vec3( 0.0, 1.0, 0.0 ) : vec3( 0.0, 0.0, 1.0 );
			vec3 albedo = vec3( face == 1 ? FloorAlbedo : BackAlbedo );
			if( IsOver != 0 && face == 2 && ClipAs != 0 )
			{
				vec4 c = wallClip( x );
				if( ClipAs == 1 )
					albedo = mix( albedo, c.rgb, c.a );
				else
				{
					//A screen: the radiance that returns the clip through
					//the tone curve with nothing in front of it.
					vec3 lit = c.rgb * c.a;
					L += -log( vec3( 1.0 ) - min( lit, vec3( 0.98 ) ) ) / Gain * transmit( 0.0, tFar );
					albedo *= 1.0 - c.a;
				}
			}
			L += albedo * surfaceLight( x, nrm, tFar );
		}
		L += lenses( o, w, tFar );
	}

	vec3 x   = L * Gain;
	vec4 col = hooked( kHookRaw ) ? vec4( x, 1.0 ) : vec4( toCode( tone( x ) ), 1.0 );
	if( IsOver != 0 )
	{
		col.rgb = mix( clip.rgb, col.rgb, MixAmount );
		col.a   = hooked( kHookAlphaThrough ) ? clip.a : mix( clip.a, 1.0, MixAmount );
	}
	fragColour = col;
}
`;
