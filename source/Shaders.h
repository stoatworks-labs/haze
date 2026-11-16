#pragma once

#include <string>

/**
    The passes, once a frame.

        1. **noise**  R16F 512 x 256: the haze's stir, n( x, t ) in [ -1, 1 ],
                      on a 64 x 32 x 64 grid over the room stored as an 8 x 8
                      atlas of 64 x 32 slices. Three octaves of 4D value noise
                      (3, 1.5, 0.75 m cells; lattice values uniform on [ -1, 1 ],
                      weights 4/7, 2/7, 1/7, so |n| <= 1 and its expectation
                      is 0); the drift and the evolution are lattice offsets
                      the CPU reduces modulo the lattice's period in double.
        2. **gobo**   RGBA16F 512 x 512, mipmapped: the gobo's transmission in
                      linear light, in the central 256 x 256 (a disc of radius
                      1 in gobo units), black around it so a blur spills
                      outward. Built-in patterns are drawn once per change;
                      the Over's clip every frame (centre-cropped to a square,
                      premultiplied by alpha, masked by the round gate).
        3. **air**    RGBA32F at Quality's raster: the light the beams scatter
                      toward the eye, in units of the 20 cd/m^2 reference. Per
                      fixture: the eye ray against the cone that bounds the
                      beam (the gobo cone grown by the widest defocus disc),
                      clipped to the lens plane and the room, then integrated
                      by equiangular sampling about the lens (Kulla & Fajardo
                      2012) with a per-pixel jitter hashed from the pixel only.
        4. **final**  the host's frame: the floor or back wall each beam
                      lights (the gobo through a 16-tap defocus disc), the
                      lenses, the air upsampled, exposure, the tone curve, sRGB,
                      and the Over's composite.

    Assembled at InitGL as kVersion + kCommon + the pass by `Assemble()`, the
    one place the order is written; tools/glslc.sh's ASSEMBLED table mirrors it.
*/
namespace haze::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kNoiseFragment;
extern const char* const kGoboFragment;
extern const char* const kAirFragment;
extern const char* const kFinalFragment;

enum class Pass
{
	Noise,
	Gobo,
	Air,
	Final
};

std::string Assemble( Pass pass );

/// Hook bits (the Hooks uniform). A shipped instance sets 0.
enum Hook : int
{
	kHookRaw          = 1 << 0, ///< write linear radiance x Gain: no tone curve, no sRGB
	kHookAirOnly      = 1 << 1, ///< the air alone: no surfaces, no lenses
	kHookSurfacesOnly = 1 << 2, ///< the surfaces and lenses alone: no air
	kHookIsotropic    = 1 << 3, ///< negative: p = 1 / 4 pi
	kHookBackward     = 1 << 4, ///< negative: cos theta negated
	kHookNoLightExt   = 1 << 5, ///< negative: nothing lost between the lens and the droplet
	kHookNoEyeExt     = 1 << 6, ///< negative: nothing lost between the droplet and the eye
	kHookNoFocus      = 1 << 7, ///< negative: the gobo sharp at every distance
	kHookNoInvSquare  = 1 << 8, ///< negative: E without the inverse square
	kHookMirror       = 1 << 9, ///< negative: the gobo mirrored
	kHookStirPerBeam  = 1 << 10,///< negative: each beam sees its own haze
	kHookAlphaThrough = 1 << 11 ///< negative: the Over passes the clip's alpha through
};

constexpr int kMaxFixtures = 12;
constexpr int kGoboSize    = 512;
constexpr int kAtlasW = 512, kAtlasH = 256;

} // namespace haze::shaders
