#include "Haze.h"

#include "Diag.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace haze
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Host seconds a single frame may advance by: a stalled host must not throw
/// the yokes a tenth of a second of motion at once.
constexpr double kMaxFrameDelta = 0.1;
/// The stir: drift across the stage, the evolution's time constant, the
/// octaves' cells (SPEC-haze.md: assumptions, nothing measured).
constexpr double kDrift = 0.25;    //m/s, along +x
constexpr double kStirTime = 12.0; //s
constexpr double kCells[ 3 ] = { 3.0, 1.5, 0.75 };
constexpr double kLatticePeriod = 64.0;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// Every pass here writes every texel of its target, so what the target held
/// does not matter -- but Apple's GL runs on Metal, where binding a
/// framebuffer LOADS the attachment unless it is cleared first. A clear
/// becomes the pass's load action, which costs nothing.
void discardTarget()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
}

double positiveMod( double x, double period )
{
	const double m = std::fmod( x, period );
	return m < 0.0 ? m + period : m;
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
HazePlugin::HazePlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );

	//The motors run on the host's clock: their limits are in degrees a
	//second, so the plugin has to know what a second is.
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		if( id >= PT_ABOUT_TEXT )
			break;
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( host, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Event: SetParamInfo( host, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//An FFT buffer: Resolume shows it as an audio-source picker and
			//writes the spectrum into it every frame. With nothing routed,
			//Onset never steps.
			SetBufferParamInfo( host, info.name, audio::kBins, FF_USAGE_FFT );
			for( int i = 0; i < audio::kBins; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( host, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	const unsigned int aboutHost = static_cast< unsigned int >( idToHost[ PT_ABOUT_TEXT ] );
	SetParamInfo( aboutHost, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = aboutHost + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = aboutHost; i < hostOrder.size(); ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint HazePlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void HazePlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void HazePlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}
void HazePlugin::uniform( const FFGLShader& shader, const char* name, float a, float b )
{
	glUniform2f( where( shader, name ), a, b );
}
void HazePlugin::uniform( const FFGLShader& shader, const char* name, const Vec3& v )
{
	glUniform3f( where( shader, name ), static_cast< float >( v[ 0 ] ), static_cast< float >( v[ 1 ] ), static_cast< float >( v[ 2 ] ) );
}

//---------------------------------------------------------------------------
FFResult HazePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) + ( isEffect ? " (Over)" : " (source)" ) );

	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &noiseShader, shaders::Pass::Noise, "noise" },
		{ &goboShader, shaders::Pass::Gobo, "gobo" },
		{ &airShader, shaders::Pass::Air, "air" },
		{ &finalShader, shaders::Pass::Final, "final" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Haze: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void HazePlugin::drawNoise()
{
	//The drift and the evolution as lattice offsets, reduced in double: the
	//stir's clock is the sum of frame deltas, never the host's raw clock.
	for( int o = 0; o < 3; ++o )
	{
		stirOffsets[ o ][ 0 ] = positiveMod( -kDrift * stirTime / kCells[ o ], kLatticePeriod );
		stirOffsets[ o ][ 1 ] = 17.0 * ( o + 1 );
		stirOffsets[ o ][ 2 ] = 29.0 * ( o + 1 );
		stirOffsets[ o ][ 3 ] = positiveMod( stirTime / kStirTime + 13.0 * o, kLatticePeriod );
	}
	ScopedFBOBinding target( atlas.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, shaders::kAtlasW, shaders::kAtlasH );
	discardTarget();
	glUseProgram( noiseShader.GetGLID() );
	float offsets[ 12 ];
	for( int o = 0; o < 3; ++o )
		for( int k = 0; k < 4; ++k )
			offsets[ o * 4 + k ] = static_cast< float >( stirOffsets[ o ][ k ] );
	glUniform4fv( where( noiseShader, "Offset" ), 3, offsets );
	uniform( noiseShader, "Hooks", hooks );
	quad.Draw();
	glUseProgram( 0 );
}

void HazePlugin::drawGobo( const FFGLTextureStruct* input, int kind )
{
	{
		ScopedFBOBinding target( gobo.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, shaders::kGoboSize, shaders::kGoboSize );
		discardTarget();
		glUseProgram( goboShader.GetGLID() );
		//Always a real texture on the clip's unit, even in the source: a
		//sampler on texture 0 is "unloadable" to Apple's GL (boreal's trap).
		bindUnit( 0, input != nullptr ? input->Handle : atlas.TextureID() );
		uniform( goboShader, "InputTexture", 0 );
		if( input != nullptr )
		{
			const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
			uniform( goboShader, "MaxUV", maxCoords.s, maxCoords.t );
			uniform( goboShader, "ClipAspect", static_cast< float >( input->Width ) / static_cast< float >( std::max( input->Height, 1u ) ) );
		}
		else
		{
			uniform( goboShader, "MaxUV", 1.0f, 1.0f );
			uniform( goboShader, "ClipAspect", 1.0f );
		}
		uniform( goboShader, "GoboKind", kind );
		uniform( goboShader, "Hooks", hooks );
		quad.Draw();
		unbindTextureUnits( 1 );
		glUseProgram( 0 );
	}
	gobo.GenerateMipmaps();
	goboBuilt = kind;
}

void HazePlugin::sceneUniforms( const FFGLShader& shader )
{
	const stage::Camera& c = frame.camera;
	uniform( shader, "Eye", c.eye );
	uniform( shader, "Forward", c.forward );
	uniform( shader, "Right", c.right );
	uniform( shader, "Up", c.up );
	float pos[ 36 ] = {}, dir[ 36 ] = {}, right[ 36 ] = {}, up[ 36 ] = {}, i0[ 36 ] = {};
	double rgb[ 3 ];
	ColourOf( static_cast< Colour >( OptionIndex( params[ PT_COLOUR ], static_cast< int >( Colour::Count ) ) ), rgb );
	for( int i = 0; i < frame.count; ++i )
		for( int k = 0; k < 3; ++k )
		{
			pos[ i * 3 + k ]   = static_cast< float >( frame.position[ i ][ k ] );
			dir[ i * 3 + k ]   = static_cast< float >( frame.direction[ i ][ k ] );
			right[ i * 3 + k ] = static_cast< float >( frame.right[ i ][ k ] );
			up[ i * 3 + k ]    = static_cast< float >( frame.up[ i ][ k ] );
			i0[ i * 3 + k ]    = static_cast< float >( frame.intensity * rgb[ k ] / kReferenceLuminance );
		}
	uniform( shader, "Count", frame.count );
	glUniform3fv( where( shader, "FixPos" ), shaders::kMaxFixtures, pos );
	glUniform3fv( where( shader, "FixDir" ), shaders::kMaxFixtures, dir );
	glUniform3fv( where( shader, "FixRight" ), shaders::kMaxFixtures, right );
	glUniform3fv( where( shader, "FixUp" ), shaders::kMaxFixtures, up );
	glUniform3fv( where( shader, "FixI0" ), shaders::kMaxFixtures, i0 );
	uniform( shader, "TanAlpha", static_cast< float >( frame.tanAlpha ) );
	uniform( shader, "Focus", static_cast< float >( frame.focus ) );
	uniform( shader, "LensRadius", static_cast< float >( 0.5 * fixture::kLensDiameter ) );
	uniform( shader, "Sigma", static_cast< float >( frame.sigma ) );
	uniform( shader, "Hooks", hooks );
}

void HazePlugin::drawAir( int width, int height )
{
	const int s = frame.airScale;
	ScopedFBOBinding target( air.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, ( width + s - 1 ) / s, ( height + s - 1 ) / s );
	discardTarget();
	glUseProgram( airShader.GetGLID() );
	bindUnit( 0, gobo.TextureID() );
	bindUnit( 1, atlas.TextureID() );
	uniform( airShader, "Gobo", 0 );
	uniform( airShader, "Density", 1 );
	sceneUniforms( airShader );
	const double tanV = frame.camera.tanHalfV;
	uniform( airShader, "TanHalf", static_cast< float >( tanV * width / height ), static_cast< float >( tanV ) );
	uniform( airShader, "G", static_cast< float >( frame.g ) );
	uniform( airShader, "Stir", static_cast< float >( frame.stir ) );
	uniform( airShader, "Samples", frame.samples );
	uniform( airShader, "AirScale", s );
	glUniform2i( where( airShader, "FullSize" ), width, height );
	quad.Draw();
	unbindTextureUnits( 2 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

void HazePlugin::drawFinal( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( finalShader.GetGLID() );
	bindUnit( 0, air.TextureID() );
	bindUnit( 1, input != nullptr ? input->Handle : atlas.TextureID() );
	bindUnit( 2, gobo.TextureID() );
	uniform( finalShader, "Air", 0 );
	uniform( finalShader, "InputTexture", 1 );
	uniform( finalShader, "Gobo", 2 );
	sceneUniforms( finalShader );
	const double tanV = frame.camera.tanHalfV;
	uniform( finalShader, "TanHalf", static_cast< float >( tanV * width / height ), static_cast< float >( tanV ) );
	if( input != nullptr )
	{
		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
		uniform( finalShader, "MaxUV", maxCoords.s, maxCoords.t );
		uniform( finalShader, "ClipAspect", static_cast< float >( input->Width ) / static_cast< float >( std::max( input->Height, 1u ) ) );
	}
	else
	{
		uniform( finalShader, "MaxUV", 1.0f, 1.0f );
		uniform( finalShader, "ClipAspect", 16.0f / 9.0f );
	}
	uniform( finalShader, "IsOver", isEffect ? 1 : 0 );
	uniform( finalShader, "ClipAs", isEffect ? OptionIndex( params[ PT_CLIP_AS ], static_cast< int >( ClipAs::Count ) ) : 0 );
	uniform( finalShader, "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
	uniform( finalShader, "Gain", static_cast< float >( frame.gain ) );
	glUniform4i( where( finalShader, "Viewport" ), hostViewport[ 0 ], hostViewport[ 1 ], width, height );
	uniform( finalShader, "AirScale", frame.airScale );
	glUniform2i( where( finalShader, "AirSize" ), ( width + frame.airScale - 1 ) / frame.airScale,
	             ( height + frame.airScale - 1 ) / frame.airScale );
	uniform( finalShader, "BackAlbedo", static_cast< float >( stage::kBackAlbedo ) );
	uniform( finalShader, "FloorAlbedo", static_cast< float >( stage::kFloorAlbedo ) );
	uniform( finalShader, "ScreenHeight", static_cast< float >( stage::kScreenHeight ) );
	uniform( finalShader, "ScreenBottom", static_cast< float >( stage::kScreenBottom ) );
	quad.Draw();
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void HazePlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (rosette's, by way of millpond).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

void HazePlugin::UpdateAudio( double dt )
{
	float bins[ audio::kBins ] = {};
	int binCount               = 0;
	const int host             = idToHost[ PT_AUDIO ];
	if( const ParamInfo* info = FindParamInfo( static_cast< FFUInt32 >( host ) ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), audio::kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	audio::Settings settings;
	analyser.SetUnprimedForTest( unprimed );
	analyser.Update( bins, binCount, static_cast< float >( dt ), settings );
}

//---------------------------------------------------------------------------
FFResult HazePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = input != nullptr ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height = input != nullptr ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//---------------------------------------------------------------------
	// Time, the audio and the beat.
	//---------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	UpdateAudio( dt );
	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;
	stirTime += dt;

	//---------------------------------------------------------------------
	// The camera, the chase, the yokes.
	//---------------------------------------------------------------------
	frame.camera = testRig.camera ? stage::CameraLooking( testRig.eye, testRig.target, testRig.tanHalfV )
	                              : stage::CameraAt( OrbitFromParam( params[ PT_ORBIT ] ) );

	ChaseInputs in;
	in.pattern  = static_cast< Pattern >( OptionIndex( params[ PT_PATTERN ], static_cast< int >( Pattern::Count ) ) );
	in.stepOn   = static_cast< StepOn >( OptionIndex( params[ PT_STEP_ON ], static_cast< int >( StepOn::Count ) ) );
	in.presses  = stepPresses;
	stepPresses = 0;
	in.onset    = analyser.Fired();
	in.bpm      = bpm;
	in.barPhase = barPhase;
	in.hostBeat = hostBeatSeen;
	in.spread   = SpreadFromParam( params[ PT_SPREAD ] );
	in.speed    = MotorSpeedFromParam( params[ PT_MOTOR_SPEED ] );
	in.count    = FixturesFromParam( params[ PT_FIXTURES ] );
	in.layout   = static_cast< Layout >( OptionIndex( params[ PT_LAYOUT ], static_cast< int >( Layout::Count ) ) );
	in.camera   = frame.camera.eye;
	chase.Advance( now, dt, in );

	const double alpha = 0.5 * ZoomFromParam( params[ PT_ZOOM ] ) * kPi / 180.0;
	frame.tanAlpha     = std::tan( alpha );
	frame.intensity    = fixture::kLumens / ( 2.0 * kPi * ( 1.0 - std::cos( alpha ) ) );
	frame.focus        = FocusFromParam( params[ PT_FOCUS ] );
	frame.sigma        = HazeFromParam( params[ PT_HAZE ] );
	frame.g            = AnisotropyFromParam( params[ PT_ANISOTROPY ] );
	frame.stir         = StirFromParam( params[ PT_STIR ] );
	frame.gain         = std::pow( 2.0, ExposureFromParam( params[ PT_EXPOSURE ] ) );
	const Quality quality = static_cast< Quality >( OptionIndex( params[ PT_QUALITY ], static_cast< int >( Quality::Count ) ) );
	frame.airScale        = quality == Quality::High ? 1 : quality == Quality::Medium ? 2 : 4;
	frame.samples         = quality == Quality::High ? 32 : quality == Quality::Medium ? 24 : 16;

	if( testRig.fixtures )
	{
		frame.count = std::clamp( testRig.count, 0, stage::kMaxFixtures );
		for( int i = 0; i < frame.count; ++i )
		{
			frame.position[ i ]  = testRig.position[ i ];
			frame.direction[ i ] = normalise( testRig.direction[ i ] );
			fixture::GoboFrameOf( frame.direction[ i ], { 0.0, 0.0, -1.0 }, frame.right[ i ], frame.up[ i ] );
		}
	}
	else
	{
		frame.count = chase.Count();
		for( int i = 0; i < frame.count; ++i )
		{
			const stage::Placement place = stage::Place( in.layout, i, frame.count );
			double pan = 0.0, tilt = 0.0;
			chase.Pose( i, pan, tilt );
			frame.pose[ i ]     = { pan, tilt };
			frame.position[ i ] = place.position;
			fixture::GoboFrame( place.home, pan, tilt, frame.direction[ i ], frame.right[ i ], frame.up[ i ] );
		}
	}

	//---------------------------------------------------------------------
	// The buffers, allocated before anything is bound.
	//---------------------------------------------------------------------
	const int airW = ( width + frame.airScale - 1 ) / frame.airScale;
	const int airH = ( height + frame.airScale - 1 ) / frame.airScale;
	if( !atlas.Ensure( shaders::kAtlasW, shaders::kAtlasH, GL_R16F, PassBuffer::Sampling::Linear )
	    || !gobo.Ensure( shaders::kGoboSize, shaders::kGoboSize, GL_RGBA16F, PassBuffer::Sampling::Mipmapped )
	    || !air.Ensure( airW, airH, GL_RGBA32F, PassBuffer::Sampling::Linear ) )
	{
		diag::error( "could not allocate the buffers" );
		return FF_FAIL;
	}

	drawNoise();
	int kind = OptionIndex( params[ PT_GOBO ], static_cast< int >( Gobo::Count ) );
	if( isEffect && static_cast< ClipAs >( OptionIndex( params[ PT_CLIP_AS ], static_cast< int >( ClipAs::Count ) ) ) == ClipAs::Gobo )
		kind = static_cast< int >( Gobo::Clip );
	if( kind == static_cast< int >( Gobo::Clip ) || kind != goboBuilt )
		drawGobo( kind == static_cast< int >( Gobo::Clip ) ? input : nullptr, kind );
	drawAir( width, height );
	drawFinal( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult HazePlugin::DeInitGL()
{
	uniformCache.clear();
	noiseShader.FreeGLResources();
	goboShader.FreeGLResources();
	airShader.FreeGLResources();
	finalShader.FreeGLResources();
	quad.Release();
	atlas.Destroy();
	gobo.Destroy();
	air.Destroy();
	goboBuilt = -1;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult HazePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void HazePlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

char* HazePlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult HazePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void HazePlugin::SetById( unsigned int id, float value )
{
	if( id >= PT_COUNT || idToHost[ id ] < 0 )
		return;
	SetFloatParameter( static_cast< unsigned int >( idToHost[ id ] ), value );
}

FFResult HazePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Step acts on the press, once: an event arrives as 1 on press and 0 on
	//release, and a host is free to restate either.
	if( id == PT_STEP )
	{
		const bool down = value >= 0.5f;
		if( down && !stepHeld )
			++stepPresses;
		stepHeld = down;
	}
	params[ id ] = value;
	return FF_SUCCESS;
}

float HazePlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace haze
