#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace haze
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr int kFixturesHigh = 12;
//The Clay Paky Mythos: 2.5 degrees in its beam mode, 30.8 at the wide end
//of its fixed-gobo zoom (the manual's specifications, SPEC-haze.md).
constexpr double kZoomLow = 2.5, kZoomHigh = 30.8;
constexpr double kFocusLow = 2.0, kFocusHigh = 30.0;
//Teschke et al.'s 0.02-4.1 mg/m^3 times mie.py's 1.9-8.4 m^2/g is
//4e-5 - 0.034 /m on real sets; the top decade is a fog, on purpose.
constexpr double kHazeLow = 0.001, kHazeHigh = 0.1;
constexpr double kGHigh = 0.95;
constexpr double kSpreadLow = 5.0, kSpreadHigh = 90.0;
//150%: the Sharpy's pan is 144% of the Mythos's, its tilt 172%.
constexpr double kMotorLow = 0.05, kMotorHigh = 1.5;
constexpr double kOrbitHigh = 90.0;
constexpr double kStops = 4.0;

const char* const kLayoutNames[]  = { "Line", "Arc", "Floor", "Split" };
const char* const kGoboNames[]    = { "Open", "Dots", "Bars", "Star", "Ring", "Breakup" };
const char* const kColourNames[]  = { "White", "Red", "Amber", "Green", "Cyan", "Blue", "Magenta" };
const char* const kPatternNames[] = { "Fan", "Cross", "Sweep", "Audience", "Wave", "Random", "Hold" };
const char* const kStepOnNames[]  = { "4 a Beat", "2 a Beat", "Beat", "2 Beats", "Bar", "Onset", "Step Only" };
const char* const kQualityNames[] = { "High", "Medium", "Low" };
const char* const kClipAsNames[]  = { "Gobo", "Backdrop", "Screen" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: six Mythos on a straight truss, 12 degree zoom, focused at
// 9 m, a heavy concert haze (0.015 /m, g 0.70, stirred), a fan chase on
// every beat at the motors' full speed, seen from front of house. AGENTS.md
// ("Decisions") says how each was chosen.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	standard( PT_FIXTURES, "Fixtures", "Fixtures", ParamFromFixtures( 6 ) ),
	option( PT_LAYOUT, "Layout", "Fixtures", 0.0f, kLayoutNames, static_cast< int >( Layout::Count ) ),
	standard( PT_ZOOM, "Zoom", "Fixtures", ParamFromZoom( 12.0 ) ),
	option( PT_GOBO, "Gobo", "Fixtures", 1.0f, kGoboNames, static_cast< int >( Gobo::Count ) ),
	option( PT_COLOUR, "Colour", "Fixtures", 0.0f, kColourNames, static_cast< int >( Colour::Count ) ),
	standard( PT_FOCUS, "Focus", "Fixtures", ParamFromFocus( 9.0 ) ),

	standard( PT_HAZE, "Haze", "Haze", ParamFromHaze( 0.015 ) ),
	standard( PT_ANISOTROPY, "Anisotropy", "Haze", ParamFromAnisotropy( 0.70 ) ),
	standard( PT_STIR, "Stir", "Haze", 0.5f ),

	option( PT_PATTERN, "Pattern", "Chase", 0.0f, kPatternNames, static_cast< int >( Pattern::Count ) ),
	option( PT_STEP_ON, "Step On", "Chase", 2.0f, kStepOnNames, static_cast< int >( StepOn::Count ) ),
	standard( PT_SPREAD, "Spread", "Chase", ParamFromSpread( 30.0 ) ),
	standard( PT_MOTOR_SPEED, "Motor Speed", "Chase", ParamFromMotorSpeed( 1.0 ) ),
	event( PT_STEP, "Step", "Chase" ),
	{ PT_AUDIO, "Audio", "Chase", Kind::Buffer, 0.0f, nullptr, 0 },

	standard( PT_ORBIT, "Orbit", "View", ParamFromOrbit( 0.0 ) ),
	standard( PT_EXPOSURE, "Exposure", "View", ParamFromExposure( 0.0 ) ),
	option( PT_QUALITY, "Quality", "View", 1.0f, kQualityNames, static_cast< int >( Quality::Count ) ),
	option( PT_CLIP_AS, "Clip As", "View", 2.0f, kClipAsNames, static_cast< int >( ClipAs::Count ) ),
	standard( PT_MIX, "Mix", "View", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			const bool overOnly = id == PT_CLIP_AS || id == PT_MIX;
			if( !over && overOnly )
				continue;
			order.push_back( id );
		}
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false ), over = build( true );
	return effect ? over : source;
}

int OptionIndex( float value, int count )
{
	if( !std::isfinite( value ) )
		return 0;
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

int FixturesFromParam( float v )
{
	return std::clamp( static_cast< int >( std::lround( linear( v, 1.0, kFixturesHigh ) ) ), 1, kFixturesHigh );
}
float ParamFromFixtures( int count )
{
	return inverseLinear( count, 1.0, kFixturesHigh );
}

double ZoomFromParam( float v )
{
	return linear( v, kZoomLow, kZoomHigh );
}
float ParamFromZoom( double degrees )
{
	return inverseLinear( degrees, kZoomLow, kZoomHigh );
}

double FocusFromParam( float v )
{
	return geometric( v, kFocusLow, kFocusHigh );
}
float ParamFromFocus( double metres )
{
	return inverseGeometric( metres, kFocusLow, kFocusHigh );
}

double HazeFromParam( float v )
{
	return geometric( v, kHazeLow, kHazeHigh );
}
float ParamFromHaze( double sigma )
{
	return inverseGeometric( sigma, kHazeLow, kHazeHigh );
}

double AnisotropyFromParam( float v )
{
	return linear( v, 0.0, kGHigh );
}
float ParamFromAnisotropy( double g )
{
	return inverseLinear( g, 0.0, kGHigh );
}

double StirFromParam( float v )
{
	return clamp01( v );
}

double SpreadFromParam( float v )
{
	return linear( v, kSpreadLow, kSpreadHigh );
}
float ParamFromSpread( double degrees )
{
	return inverseLinear( degrees, kSpreadLow, kSpreadHigh );
}

double MotorSpeedFromParam( float v )
{
	return geometric( v, kMotorLow, kMotorHigh );
}
float ParamFromMotorSpeed( double fraction )
{
	return inverseGeometric( fraction, kMotorLow, kMotorHigh );
}

double OrbitFromParam( float v )
{
	return linear( v, -kOrbitHigh, kOrbitHigh );
}
float ParamFromOrbit( double degrees )
{
	return inverseLinear( degrees, -kOrbitHigh, kOrbitHigh );
}

double ExposureFromParam( float v )
{
	return linear( v, -kStops, kStops );
}
float ParamFromExposure( double stops )
{
	return inverseLinear( stops, -kStops, kStops );
}

void ColourOf( Colour colour, double rgb[ 3 ] )
{
	static const double table[][ 3 ] = {
		{ 1.0, 1.0, 1.0 },  //White
		{ 1.0, 0.02, 0.01 },//Red
		{ 1.0, 0.45, 0.04 },//Amber
		{ 0.05, 1.0, 0.08 },//Green
		{ 0.04, 0.85, 1.0 },//Cyan
		{ 0.03, 0.12, 1.0 },//Blue
		{ 1.0, 0.04, 0.75 },//Magenta
	};
	const int i = std::clamp( static_cast< int >( colour ), 0, static_cast< int >( Colour::Count ) - 1 );
	for( int k = 0; k < 3; ++k )
		rgb[ k ] = table[ i ][ k ];
}

} // namespace haze
