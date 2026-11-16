#include "Haze.h"

/**
    The source: moving heads in a hazed room, each beam carrying the gobo
    wheel's gobo.

    Listed directly in the HazeSource target, not in haze_core: both plugins
    share the class and not the `CFFGLPluginInfo` below, and putting either
    registration in the shared library would register both plugins into both
    bundles. The core is an OBJECT library because this registers itself from
    a file-scope constructor nothing references.

    `SW Haze` is seven characters; the FFGL name field is char[ 16 ] and not
    null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class HazeSource : public haze::HazePlugin
{
public:
	HazeSource() :
		HazePlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< HazeSource >,// Create method
	"HZ01",                     // Plugin unique ID of maximum length 4
	"SW Haze",                  // Plugin name
	2,                          // API major version number
	1,                          // API minor version number
	0,                          // Plugin major version number
	1,                          // Plugin minor version number
	FF_SOURCE,                  // Plugin type
	"Moving heads in a hazed room. Each beam is seen only by the light the haze scatters toward you (Henyey-Greenstein, "
	"Beer-Lambert): faint from the side, blazing when it swings at the camera. The gobo is sharp only at the focus distance, "
	"and the motors are speed- and acceleration-limited, so a fast chase arrives late on every beat.",
	"Haze FFGL source"// About
);

extern "C" const char* HazeSourceBuildStamp()
{
	return "haze " HAZE_VERSION " source, built " __DATE__ " " __TIME__;
}
