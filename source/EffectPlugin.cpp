#include "Haze.h"

/**
    The effect: the same room, with the clip as every beam's gobo (or as the
    back wall the beams light, or a screen behind them).

    See SourcePlugin.cpp for why this file is listed in its own target.
    `SW Haze Over` is twelve characters, inside FFGL's 16.
*/
namespace
{
class HazeEffect : public haze::HazePlugin
{
public:
	HazeEffect() :
		HazePlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< HazeEffect >,// Create method
	"HZ02",                     // Plugin unique ID of maximum length 4
	"SW Haze Over",             // Plugin name
	2,                          // API major version number
	1,                          // API minor version number
	0,                          // Plugin major version number
	1,                          // Plugin minor version number
	FF_EFFECT,                  // Plugin type
	"Your clip as the gobo of moving heads in a hazed room: sharp where a beam meets the wall at the focus distance, "
	"smeared along the beam everywhere else, brightest when a beam swings at the camera. Or the clip as the back wall the "
	"beams light, or a screen behind them.",
	"Haze FFGL effect"// About
);

extern "C" const char* HazeEffectBuildStamp()
{
	return "haze " HAZE_VERSION " effect, built " __DATE__ " " __TIME__;
}
