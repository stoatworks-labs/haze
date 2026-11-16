#include "Motion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace haze
{
namespace
{
constexpr double kDeg = kPi / 180.0;

/// splitmix64: a step's random lean, the same on every machine.
uint64_t mix64( uint64_t z )
{
	z += 0x9E3779B97F4A7C15ull;
	z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
	z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;
	return z ^ ( z >> 31 );
}
double unit( uint64_t h )
{
	return static_cast< double >( h >> 11 ) * ( 1.0 / 9007199254740992.0 );
}

/// Rest-to-rest time for a distance under ( v, a ).
double restTime( double distance, const fixture::Limits& l )
{
	distance = std::fabs( distance );
	if( distance <= 0.0 )
		return 0.0;
	return distance >= l.v * l.v / l.a ? distance / l.v + l.v / l.a : 2.0 * std::sqrt( distance / l.a );
}

/// The plan from ( x0, v0 ) to target that takes `duration`, the axis's
/// limits scaled to ( v k, a k^2 ): bisection on k (the vectorial sync).
Profile stretched( double x0, double v0, double target, const fixture::Limits& l, double duration )
{
	double lo = 1e-4, hi = 1.0;
	if( Plan( x0, v0, target, l.v * lo, l.a * lo * lo ).Duration() <= duration )
		return Plan( x0, v0, target, l.v * lo, l.a * lo * lo );
	for( int i = 0; i < 100 && hi - lo > 1e-15; ++i )
	{
		const double k = 0.5 * ( lo + hi );
		if( Plan( x0, v0, target, l.v * k, l.a * k * k ).Duration() > duration )
			lo = k;
		else
			hi = k;
	}
	return Plan( x0, v0, target, l.v * hi, l.a * hi * hi );
}
} // namespace

Vec3 normalise( const Vec3& v )
{
	const double l = std::sqrt( dot( v, v ) );
	return l > 0.0 ? v * ( 1.0 / l ) : Vec3 { 0.0, 0.0, 0.0 };
}

//---------------------------------------------------------------------------
namespace stage
{
Placement Place( Layout layout, int index, int count )
{
	count             = std::max( count, 1 );
	const double span = count > 1 ? std::min( 3.0, 16.0 / ( count - 1 ) ) : 0.0;
	const double x    = ( index - 0.5 * ( count - 1 ) ) * span;
	const Placement truss { { x, kTrussY, kTrussZ }, { 0.0, -1.0, 0.0 } };
	const Placement deck { { x, kFloorY, kFloorZ }, { 0.0, 1.0, 0.0 } };
	switch( layout )
	{
	case Layout::Arc:
	{
		const double bow = 1.0 - ( x / 8.0 ) * ( x / 8.0 );
		return { { x, kTrussY, kTrussZ + 2.0 * bow }, { 0.0, -1.0, 0.0 } };
	}
	case Layout::Floor: return deck;
	case Layout::Split: return ( index % 2 ) == 0 ? truss : deck;
	default: return truss;
	}
}

Camera CameraLooking( const Vec3& eye, const Vec3& target, double tanHalfV )
{
	Camera c;
	c.eye     = eye;
	c.forward = normalise( target - eye );
	c.right   = normalise( cross( c.forward, Vec3 { 0.0, 1.0, 0.0 } ) );
	if( dot( c.right, c.right ) < 0.5 )
		c.right = { 1.0, 0.0, 0.0 };
	//Right x up points at the viewer: a right-handed view (dice's lesson).
	c.up       = cross( c.right, c.forward );
	c.tanHalfV = tanHalfV;
	return c;
}

Camera CameraAt( double orbitDegrees )
{
	const double phi = orbitDegrees * kDeg;
	const Vec3 eye   = { kOrbitRadius * std::sin( phi ), kEyeHeight, kTarget[ 2 ] + kOrbitRadius * std::cos( phi ) };
	return CameraLooking( eye, { kTarget[ 0 ], kTarget[ 1 ], kTarget[ 2 ] }, std::tan( 0.5 * kFieldOfView * kDeg ) );
}
} // namespace stage

//---------------------------------------------------------------------------
namespace fixture
{
Limits PanLimits( double speed )
{
	const double v = kPanRange / ( ( 1.0 - kRampFraction ) * kPanTime );
	const double a = v / ( kRampFraction * kPanTime );
	return { v * speed, a * speed * speed };
}
Limits TiltLimits( double speed )
{
	const double v = kTiltRange / ( ( 1.0 - kRampFraction ) * kTiltTime );
	const double a = v / ( kRampFraction * kTiltTime );
	return { v * speed, a * speed * speed };
}

Vec3 Direction( const Vec3& home, double pan, double tilt )
{
	const double p = pan * kDeg, t = tilt * kDeg;
	const Vec3 lean = { std::sin( p ), 0.0, std::cos( p ) };
	return normalise( home * std::cos( t ) + lean * std::sin( t ) );
}

void GoboFrameOf( const Vec3& d, const Vec3& fallback, Vec3& right, Vec3& up )
{
	const Vec3 world  = { 0.0, 1.0, 0.0 };
	const Vec3 square = world - d * dot( world, d );
	if( std::sqrt( dot( square, square ) ) >= std::sin( 3.0 * kDeg ) )
		up = normalise( square );
	else
		up = normalise( fallback - d * dot( fallback, d ) );
	right = cross( d, up );
}

void GoboFrame( const Vec3& home, double pan, double tilt, Vec3& d, Vec3& right, Vec3& up )
{
	d               = Direction( home, pan, tilt );
	const double p  = pan * kDeg, t = tilt * kDeg;
	const Vec3 lean = { std::sin( p ), 0.0, std::cos( p ) };
	//Near vertical: the yoke's tilt direction (gobo indexing held to the head).
	GoboFrameOf( d, home * ( -std::sin( t ) ) + lean * std::cos( t ), right, up );
}
} // namespace fixture

//---------------------------------------------------------------------------
void Profile::At( double tau, double& x, double& v ) const
{
	if( tau >= Duration() )
	{
		x = target;
		v = 0.0;
		return;
	}
	x = x0;
	v = v0;
	tau = std::max( tau, 0.0 );
	for( const Segment& s : seg )
	{
		const double dt = std::min( tau, s.duration );
		x += v * dt + 0.5 * s.accel * dt * dt;
		v += s.accel * dt;
		tau -= dt;
		if( tau <= 0.0 )
			break;
	}
}

Profile Plan( double x0, double v0, double target, double vmax, double amax )
{
	Profile p;
	p.x0     = x0;
	p.v0     = v0;
	p.target = target;
	const double d = target - x0;
	if( !( vmax > 0.0 ) || !( amax > 0.0 ) || ( std::fabs( d ) < 1e-12 && std::fabs( v0 ) < 1e-12 ) )
	{
		p.x0 = target;
		p.v0 = 0.0;
		return p;
	}
	//Which way: past where stopping now would leave it, or short of it.
	const double stop  = v0 * std::fabs( v0 ) / ( 2.0 * amax );
	const double sigma = d - stop >= 0.0 ? 1.0 : -1.0;
	const double u0 = sigma * v0, D = sigma * d;
	double t1 = 0.0, t2 = 0.0, t3 = 0.0, a1 = sigma * amax;
	if( u0 > vmax )
	{
		//Over speed (Motor Speed turned down mid-move): slow to vmax first.
		t1              = ( u0 - vmax ) / amax;
		a1              = -sigma * amax;
		const double d1 = ( u0 * u0 - vmax * vmax ) / ( 2.0 * amax );
		const double d3 = vmax * vmax / ( 2.0 * amax );
		t2              = std::max( D - d1 - d3, 0.0 ) / vmax;
		t3              = vmax / amax;
	}
	else
	{
		const double peak = std::sqrt( std::max( amax * D + 0.5 * u0 * u0, 0.0 ) );
		if( peak <= vmax )
		{
			t1 = std::max( peak - u0, 0.0 ) / amax;
			t3 = peak / amax;
		}
		else
		{
			t1              = ( vmax - u0 ) / amax;
			const double d1 = ( vmax * vmax - u0 * u0 ) / ( 2.0 * amax );
			const double d3 = vmax * vmax / ( 2.0 * amax );
			t2              = std::max( D - d1 - d3, 0.0 ) / vmax;
			t3              = vmax / amax;
		}
	}
	p.seg[ 0 ] = { t1, a1 };
	p.seg[ 1 ] = { t2, 0.0 };
	p.seg[ 2 ] = { t3, -sigma * amax };
	return p;
}

//---------------------------------------------------------------------------
void Yoke::Reset( double pan, double tilt )
{
	panPlan  = Plan( pan, 0.0, pan, 1.0, 1.0 );
	tiltPlan = Plan( tilt, 0.0, tilt, 1.0, 1.0 );
	start    = 0.0;
}

void Yoke::Retarget( double at, double pan, double tilt, const fixture::Limits& panLimits, const fixture::Limits& tiltLimits, int hooks )
{
	double p = 0.0, t = 0.0, pv = 0.0, tv = 0.0;
	At( at, p, t, &pv, &tv );
	start = at;
	fixture::Limits lp = panLimits, lt = tiltLimits;
	if( ( hooks & kHookUnboundedAccel ) != 0 )
	{
		lp.a = 1e9;
		lt.a = 1e9;
	}
	panPlan  = Plan( p, pv, pan, lp.v, lp.a );
	tiltPlan = Plan( t, tv, tilt, lt.v, lt.a );
	if( ( hooks & kHookNoSync ) != 0 )
		return;
	const double tp = panPlan.Duration(), tt = tiltPlan.Duration();
	if( tp < tt )
		panPlan = stretched( p, pv, pan, lp, tt );
	else if( tt < tp )
		tiltPlan = stretched( t, tv, tilt, lt, tp );
}

void Yoke::At( double t, double& pan, double& tilt, double* panRate, double* tiltRate ) const
{
	double pv = 0.0, tv = 0.0;
	panPlan.At( t - start, pan, pv );
	tiltPlan.At( t - start, tilt, tv );
	if( panRate )
		*panRate = pv;
	if( tiltRate )
		*tiltRate = tv;
}

void Aim( const Vec3& home, const Vec3& direction, double pan0, double tilt0, double& pan, double& tilt )
{
	const Vec3 u   = normalise( direction );
	const double t = std::acos( std::clamp( dot( u, home ), -1.0, 1.0 ) ) / kDeg;
	const double across = u[ 0 ], out = u[ 2 ];
	const bool vertical = std::hypot( across, out ) < 1e-12;
	const double p      = vertical ? pan0 : std::atan2( across, out ) / kDeg;
	const fixture::Limits lp = fixture::PanLimits(), lt = fixture::TiltLimits();
	double best = 1e300, bestPan = pan0, bestTilt = tilt0, bestDp = 1e300;
	for( int sign = 0; sign < 2; ++sign )
	{
		const double tc = std::clamp( sign == 0 ? t : -t, -fixture::kTiltLimit, fixture::kTiltLimit );
		const double pc0 = sign == 0 ? p : p + 180.0;
		for( int k = -2; k <= 2; ++k )
		{
			const double pc = vertical ? pan0 : pc0 + 360.0 * k;
			if( std::fabs( pc ) > fixture::kPanLimit + 1e-9 )
				continue;
			const double cost = std::max( restTime( pc - pan0, lp ), restTime( tc - tilt0, lt ) );
			const double dp   = std::fabs( pc - pan0 );
			if( cost < best - 1e-12 || ( cost < best + 1e-12 && dp < bestDp ) )
			{
				best     = cost;
				bestDp   = dp;
				bestPan  = pc;
				bestTilt = tc;
			}
		}
	}
	pan  = bestPan;
	tilt = bestTilt;
}

//---------------------------------------------------------------------------
void Chase::Lean( Pattern pattern, long long k, int i, int n, double W, double& across, double& out )
{
	const double u   = n > 1 ? 2.0 * i / ( n - 1 ) - 1.0 : 0.0;
	const bool even  = ( k % 2 ) == 0;
	switch( pattern )
	{
	case Pattern::Fan:
		across = even ? u * W : 0.0;
		out    = 0.3 * W;
		break;
	case Pattern::Cross:
		across = even ? u * W : -u * W;
		out    = 0.5 * W;
		break;
	case Pattern::Sweep:
		across = even ? W : -W;
		out    = 0.5 * W;
		break;
	case Pattern::Audience:
		//The odd steps aim at the camera; Chase does that, not the lean.
		across = 0.5 * u * W;
		out    = W;
		break;
	case Pattern::Wave:
		across = 0.5 * u * W;
		out    = 0.5 * W * ( 1.0 + std::sin( 2.0 * kPi * ( static_cast< double >( k % 8 ) / 8.0 + 0.5 * u ) ) );
		break;
	case Pattern::Random:
	{
		const uint64_t h  = mix64( static_cast< uint64_t >( k ) * 16u + static_cast< uint64_t >( i ) );
		const double r    = W * std::sqrt( unit( h ) );
		const double ang  = 2.0 * kPi * unit( mix64( h ) );
		across            = r * std::sin( ang );
		out               = r * std::cos( ang );
		break;
	}
	default:
		across = 0.5 * u * W;
		out    = 0.5 * W;
		break;
	}
}

void Chase::Reset()
{
	yokes.clear();
	count     = 0;
	steps     = 0;
	lastPhase = -1.0;
	lastSpeed = -1.0;
	started   = false;
	stepTimes.clear();
}

void Chase::Pose( int i, double& pan, double& tilt ) const
{
	if( i < 0 || i >= count )
	{
		pan = tilt = 0.0;
		return;
	}
	yokes[ static_cast< size_t >( i ) ].At( frameEnd, pan, tilt );
}

void Chase::retarget( double at, const ChaseInputs& in, bool initial )
{
	const fixture::Limits lp = fixture::PanLimits( in.speed ), lt = fixture::TiltLimits( in.speed );
	const bool speedChanged  = lastSpeed >= 0.0 && in.speed != lastSpeed;
	for( int i = 0; i < count; ++i )
	{
		Yoke& y                       = yokes[ static_cast< size_t >( i ) ];
		const stage::Placement place  = stage::Place( in.layout, i, count );
		double pan = 0.0, tilt = 0.0;
		if( in.pattern == Pattern::Audience && ( steps % 2 ) == 1 )
			Aim( place.home, normalise( in.camera - place.position ), y.TargetPan(), y.TargetTilt(), pan, tilt );
		else
		{
			//A console's chase stores pan and tilt, not directions: the head
			//swings its axes and never flips over the top to save time. Pan
			//is taken at the turn nearest the last target that stays inside
			//the yoke's range.
			double across = 0.0, out = 0.0;
			Lean( in.pattern, steps, i, count, in.spread, across, out );
			tilt = std::min( std::hypot( across, out ), fixture::kTiltLimit );
			pan  = std::atan2( across, out ) / kDeg;
			pan += 360.0 * std::round( ( y.TargetPan() - pan ) / 360.0 );
			while( pan > fixture::kPanLimit )
				pan -= 360.0;
			while( pan < -fixture::kPanLimit )
				pan += 360.0;
		}
		if( initial )
			y.Reset( pan, tilt );
		else if( speedChanged || std::fabs( pan - y.TargetPan() ) > 1e-9 || std::fabs( tilt - y.TargetTilt() ) > 1e-9 )
			y.Retarget( at, pan, tilt, lp, lt, hooks );
	}
}

void Chase::Advance( double now, double dt, const ChaseInputs& in )
{
	const double frameStart = dt > 0.0 ? now - dt : now;
	stepTimes.clear();

	const int want = std::clamp( in.count, 1, stage::kMaxFixtures );
	if( want != count )
	{
		//New fixtures start at home and move to their places.
		const size_t old = yokes.size();
		yokes.resize( static_cast< size_t >( want ) );
		for( size_t i = old; i < yokes.size(); ++i )
			yokes[ i ].Reset( 0.0, 0.0 );
		count = want;
	}
	if( !started )
	{
		//A fresh instance starts with every beam already at the first step's place.
		retarget( frameStart, in, true );
		started = true;
	}
	else
		retarget( frameStart, in, false );
	lastSpeed = in.speed;

	std::vector< double > due;
	for( int i = 0; i < in.presses; ++i )
		due.push_back( frameStart );
	if( in.stepOn == StepOn::Onset && in.onset )
		due.push_back( frameStart );

	const double tempo    = in.bpm > 1.0 ? in.bpm : 120.0;
	const double phaseNow = in.hostBeat ? std::clamp( in.barPhase, 0.0, 1.0 ) : std::fmod( now * tempo / 240.0, 1.0 );
	double grid           = 0.0;
	switch( in.stepOn )
	{
	case StepOn::FourABeat: grid = 1.0 / 16.0; break;
	case StepOn::TwoABeat: grid = 1.0 / 8.0; break;
	case StepOn::Beat: grid = 0.25; break;
	case StepOn::TwoBeats: grid = 0.5; break;
	case StepOn::Bar: grid = 1.0; break;
	default: break;
	}
	if( grid > 0.0 && lastPhase >= 0.0 && dt > 0.0 )
	{
		double p0 = lastPhase, p1 = phaseNow;
		if( p1 < p0 )
			p1 += 1.0;
		if( p1 > p0 )
		{
			double boundary = ( std::floor( p0 / grid + 1e-9 ) + 1.0 ) * grid;
			for( int n = 0; boundary <= p1 + 1e-12 && n < kMaxStepsPerFrame; ++n, boundary += grid )
			{
				const double inside = ( hooks & kHookStepAtFrameStart ) != 0 ? 0.0 : ( boundary - p0 ) / ( p1 - p0 );
				due.push_back( frameStart + inside * dt );
			}
		}
	}
	lastPhase = phaseNow;

	std::sort( due.begin(), due.end() );
	if( in.pattern != Pattern::Hold )
		for( double at : due )
		{
			++steps;
			stepTimes.push_back( at );
			retarget( at, in, false );
		}
	frameEnd = now;
}

} // namespace haze
