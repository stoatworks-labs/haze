#pragma once

#include "Controls.h"

#include <array>
#include <vector>

/**
    Everything that moves, in double, with no GL: the stage's geometry, the
    yokes' servos, their inverse kinematics and the chase that steps them.
    `hztest --servo`, `--late` and `--chase` check it without a context.

    **The servo** (one axis). Time-optimal from ( x0, v0 ) to ( X, 0 ) under
    |v| <= V and |a| <= A: accelerate, cruise, decelerate, solved in closed
    form (SPEC-haze.md, "Motion"). Rest to rest it takes D / V + V / A when it
    reaches full speed and 2 sqrt( D / A ) when it does not.

    **Vectorial.** Pan and tilt arrive together: the axis that would arrive
    first has its limits scaled to ( V k, A k^2 ), k found by bisection so its
    move takes as long as the other's. Rest to rest that is exactly the time
    scaling k = T_fast / T_slow.

    **The limits** come from the Clay Paky Mythos manual (pan 540 degrees in
    3.517 s, tilt 244 degrees in 2.180 s at Fast) and one assumption, that each
    ramp of a full-range move takes a quarter of it, so V = 4D / 3T and
    A = 4V / T. Motor Speed s scales them to ( V s, A s^2 ).
*/
namespace haze
{

using Vec3 = std::array< double, 3 >;

inline Vec3 operator+( const Vec3& a, const Vec3& b )
{
	return { a[ 0 ] + b[ 0 ], a[ 1 ] + b[ 1 ], a[ 2 ] + b[ 2 ] };
}
inline Vec3 operator-( const Vec3& a, const Vec3& b )
{
	return { a[ 0 ] - b[ 0 ], a[ 1 ] - b[ 1 ], a[ 2 ] - b[ 2 ] };
}
inline Vec3 operator*( const Vec3& a, double s )
{
	return { a[ 0 ] * s, a[ 1 ] * s, a[ 2 ] * s };
}
inline double dot( const Vec3& a, const Vec3& b )
{
	return a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 1 ] + a[ 2 ] * b[ 2 ];
}
inline Vec3 cross( const Vec3& a, const Vec3& b )
{
	return { a[ 1 ] * b[ 2 ] - a[ 2 ] * b[ 1 ], a[ 2 ] * b[ 0 ] - a[ 0 ] * b[ 2 ], a[ 0 ] * b[ 1 ] - a[ 1 ] * b[ 0 ] };
}
Vec3 normalise( const Vec3& v );

//---------------------------------------------------------------------------
// The stage (metres; x to the audience's right, y up, z toward the audience).
//---------------------------------------------------------------------------
namespace stage
{
constexpr double kBackWall = -8.0;
constexpr double kBoxMin[ 3 ] = { -20.0, 0.0, -8.0 };
constexpr double kBoxMax[ 3 ] = { 20.0, 12.0, 20.0 };
constexpr double kTrussY = 7.0, kTrussZ = -4.0;
constexpr double kFloorY = 0.4, kFloorZ = -7.0;
constexpr double kBackAlbedo = 0.5, kFloorAlbedo = 0.15;
//The camera: eye height, the circle it orbits on, what it looks at.
constexpr double kEyeHeight = 1.7, kOrbitRadius = 13.0;
constexpr double kTarget[ 3 ] = { 0.0, 3.5, -4.0 };
constexpr double kFieldOfView = 55.0;//vertical, degrees
//The clip on the back wall (the Over's Backdrop and Screen).
constexpr double kScreenHeight = 9.0, kScreenBottom = 0.5;

constexpr int kMaxFixtures = 12;

struct Placement
{
	Vec3 position;///< the lens's centre
	Vec3 home;    ///< the beam at pan 0, tilt 0: down from a truss, up from the deck
};
Placement Place( Layout layout, int index, int count );

struct Camera
{
	Vec3 eye, forward, right, up;
	double tanHalfV = 0.0;///< the vertical half-field's tangent
};
Camera CameraAt( double orbitDegrees );
Camera CameraLooking( const Vec3& eye, const Vec3& target, double tanHalfV );
} // namespace stage

//---------------------------------------------------------------------------
// The fixture: the Clay Paky Mythos (manual p.30; Mythos2 datasheet).
//---------------------------------------------------------------------------
namespace fixture
{
constexpr double kLensDiameter = 0.157;//m
constexpr double kLumens       = 24000.0;
constexpr double kPanRange = 540.0, kPanTime = 3.517;  //degrees, s at Fast
constexpr double kTiltRange = 244.0, kTiltTime = 2.180;
constexpr double kPanLimit = 270.0, kTiltLimit = 122.0;//+- degrees
/// The assumption: each ramp of a full-range move is this fraction of it.
constexpr double kRampFraction = 0.25;

struct Limits
{
	double v = 0.0, a = 0.0;///< degrees/s, degrees/s^2
};
Limits PanLimits( double speed = 1.0 );
Limits TiltLimits( double speed = 1.0 );

/// The beam's direction at ( pan, tilt ), degrees.
Vec3 Direction( const Vec3& home, double pan, double tilt );
/// The gobo's frame for a beam: U is the world's up made square to d (the
/// gobo indexed upright), R = d x U; near vertical, the yoke's tilt
/// direction stands in for up.
void GoboFrame( const Vec3& home, double pan, double tilt, Vec3& d, Vec3& right, Vec3& up );
/// The same rule for a bare direction; `fallback` stands in for up near vertical.
void GoboFrameOf( const Vec3& d, const Vec3& fallback, Vec3& right, Vec3& up );
} // namespace fixture

