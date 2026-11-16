#pragma once

#include "Audio.h"
#include "Controls.h"
#include "Motion.h"
#include "PassBuffer.h"
#include "Shaders.h"

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

#include <array>
#include <vector>

namespace haze
{

/// The exposure's reference: radiance is written in units of 20 cd/m^2 (a
/// choice: the default rig's side-on beams read at about a quarter of white).
constexpr double kReferenceLuminance = 20.0;

/// What one frame drew with: the harness reads it to build its closed forms.
struct FrameData
{
	stage::Camera camera;
	int count = 0;
	std::array< Vec3, stage::kMaxFixtures > position {}, direction {}, right {}, up {};
	std::array< double, 2 > pose[ stage::kMaxFixtures ] {};///< pan, tilt
	double intensity = 0.0;///< I0, candela (open gobo, before colour)
	double tanAlpha  = 0.0;
	double focus     = 0.0;
	double sigma     = 0.0;
	double g         = 0.0;
	double stir      = 0.0;
	double gain      = 1.0;///< 2^Exposure
	int airScale     = 1;
	int samples      = 0;
};

/// A test rig: the camera and fixtures put exactly where a check wants them,
/// the servos bypassed. Never set by a host.
struct TestRig
{
	bool camera = false;
	Vec3 eye {}, target {};
	double tanHalfV = 0.5;
	bool fixtures = false;
	int count     = 0;
	std::array< Vec3, stage::kMaxFixtures > position {}, direction {};
};

/**
    The plugin: the source (`SW Haze`, the gobo wheel's gobo) and, with
    `isEffect`, the Over (`SW Haze Over`, the clip as the gobo, the backdrop or
    a screen). One class, two registrations; see SourcePlugin.cpp and
    EffectPlugin.cpp.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate.
*/
class HazePlugin : public CFFGLPlugin
{
public:
	explicit HazePlugin( bool isEffect );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//The harness's hooks. A host never calls these.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	void SetMotionHooksForTest( int bits )
	{
		chase.SetHooksForTest( bits );
	}
	void SetRigForTest( const TestRig& rig )
	{
		testRig = rig;
	}
	void SetUnprimedForTest( bool on )
	{
		unprimed = on;
	}
	/// The stir's clock, s (it otherwise advances with the frames).
	void SetStirTimeForTest( double seconds )
	{
		stirTime = seconds;
	}
	const FrameData& LastFrame() const
	{
		return frame;
	}
	const Chase& ChaseState() const
	{
		return chase;
	}
	const audio::Analyser& Analyser() const
	{
		return analyser;
	}
	double HostNow() const
	{
		return now;
	}
	double StirTime() const
	{
		return stirTime;
	}
	/// The lattice offsets the last noise pass used: per octave ( x, y, z, w ).
	const std::array< std::array< double, 4 >, 3 >& StirOffsets() const
	{
		return stirOffsets;
	}
	GLuint AtlasTextureID() const
	{
		return atlas.TextureID();
	}
	GLuint GoboTextureID() const
	{
		return gobo.TextureID();
	}

private:
	void UpdateClock();
	void UpdateAudio( double dt );
	void drawNoise();
	void drawGobo( const FFGLTextureStruct* input, int kind );
	void sceneUniforms( const ffglex::FFGLShader& shader );
	void drawAir( int width, int height );
	void drawFinal( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b );
	void uniform( const ffglex::FFGLShader& shader, const char* name, const Vec3& v );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader noiseShader, goboShader, airShader, finalShader;
	ffglex::FFGLScreenQuad quad;
	PassBuffer atlas;///< R16F, the stir
	PassBuffer gobo; ///< RGBA16F, mipmapped
	PassBuffer air;  ///< RGBA32F at Quality's raster
	int goboBuilt = -1;

	Chase chase;
	audio::Analyser analyser;
	FrameData frame;
	TestRig testRig;
	double stirTime = 0.0;
	std::array< std::array< double, 4 >, 3 > stirOffsets {};

	//The clock (double throughout: Resolume's has been seen at ~499 million ms).
	double hostTime     = -1.0;
	double now          = 0.0;
	double lastNow      = -1.0;
	double clockScale   = 0.0;
	double wallStart    = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	bool settledJump = false;

	float bpm          = 120.0f;
	float barPhase     = 0.0f;
	float lastBarPhase = -1.0f;
	bool hostBeatSeen  = false;

	int stepPresses = 0;
	bool stepHeld   = false;
	bool unprimed   = false;
	int hooks       = 0;
};

} // namespace haze
