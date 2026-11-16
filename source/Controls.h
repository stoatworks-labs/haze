#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index); Step is a press; Audio is Resolume's
    64-bin FFT buffer.

    Units: metres, degrees for angles the operator reads (Zoom, Spread,
    Orbit), m^-1 for the haze, a fraction of the Mythos's Fast speed for the
    motors, and stops of exposure.
*/
namespace haze
{
enum ParamId : unsigned int
{
	PT_FIXTURES = 0,
	PT_LAYOUT,
	PT_ZOOM,
	PT_GOBO,///< the source; the Over's Backdrop and Screen
	PT_COLOUR,
	PT_FOCUS,

	PT_HAZE,
	PT_ANISOTROPY,
	PT_STIR,

	PT_PATTERN,
	PT_STEP_ON,
	PT_SPREAD,
	PT_MOTOR_SPEED,
	PT_STEP,
	PT_AUDIO,

	PT_ORBIT,
	PT_EXPOSURE,
	PT_QUALITY,
	PT_CLIP_AS,///< Over only
	PT_MIX,    ///< Over only

	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer
};

struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

const ControlInfo& InfoOf( unsigned int id );

/// Host index -> ParamId for one plugin: the source has no Clip As or Mix.
const std::vector< unsigned int >& HostOrder( bool effect );

/// An option's element index from whatever the host sent (index, rounded).
int OptionIndex( float value, int count );

enum class Layout
{
	Line = 0,///< a straight truss, hanging
	Arc,     ///< the truss bowed toward the audience
	Floor,   ///< on the deck at the back, pointing up
	Split,   ///< alternately truss and floor
	Count
};

enum class Gobo
{
	Open = 0,
	Dots,
	Bars,
	Star,
	Ring,
	Breakup,
	Count,
	Clip = 100///< not an option: the Over's Clip As Gobo
};

enum class Colour
{
	White = 0,
	Red,
	Amber,
	Green,
	Cyan,
	Blue,
	Magenta,
	Count
};

enum class Pattern
{
	Fan = 0,
	Cross,
	Sweep,
	Audience,
	Wave,
	Random,
	Hold,
	Count
};

enum class StepOn
{
	FourABeat = 0,
	TwoABeat,
	Beat,
	TwoBeats,
	Bar,
	Onset,
	StepOnly,
	Count
};

enum class Quality
{
	High = 0,///< the air at the full raster, 32 samples a beam
	Medium,  ///< half the raster, 24
	Low,     ///< a quarter, 16
	Count
};

enum class ClipAs
{
	Gobo = 0,///< every beam carries the clip
	Backdrop,///< the back wall is the clip, lit only by the beams
	Screen,  ///< the back wall shows the clip, the beams in front of it
	Count
};

int FixturesFromParam( float value );
float ParamFromFixtures( int count );

double ZoomFromParam( float value );///< full beam angle, degrees
float ParamFromZoom( double degrees );

double FocusFromParam( float value );///< metres
float ParamFromFocus( double metres );

double HazeFromParam( float value );///< sigma, 1/m
float ParamFromHaze( double sigma );

double AnisotropyFromParam( float value );///< g
float ParamFromAnisotropy( double g );

double StirFromParam( float value );///< 0..1
double SpreadFromParam( float value );///< degrees
float ParamFromSpread( double degrees );

double MotorSpeedFromParam( float value );///< fraction of the Mythos's Fast
float ParamFromMotorSpeed( double fraction );

double OrbitFromParam( float value );///< degrees
float ParamFromOrbit( double degrees );

double ExposureFromParam( float value );///< stops
float ParamFromExposure( double stops );

/// Linear-sRGB filter colours: chosen, not measured (SPEC-haze.md).
void ColourOf( Colour colour, double rgb[ 3 ] );

constexpr double kPi = 3.14159265358979323846;

} // namespace haze