//---------------------------------------------------------------------------
// One axis.
//---------------------------------------------------------------------------
struct Profile
{
	struct Segment
	{
		double duration = 0.0, accel = 0.0;
	};
	double x0 = 0.0, v0 = 0.0, target = 0.0;
	Segment seg[ 3 ];
	double Duration() const
	{
		return seg[ 0 ].duration + seg[ 1 ].duration + seg[ 2 ].duration;
	}
	/// Position and velocity tau seconds after the plan began (held at the target after).
	void At( double tau, double& x, double& v ) const;
};

/// The time-optimal plan from ( x0, v0 ) to ( target, 0 ) under ( vmax, amax ).
Profile Plan( double x0, double v0, double target, double vmax, double amax );

/// Hooks for the negative controls. A shipped instance sets 0.
enum MotionHook : int
{
	kHookUnboundedAccel = 1 << 0,///< the servo jumps to full speed: no ramps
	kHookNoSync         = 1 << 1,///< each axis on its own time-optimal plan
	kHookStepAtFrameStart = 1 << 2///< a beat steps at its frame's start, not where it fell
};

/// Pan and tilt, synced.
class Yoke
{
public:
	void Reset( double pan, double tilt );
	/// A new target from host time `at` (the state there is the plan's).
	void Retarget( double at, double pan, double tilt, const fixture::Limits& panLimits, const fixture::Limits& tiltLimits,
	               int hooks );
	void At( double t, double& pan, double& tilt, double* panRate = nullptr, double* tiltRate = nullptr ) const;
	double ArrivalTime() const
	{
		return start + std::max( panPlan.Duration(), tiltPlan.Duration() );
	}
	double TargetPan() const
	{
		return panPlan.target;
	}
	double TargetTilt() const
	{
		return tiltPlan.target;
	}
	const Profile& PanPlan() const
	{
		return panPlan;
	}
	const Profile& TiltPlan() const
	{
		return tiltPlan;
	}
	double PlanStart() const
	{
		return start;
	}

private:
	Profile panPlan, tiltPlan;
	double start = 0.0;
};

/// ( pan, tilt ) for a direction, the solution with the shortest synced
/// move from ( pan0, tilt0 ); clamped to the tilt range if out of it.
void Aim( const Vec3& home, const Vec3& direction, double pan0, double tilt0, double& pan, double& tilt );

//---------------------------------------------------------------------------
// The chase.
//---------------------------------------------------------------------------
struct ChaseInputs
{
	Pattern pattern  = Pattern::Fan;
	StepOn stepOn    = StepOn::Beat;
	int presses      = 0;
	bool onset       = false;
	double bpm       = 120.0;
	double barPhase  = 0.0;
	bool hostBeat    = false;
	double spread    = 30.0;///< degrees
	double speed     = 1.0; ///< Motor Speed
	int count        = 6;
	Layout layout    = Layout::Line;
	Vec3 camera      = { 0.0, 1.7, 14.0 };
};

class Chase
{
public:
	static constexpr int kMaxStepsPerFrame = 8;

	void Reset();
	/// One host frame [ now - dt, now ]: steps whatever is due inside it,
	/// at the instant it is due, and runs every yoke to `now`.
	void Advance( double now, double dt, const ChaseInputs& in );

	int Count() const
	{
		return count;
	}
	const Yoke& YokeOf( int i ) const
	{
		return yokes[ static_cast< size_t >( i ) ];
	}
	/// The fixture's pan and tilt at the end of the last frame.
	void Pose( int i, double& pan, double& tilt ) const;
	long long Steps() const
	{
		return steps;
	}
	const std::vector< double >& StepTimes() const
	{
		return stepTimes;
	}
	/// The lean ( across, toward the audience ), degrees, step k gives fixture i of n.
	static void Lean( Pattern pattern, long long k, int i, int n, double spread, double& across, double& out );

	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}

private:
	void retarget( double at, const ChaseInputs& in, bool initial );

	std::vector< Yoke > yokes;
	int count          = 0;
	long long steps    = 0;
	double lastPhase   = -1.0;
	double frameEnd    = 0.0;
	double lastSpeed   = -1.0;
	bool started       = false;
	int hooks          = 0;
	std::vector< double > stepTimes;///< the last frame's
};

} // namespace haze
