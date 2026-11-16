//===========================================================================
// The checks. Each sets every control it depends on (honeydew's lesson), and
// every GL check runs at each raster in kRasters.
//===========================================================================

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.SetHooksForTest( perturb.hooks );
	rig.plugin.SetMotionHooksForTest( perturb.motionHooks );
	rig.plugin.SetUnprimedForTest( perturb.unprimed );
	return true;
}

/// An optics rig: linear radiance out, one view, no stir, every sample,
/// Exposure 0, an open white gobo.
void opticsSetup( Rig& rig, const Perturb& perturb, int view )
{
	rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookRaw | view );
	rig.Set( PT_STIR, 0.0f );
	rig.Set( PT_QUALITY, static_cast< float >( Quality::High ) );
	rig.Set( PT_EXPOSURE, ParamFromExposure( 0.0 ) );
	rig.Set( PT_GOBO, static_cast< float >( Gobo::Open ) );
	rig.Set( PT_COLOUR, static_cast< float >( Colour::White ) );
}

TestRig beamRig( const Vec3& F, const Vec3& d, const Vec3& eye, const Vec3& target, double tanHalfV )
{
	TestRig r;
	r.camera          = true;
	r.eye             = eye;
	r.target          = target;
	r.tanHalfV        = tanHalfV;
	r.fixtures        = true;
	r.count           = 1;
	r.position[ 0 ]   = F;
	r.direction[ 0 ]  = d;
	return r;
}

/// The mean of channel c over the central block of +-fx, +-fy of the frame.
double blockMean( const Floats& px, int w, int h, double fx, double fy, int c = 0 )
{
	const int x0 = static_cast< int >( std::floor( w * ( 0.5 - fx ) ) ), x1 = static_cast< int >( std::ceil( w * ( 0.5 + fx ) ) );
	const int y0 = static_cast< int >( std::floor( h * ( 0.5 - fy ) ) ), y1 = static_cast< int >( std::ceil( h * ( 0.5 + fy ) ) );
	double sum = 0.0;
	int n      = 0;
	for( int y = std::max( y0, 0 ); y < std::min( y1, h ); ++y )
		for( int x = std::max( x0, 0 ); x < std::min( x1, w ); ++x )
		{
			sum += px[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ];
			++n;
		}
	return n > 0 ? sum / n : 0.0;
}

double hg( double c, double g )
{
	const double d = 1.0 + g * g - 2.0 * g * c;
	return ( 1.0 - g * g ) / ( 4.0 * kPi * d * std::sqrt( d ) );
}

double lensRadius()
{
	return 0.5 * fixture::kLensDiameter;
}

//===========================================================================
// --phase: the beam's radiance against the viewing angle is the phase
// function, recovered from the picture.
//===========================================================================
int runPhase( const Perturb& perturb )
{
	std::printf( "\n=== phase: a 2.5 deg beam crossed at angle theta: L sin-free over the cone's chord integral gives Henyey-Greenstein's p "
	             "(bound 3%%)\n" );
	const double thetas[] = { 20, 30, 45, 60, 90, 120, 150, 160 };
	const double gs[]     = { 0.0, 0.5, 0.7, 0.9 };
	const double sigma = 0.001, Dc = 6.0, a = lensRadius();
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		opticsSetup( rig, perturb, shaders::kHookAirOnly );
		rig.Set( PT_ZOOM, ParamFromZoom( 2.5 ) );
		rig.Set( PT_HAZE, ParamFromHaze( sigma ) );
		double worst = 0.0;
		std::string worstAt;
		int measured = 0;
		for( double r0 : { 6.0, 12.0 } )
		{
			rig.Set( PT_FOCUS, ParamFromFocus( r0 ) );
			for( double g : gs )
			{
				rig.Set( PT_ANISOTROPY, ParamFromAnisotropy( g ) );
				for( double th : thetas )
				{
					const double t  = th * kPi / 180.0;
					const Vec3 F    = { -10.0, 6.0, 0.0 }, d = { 1.0, 0.0, 0.0 };
					const Vec3 X0   = F + d * r0;
					const Vec3 eye  = X0 + Vec3 { std::cos( t ), 0.0, std::sin( t ) } * Dc;
					rig.plugin.SetRigForTest( beamRig( F, d, eye, X0, std::tan( 1.0 * kPi / 180.0 ) ) );
					if( !rig.Render( 1 ) )
						return 1;
					const FrameData& fd = rig.plugin.LastFrame();
					const double L      = blockMean( rig.Output(), raster.w, raster.h, 0.02, 0.02 );
					//The cone's chord through X0 along the eye's ray, and the
					//inverse square integrated over it in closed form.
					const double ta = fd.tanAlpha;
					const double s1 = ta * r0 / ( std::sin( t ) + ta * std::cos( t ) );
					const double s2 = -ta * r0 / ( std::sin( t ) - ta * std::cos( t ) );
					const double k  = std::sqrt( r0 * r0 * std::sin( t ) * std::sin( t ) + a * a );
					const double J  = ( std::atan( ( s1 - r0 * std::cos( t ) ) / k ) - std::atan( ( s2 - r0 * std::cos( t ) ) / k ) ) / k;
					const double T0 = std::exp( -sigma * ( r0 + Dc ) );
					const double p  = L / ( sigma * fd.intensity / kReferenceLuminance * J * T0 );
					const double err = p / hg( std::cos( t ), g ) - 1.0;
					++measured;
					if( std::fabs( err ) > std::fabs( worst ) || !std::isfinite( err ) )
					{
						worst   = std::isfinite( err ) ? err : 1e9;
						worstAt = fmt( "r %.0f m, g %.1f, theta %.0f: p %.5g against %.5g", r0, g, th, p, hg( std::cos( t ), g ) );
					}
				}
			}
		}
		Check( measured == 64 && std::fabs( worst ) < 0.03,
		       fmt( "%dx%d: %d angles x g x distance, worst %+.2f%% (%s)", raster.w, raster.h, measured, 100.0 * worst, worstAt.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --extinction: Beer-Lambert, from the picture.
//===========================================================================
double slopeOf( const std::vector< double >& x, const std::vector< double >& y, double* residual = nullptr )
{
	const size_t n = x.size();
	double mx = 0.0, my = 0.0;
	for( size_t i = 0; i < n; ++i )
	{
		mx += x[ i ] / n;
		my += y[ i ] / n;
	}
	double sxy = 0.0, sxx = 0.0;
	for( size_t i = 0; i < n; ++i )
	{
		sxy += ( x[ i ] - mx ) * ( y[ i ] - my );
		sxx += ( x[ i ] - mx ) * ( x[ i ] - mx );
	}
	const double slope = sxy / sxx;
	if( residual )
	{
		*residual = 0.0;
		for( size_t i = 0; i < n; ++i )
			*residual = std::max( *residual, std::fabs( y[ i ] - ( my + slope * ( x[ i ] - mx ) ) ) );
	}
	return slope;
}

int runExtinction( const Perturb& perturb )
{
	std::printf( "\n=== extinction: ln L against sigma is a line whose slope is minus the path the light travels\n" );
	const double sigmas[] = { 0.001, 0.003, 0.01, 0.03, 0.1 };
	for( const Raster& raster : kRasters )
	{
		//(a) A beam square to the back wall, the spot seen from behind the lens: surfaces only.
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			opticsSetup( rig, perturb, shaders::kHookSurfacesOnly );
			rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
			rig.Set( PT_FOCUS, ParamFromFocus( 10.0 ) );
			const Vec3 F = { 0.0, 4.0, 2.0 }, spot = { 0.0, 4.0, -8.0 }, eye = { 3.0, 4.0, 6.0 };
			rig.plugin.SetRigForTest( beamRig( F, { 0.0, 0.0, -1.0 }, eye, spot, 0.05 ) );
			std::vector< double > xs, ys;
			for( double s : sigmas )
			{
				rig.Set( PT_HAZE, ParamFromHaze( s ) );
				if( !rig.Render( 1 ) )
					return 1;
				const double L = blockMean( rig.Output(), raster.w, raster.h, 1.0 / raster.w, 1.0 / raster.h );
				xs.push_back( rig.plugin.LastFrame().sigma );
				ys.push_back( std::log( std::max( L, 1e-30 ) ) );
			}
			const Vec3 toEye = eye - spot;
			const double path = 10.0 + std::sqrt( dot( toEye, toEye ) );
			double residual   = 0.0;
			const double slope = slopeOf( xs, ys, &residual );
			Check( std::fabs( -slope / path - 1.0 ) < 1e-4 && residual < 1e-4,
			       fmt( "%dx%d, the wall: slope %.6f m against the light's path %.6f m (lens to wall 10 + wall to eye %.6f): %.2e relative, "
			            "worst residual %.1e",
			            raster.w, raster.h, -slope, path, path - 10.0, -slope / path - 1.0, residual ) );
		}
		//(b) The air, side-on: L / sigma against sigma.
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			opticsSetup( rig, perturb, shaders::kHookAirOnly );
			rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
			rig.Set( PT_FOCUS, ParamFromFocus( 10.0 ) );
			rig.Set( PT_ANISOTROPY, ParamFromAnisotropy( 0.7 ) );
			const Vec3 F = { -10.0, 6.0, 0.0 }, X0 = { 0.0, 6.0, 0.0 }, eye = { 0.0, 6.0, 8.0 };
			rig.plugin.SetRigForTest( beamRig( F, { 1.0, 0.0, 0.0 }, eye, X0, std::tan( 1.0 * kPi / 180.0 ) ) );
			std::vector< double > xs, ys;
			for( double s : sigmas )
			{
				rig.Set( PT_HAZE, ParamFromHaze( s ) );
				if( !rig.Render( 1 ) )
					return 1;
				const double L = blockMean( rig.Output(), raster.w, raster.h, 0.02, 0.02 );
				xs.push_back( rig.plugin.LastFrame().sigma );
				ys.push_back( std::log( std::max( L, 1e-30 ) / rig.plugin.LastFrame().sigma ) );
			}
			const double path  = 10.0 + 8.0;
			const double slope = slopeOf( xs, ys );
			Check( std::fabs( -slope / path - 1.0 ) < 0.01,
			       fmt( "%dx%d, the air: slope %.4f m against lens to crossing + crossing to eye %.1f m (%+.2f%%); brightest at sigma 1/L = %.4f /m",
			            raster.w, raster.h, -slope, path, 100.0 * ( -slope / path - 1.0 ), -1.0 / slope ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --servo (no GL): the motors' plans, against closed forms and a phase-plane
// integration that knows nothing of them.
//===========================================================================
double closedRest( double D, double V, double A )
{
	D = std::fabs( D );
	return D >= V * V / A ? D / V + V / A : 2.0 * std::sqrt( D / A );
}

/// Time to stop at X from ( x0, v0 ) under the bang-bang law, in steps of
/// dt: accelerate toward X (to V), and once the braking distance reaches what
/// is left, brake to rest. A feedback law integrated step by step, not the
/// planner's algebra; it lands within a step's travel of X and its time is
/// good to about a step.
double phasePlane( double x0, double v0, double X, double V, double A, double dt )
{
	double x = x0, v = v0, t = 0.0;
	for( long long i = 0; i < 400000000LL; ++i )
	{
		const double rem = X - x;
		const double s   = rem >= 0.0 ? 1.0 : -1.0;
		const double u   = s * v;
		if( u > 0.0 && u * u / ( 2.0 * A ) >= std::fabs( rem ) )
		{
			//Braking, latched: brake to rest, then judge where it stopped.
			const double stop = t + u / A;
			const double at   = x + s * u * u / ( 2.0 * A );
			if( std::fabs( X - at ) <= 4.0 * std::max( V, u ) * dt )
				return stop;
			x = at;//stopped well past X: a reversal, from rest there
			v = 0.0;
			t = stop;
			continue;
		}
		double a = 0.0;
		if( u > V )
			a = -s * A;
		else if( u < V )
			a = s * A;
		double vn = v + a * dt;
		if( a == s * A && s * vn > V )
			vn = s * V;
		x += 0.5 * ( v + vn ) * dt;
		v = vn;
		t += dt;
	}
	return -1.0;
}

int runServo( const Perturb& perturb )
{
	std::printf( "\n=== servo: the Mythos's limits, the time-optimal plan, vectorial sync, inverse kinematics\n" );
	const fixture::Limits p1 = fixture::PanLimits(), t1 = fixture::TiltLimits();
	{
		const double tp = Plan( 0.0, 0.0, fixture::kPanRange, p1.v, p1.a ).Duration();
		const double tt = Plan( 0.0, 0.0, fixture::kTiltRange, t1.v, t1.a ).Duration();
		//The manual's numbers, typed here: comparing against fixture::kPanTime would be circular.
		Check( std::fabs( tp - 3.517 ) < 1e-12 && std::fabs( tt - 2.180 ) < 1e-12,
		       fmt( "full range at Fast: pan 540 deg in %.12f s (manual 3.517), tilt 244 deg in %.12f s (manual 2.180); V %.2f / %.2f deg/s, "
		            "A %.2f / %.2f deg/s^2",
		            tp, tt, p1.v, t1.v, p1.a, t1.a ) );
	}
	//Rest to rest, through a Yoke (so the motion hooks reach it), one axis at a time.
	int bad = 0, cases = 0;
	double worstClosed = 0.0, worstPhase = 0.0;
	std::string what;
	for( double speed : { 0.05, 1.0, 1.5 } )
		for( int axis = 0; axis < 2; ++axis )
		{
			const fixture::Limits l = axis == 0 ? fixture::PanLimits( speed ) : fixture::TiltLimits( speed );
			for( double D : { 0.5, 5.0, 40.0, 120.0, 179.9, 180.0, 181.0, 240.0 } )
			{
				Yoke y;
				y.Reset( 0.0, 0.0 );
				y.Retarget( 0.0, axis == 0 ? D : 0.0, axis == 1 ? D : 0.0, fixture::PanLimits( speed ), fixture::TiltLimits( speed ),
				            perturb.motionHooks | kHookNoSync );
				const Profile& pr = axis == 0 ? y.PanPlan() : y.TiltPlan();
				const double T    = pr.Duration();
				const double Tc   = closedRest( D, l.v, l.a );
				const double Tn   = phasePlane( 0.0, 0.0, D, l.v, l.a, 1e-5 );
				//Where the plan's own segments leave it, integrated here.
				double x = pr.x0, v = pr.v0, vmax = 0.0, amax = 0.0;
				bool reverses = false;
				for( const Profile::Segment& s : pr.seg )
				{
					x += v * s.duration + 0.5 * s.accel * s.duration * s.duration;
					v += s.accel * s.duration;
					vmax = std::max( vmax, std::fabs( v ) );
					if( s.duration > 0.0 )
						amax = std::max( amax, std::fabs( s.accel ) );
					reverses |= v < -1e-9;
				}
				++cases;
				worstClosed = std::max( worstClosed, std::fabs( T - Tc ) );
				worstPhase  = std::max( worstPhase, std::fabs( T - Tn ) );
				const bool ok = std::fabs( T - Tc ) < 1e-9 * std::max( 1.0, Tc ) && std::fabs( T - Tn ) < 5e-5 && std::fabs( x - D ) < 1e-9
				                && std::fabs( v ) < 1e-9 && vmax <= l.v * ( 1.0 + 1e-12 ) && amax <= l.a * ( 1.0 + 1e-12 ) && !reverses;
				if( !ok )
				{
					++bad;
					if( what.size() < 300 )
						what += fmt( " [%s %.1f deg at %.0f%%: %.6f s, closed %.6f, phase plane %.6f, lands %.2e, vmax %.1f/%.1f, amax %.1f/%.1f]",
						             axis == 0 ? "pan" : "tilt", D, speed * 100, T, Tc, Tn, x - D, vmax, l.v, amax, l.a );
				}
			}
		}
	Check( bad == 0, fmt( "%d rest-to-rest moves (0.5-240 deg, both axes, 5/100/150%%): plan = closed form within %.1e s, = phase plane "
	                      "(5 steps of 1e-5 s) within %.1e s, lands exactly, never past V or A, never reverses (%d wrong%s)",
	                      cases, worstClosed, worstPhase, bad, what.c_str() ) );

	//Synced: both axes arrive together, at the slower axis's closed form.
	{
		int wrong   = 0;
		double skew = 0.0;
		const double moves[][ 2 ] = { { 90.0, 10.0 }, { 10.0, 90.0 }, { 200.0, 120.0 }, { 3.0, 60.0 }, { 45.0, 45.0 } };
		for( const auto& m : moves )
		{
			Yoke y;
			y.Reset( 0.0, 0.0 );
			y.Retarget( 0.0, m[ 0 ], m[ 1 ], p1, t1, perturb.motionHooks );
			const double tp = y.PanPlan().Duration(), tt = y.TiltPlan().Duration();
			const double want = std::max( closedRest( m[ 0 ], p1.v, p1.a ), closedRest( m[ 1 ], t1.v, t1.a ) );
			skew              = std::max( skew, std::fabs( tp - tt ) );
			wrong += std::fabs( tp - tt ) > 1e-9 || std::fabs( std::max( tp, tt ) - want ) > 1e-9;
		}
		Check( wrong == 0, fmt( "vectorial: 5 pan/tilt moves arrive together (worst skew %.1e s) at the slower axis's closed form (%d wrong)", skew, wrong ) );

		//Rest to rest the faster axis is its own move slowed in time by
		//k = T_fast / T_slow: every segment 1/k as long, its acceleration k^2.
		int misshapen = 0;
		double worstShape = 0.0;
		for( const auto& m : moves )
		{
			Yoke y;
			y.Reset( 0.0, 0.0 );
			y.Retarget( 0.0, m[ 0 ], m[ 1 ], p1, t1, perturb.motionHooks );
			const Profile own[ 2 ] = { Plan( 0.0, 0.0, m[ 0 ], p1.v, p1.a ), Plan( 0.0, 0.0, m[ 1 ], t1.v, t1.a ) };
			const int fast         = own[ 0 ].Duration() < own[ 1 ].Duration() ? 0 : 1;
			const double k         = own[ fast ].Duration() / own[ 1 - fast ].Duration();
			const Profile& got     = fast == 0 ? y.PanPlan() : y.TiltPlan();
			for( int sgm = 0; sgm < 3; ++sgm )
			{
				const double dd = std::fabs( got.seg[ sgm ].duration - own[ fast ].seg[ sgm ].duration / k );
				const double da = own[ fast ].seg[ sgm ].duration > 0.0 ? std::fabs( got.seg[ sgm ].accel - own[ fast ].seg[ sgm ].accel * k * k ) / std::max( 1.0, std::fabs( own[ fast ].seg[ sgm ].accel ) ) : 0.0;
				worstShape = std::max( worstShape, std::max( dd, da ) );
			}
		}
		misshapen = worstShape > 1e-9;
		Check( misshapen == 0, fmt( "vectorial: the faster axis is its own move slowed by k = T_fast / T_slow (segments x 1/k, acceleration x k^2), worst %.1e", worstShape ) );
	}

	//A retarget mid-move: against the phase plane from the same ( x0, v0 ).
	{
		int wrong   = 0;
		double gap  = 0.0;
		//The last: braking for 120 at 0.8 s it cannot stop short of 112 (its
		//stopping point is 120), so it overshoots and comes back.
		const double cases2[][ 3 ] = { { 120.0, 0.4, -30.0 }, { 120.0, 0.4, 200.0 }, { 60.0, 0.3, 61.0 }, { 300.0, 1.0, 100.0 }, { 120.0, 0.8, 112.0 } };
		for( const auto& c : cases2 )
		{
			Yoke y;
			y.Reset( 0.0, 0.0 );
			y.Retarget( 0.0, c[ 0 ], 0.0, p1, t1, perturb.motionHooks | kHookNoSync );
			double x = 0.0, tl = 0.0, v = 0.0, tv = 0.0;
			y.At( c[ 1 ], x, tl, &v, &tv );
			y.Retarget( c[ 1 ], c[ 2 ], 0.0, p1, t1, perturb.motionHooks | kHookNoSync );
			const double T  = y.PanPlan().Duration();
			const double Tn = phasePlane( x, v, c[ 2 ], p1.v, p1.a, 1e-5 );
			gap             = std::max( gap, std::fabs( T - Tn ) );
			double xe = 0.0, ve = 0.0;
			y.PanPlan().At( T - 1e-12, xe, ve );
			wrong += std::fabs( T - Tn ) > 5e-5 || std::fabs( xe - c[ 2 ] ) > 1e-6;
		}
		Check( wrong == 0, fmt( "5 retargets mid-move (one reversing, one past the target already, one too fast to stop short of it): the plan's "
		                        "time = the phase plane's from the same (x, v) within %.1e s, and it lands (%d wrong)", gap, wrong ) );
	}

	//Inverse kinematics.
	{
		int wrong     = 0;
		double worstD = 0.0;
		for( const Vec3& home : { Vec3 { 0.0, -1.0, 0.0 }, Vec3 { 0.0, 1.0, 0.0 } } )
			for( int i = 0; i < 2000; ++i )
			{
				const double z  = std::cos( std::min( 121.0, 121.0 * hash01( static_cast< uint32_t >( i ), 1 ) ) * kPi / 180.0 );
				const double ph = 2.0 * kPi * hash01( static_cast< uint32_t >( i ), 2 );
				const double s  = std::sqrt( std::max( 0.0, 1.0 - z * z ) );
				const Vec3 lean = { std::sin( ph ), 0.0, std::cos( ph ) };
				const Vec3 dir  = normalise( home * z + lean * s );
				double pan = 0.0, tilt = 0.0;
				Aim( home, dir, 400.0 * ( hash01( static_cast< uint32_t >( i ), 3 ) - 0.5 ), 0.0, pan, tilt );
				const Vec3 back = fixture::Direction( home, pan, tilt );
				const Vec3 e    = back - dir;
				worstD          = std::max( worstD, std::sqrt( dot( e, e ) ) );
				wrong += std::sqrt( dot( e, e ) ) > 1e-12 || std::fabs( pan ) > fixture::kPanLimit + 1e-9 || std::fabs( tilt ) > fixture::kTiltLimit + 1e-9;
			}
		double pan = 0.0, tilt = 0.0;
		Aim( { 0.0, -1.0, 0.0 }, fixture::Direction( { 0.0, -1.0, 0.0 }, 255.0, 40.0 ), 260.0, 40.0, pan, tilt );
		const bool nearest = std::fabs( pan - 255.0 ) < 1e-9 && std::fabs( tilt - 40.0 ) < 1e-9;
		Check( wrong == 0 && nearest, fmt( "4000 directions inside the tilt range round-trip (worst %.1e) inside +-270/+-122; from pan 260 a target at "
		                                   "255 is taken at %.3f, not round the back",
		                                   worstD, pan ) );
	}
	return Verdict();
}

//===========================================================================
// --late (no GL): a fast chase cannot keep up.
//===========================================================================
struct ChaseRun
{
	std::vector< double > steps;
	std::vector< double > arrivals;///< each step's planned arrival
	double panAt = 0.0;
};

ChaseRun runChase( double fps, double bpm, double seconds, double probe, Pattern pattern, StepOn stepOn, int motionHooks, int count = 1,
                   bool hostBeat = true )
{
	Chase chase;
	chase.SetHooksForTest( motionHooks );
	ChaseRun out;
	const int frames = static_cast< int >( std::lround( seconds * fps ) );
	bool probed      = false;
	for( int f = 0; f <= frames; ++f )
	{
		const double now = f / fps;
		ChaseInputs in;
		in.pattern  = pattern;
		in.stepOn   = stepOn;
		in.bpm      = bpm;
		in.barPhase = std::fmod( now * bpm / 240.0, 1.0 );
		in.hostBeat = hostBeat;
		in.spread   = 30.0;
		in.speed    = 1.0;
		in.count    = count;
		chase.Advance( now, f == 0 ? 0.0 : 1.0 / fps, in );
		for( double t : chase.StepTimes() )
		{
			out.steps.push_back( t );
			out.arrivals.push_back( chase.YokeOf( 0 ).ArrivalTime() );
		}
		if( !probed && now >= probe )
		{
			double pan = 0.0, tilt = 0.0;
			chase.YokeOf( 0 ).At( probe, pan, tilt );
			out.panAt = pan;
			probed    = true;
		}
	}
	return out;
}

int runLate( const Perturb& perturb )
{
	const double D    = 2.0 * std::atan( 2.0 ) * 180.0 / kPi;
	const double need = closedRest( D, fixture::PanLimits().v, fixture::PanLimits().a );
	std::printf( "\n=== late: Sweep swings pan 2 atan( 2 ) = %.2f deg a step (tilt still); from rest at full speed that takes 2 sqrt( D / A ) = %.4f s, "
	             "so the chase keeps up to 60 / %.4f = %.2f BPM and no further\n",
	             D, need, need, 60.0 / need );
	for( double bpm : { 40.0, 41.0, 120.0 } )
	{
		const ChaseRun run = runChase( 60.0, bpm, 12.0, 0.0, Pattern::Sweep, StepOn::Beat, perturb.motionHooks );
		const double beat  = 60.0 / bpm;
		int arrived = 0;
		double worst = 0.0;
		for( size_t i = 1; i + 1 < run.steps.size(); ++i )
		{
			const double move = run.arrivals[ i ] - run.steps[ i ];
			if( std::getenv( "HZ_DEBUG" ) )
				std::printf( "    step %zu at %.4f: move %.4f s, next step in %.4f\n", i, run.steps[ i ], move, run.steps[ i + 1 ] - run.steps[ i ] );
			if( run.arrivals[ i ] <= run.steps[ i + 1 ] + 1e-12 )
				++arrived;
			if( i >= 2 )
				worst = std::max( worst, std::fabs( move - need ) );
		}
		const int counted = static_cast< int >( run.steps.size() ) - 2;
		bool ok           = counted > 3;
		std::string line;
		if( bpm == 40.0 )
		{
			ok &= arrived == counted && worst < 1e-9;
			line = fmt( "%.0f BPM (%.3f s a beat): %d of %d steps land before the next, each move %.4f s from rest (closed form %.4f, worst %.1e)", bpm, beat,
			            arrived, counted, need, need, worst );
		}
		else if( bpm == 41.0 )
		{
			ok &= arrived == 0;
			line = fmt( "%.0f BPM (%.3f s a beat, shorter than the move): %d of %d steps land before the next", bpm, beat, arrived, counted );
		}
		else
		{
			ok &= 4 * arrived < counted;
			line = fmt( "%.0f BPM (%.3f s a beat): %d of %d steps land before the next (fewer than a quarter: the head arrives only where it "
			            "overshot toward the mark it was sent back to)",
			            bpm, beat, arrived, counted );
		}
		Check( ok, line );
	}
	{
		double pans[ 4 ];
		const double rates[] = { 24.0, 30.0, 60.0, 240.0 };
		for( int i = 0; i < 4; ++i )
			pans[ i ] = runChase( rates[ i ], 128.0, 4.0, 3.217, Pattern::Sweep, StepOn::Beat, perturb.motionHooks ).panAt;
		double spread = 0.0;
		for( int i = 1; i < 4; ++i )
			spread = std::max( spread, std::fabs( pans[ i ] - pans[ 0 ] ) );
		Check( spread < 1e-9, fmt( "128 BPM (its beats fall inside frames), pan at t = 3.217 s at 24, 30, 60, 240 fps: %.9f %.9f %.9f %.9f deg (spread %.1e)", pans[ 0 ], pans[ 1 ], pans[ 2 ],
		                           pans[ 3 ], spread ) );
	}
	return Verdict();
}

//===========================================================================
// --chase (no GL): when steps happen.
//===========================================================================
int runChaseCheck( const Perturb& perturb )
{
	std::printf( "\n=== chase: steps land on the beat's crossings, inside the frame, at any frame rate; presses and onsets step once each\n" );
	for( double fps : { 30.0, 240.0 } )
	{
		const double beat  = 60.0 / 128.0;
		const ChaseRun run = runChase( fps, 128.0, 4.0, 0.0, Pattern::Fan, StepOn::Beat, perturb.motionHooks, 6 );
		double worst       = 0.0;
		for( size_t i = 0; i < run.steps.size(); ++i )
			worst = std::max( worst, std::fabs( run.steps[ i ] - beat * std::round( run.steps[ i ] / beat ) ) );
		Check( run.steps.size() == 8 && worst < 1e-9,
		       fmt( "host beat at 128 BPM, Beat, %.0f fps: %zu steps in 4 s (the beat at %.5f s, inside frames), worst %.1e s off it", fps, run.steps.size(),
		            beat, worst ) );
	}
	{
		const ChaseRun run = runChase( 60.0, 120.0, 4.0, 0.0, Pattern::Fan, StepOn::Bar, perturb.motionHooks, 6, false );
		double worst       = 0.0;
		for( double t : run.steps )
			worst = std::max( worst, std::fabs( t - 2.0 * std::round( t / 2.0 ) ) );
		Check( run.steps.size() == 2 && worst < 1e-9, fmt( "no host beat: its own clock at 120 BPM, Bar: %zu steps in 4 s, worst %.1e s off", run.steps.size(), worst ) );
	}
	{
		Chase chase;
		chase.SetHooksForTest( perturb.motionHooks );
		long long held = 0;
		for( int f = 0; f <= 120; ++f )
		{
			ChaseInputs in;
			in.stepOn  = StepOn::StepOnly;
			in.presses = ( f == 10 || f == 50 || f == 90 ) ? 1 : 0;
			in.onset   = f % 7 == 0;
			in.count   = 6;
			chase.Advance( f / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, in );
		}
		Chase onsets;
		for( int f = 0; f <= 120; ++f )
		{
			ChaseInputs in;
			in.stepOn = StepOn::Onset;
			in.onset  = f == 20 || f == 40 || f == 41 || f == 100;
			in.count  = 6;
			onsets.Advance( f / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, in );
		}
		Chase hold;
		for( int f = 0; f <= 120; ++f )
		{
			ChaseInputs in;
			in.pattern = Pattern::Hold;
			in.presses = f == 30 ? 1 : 0;
			in.count   = 6;
			in.bpm     = 120.0;
			hold.Advance( f / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, in );
			held += hold.Steps();
		}
		Check( chase.Steps() == 3 && onsets.Steps() == 4 && held == 0,
		       fmt( "Step Only with 3 presses and onsets flowing: %lld steps; Onset with 4: %lld; Hold, beats and a press: %lld", chase.Steps(), onsets.Steps(),
		            held ) );
	}
	return Verdict();
}

//===========================================================================
// --aim: the spot square to a surface, seen from the axis, is centred.
//===========================================================================
int runAim( const Perturb& perturb )
{
	std::printf( "\n=== aim: a beam square to the back wall and to the floor, seen from its own axis behind the lens: the spot's centroid is the "
	             "frame's centre (symmetry; 0.05 px)\n" );
	for( const Raster& raster : kRasters )
	{
		struct Case
		{
			Vec3 F, d, eye, target;
			const char* name;
		};
		const Case cases[] = {
			{ { 2.0, 5.0, 0.0 }, { 0.0, 0.0, -1.0 }, { 2.0, 5.0, 6.0 }, { 2.0, 5.0, -8.0 }, "back wall" },
			{ { -3.0, 7.0, -4.0 }, { 0.0, -1.0, 0.0 }, { -3.0, 11.0, -4.0 }, { -3.0, 0.0, -4.0 }, "floor" },
		};
		for( const Case& c : cases )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			opticsSetup( rig, perturb, shaders::kHookSurfacesOnly );
			rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
			rig.Set( PT_HAZE, ParamFromHaze( 0.001 ) );
			rig.plugin.SetRigForTest( beamRig( c.F, c.d, c.eye, c.target, 0.12 ) );
			const Vec3 throwV = c.target - c.F;
			rig.Set( PT_FOCUS, ParamFromFocus( std::sqrt( dot( throwV, throwV ) ) ) );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats px = rig.Output();
			double sx = 0.0, sy = 0.0, sw = 0.0;
			for( int y = 0; y < raster.h; ++y )
				for( int x = 0; x < raster.w; ++x )
				{
					const double v = px[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
					sx += v * ( x + 0.5 );
					sy += v * ( y + 0.5 );
					sw += v;
				}
			const double cx = sx / sw - 0.5 * raster.w, cy = sy / sw - 0.5 * raster.h;
			Check( sw > 0.0 && std::fabs( cx ) < 0.05 && std::fabs( cy ) < 0.05,
			       fmt( "%dx%d, %s: centroid %+.4f, %+.4f px from the centre", raster.w, raster.h, c.name, cx, cy ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --lens: looking into the lens, the fixture's own luminance.
//===========================================================================
int runLens( const Perturb& perturb )
{
	std::printf( "\n=== lens: seen down the beam, the lens is a disc of luminance I0 / ( pi a^2 ), dimmed by the haze between it and the eye\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		opticsSetup( rig, perturb, shaders::kHookSurfacesOnly );
		rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
		rig.Set( PT_FOCUS, ParamFromFocus( 9.0 ) );
		double worst = 0.0, L = 0.0, want = 0.0;
		for( double sigma : { 0.001, 0.03 } )
		{
			rig.Set( PT_HAZE, ParamFromHaze( sigma ) );
			//The lens ( radius 7.85 cm ) seen from 6 m on its axis fills a frame of tanHalfV 0.004.
			rig.plugin.SetRigForTest( beamRig( { 0.0, 6.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 6.0, 6.0 }, { 0.0, 6.0, 0.0 }, 0.004 ) );
			if( !rig.Render( 1 ) )
				return 1;
			const FrameData& fd = rig.plugin.LastFrame();
			const double a      = lensRadius();
			want                = fd.intensity / kReferenceLuminance / ( kPi * a * a ) * std::exp( -fd.sigma * 6.0 );
			for( int c = 0; c < 3; ++c )
			{
				L     = blockMean( rig.Output(), raster.w, raster.h, 0.4, 0.4, c );
				worst = std::max( worst, std::fabs( L / want - 1.0 ) );
			}
		}
		//I0 from the Mythos's 24,000 lm (typed here) spread evenly over the beam's solid angle.
		const double alpha = std::atan( rig.plugin.LastFrame().tanAlpha );
		const double I0    = 24000.0 / ( 2.0 * kPi * ( 1.0 - std::cos( alpha ) ) );
		const double iErr  = rig.plugin.LastFrame().intensity / I0 - 1.0;
		Check( worst < 1e-4 && std::fabs( iErr ) < 1e-12,
		       fmt( "%dx%d: %.6g against I0 / ( pi a^2 ) exp( -6 sigma ) / L_ref = %.6g at sigma 0.03, worst %.1e relative; I0 %.0f cd = 24,000 lm over "
		            "2 pi ( 1 - cos %.2f deg ) (%.1e)",
		            raster.w, raster.h, L, want, worst, rig.plugin.LastFrame().intensity, alpha * 180.0 / kPi, iErr ) );
	}
	return Verdict();
}

//===========================================================================
// --focus: the gobo is sharpest at the focus distance.
//===========================================================================
/// The 10-90% width, in pixels, of the spot's edge along the centre row's right half.
double edgeWidth( const Floats& px, int w, int h )
{
	const int y = h / 2;
	std::vector< double > row( static_cast< size_t >( w ) );
	for( int x = 0; x < w; ++x )
		row[ static_cast< size_t >( x ) ] = 0.5 * ( px[ ( static_cast< size_t >( y ) * w + x ) * 4 ] + px[ ( static_cast< size_t >( y - 1 ) * w + x ) * 4 ] );
	const double top = row[ static_cast< size_t >( w / 2 ) ];
	auto crossing    = [ & ]( double level ) {
        for( int x = w / 2; x + 1 < w; ++x )
            if( row[ x ] >= level && row[ x + 1 ] < level )
                return x + 0.5 + ( row[ x ] - level ) / ( row[ x ] - row[ x + 1 ] );
        return static_cast< double >( w );
	};
	return crossing( 0.1 * top ) - crossing( 0.9 * top );
}

int runFocus( const Perturb& perturb )
{
	std::printf( "\n=== focus: the spot's edge on a wall 10 m away is sharpest at Focus 10 m and blurs as |1/L - 1/f|; in the air, a fine "
	             "grating's contrast along the beam peaks at r = f\n" );
	const double L = 10.0, focuses[] = { 4.0, 6.0, 8.0, 9.0, 10.0, 11.0, 12.5, 15.0, 20.0, 30.0 };
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		opticsSetup( rig, perturb, shaders::kHookSurfacesOnly );
		rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
		rig.Set( PT_HAZE, ParamFromHaze( 0.001 ) );
		rig.plugin.SetRigForTest( beamRig( { 0.0, 4.0, 2.0 }, { 0.0, 0.0, -1.0 }, { 0.0, 4.0, 8.0 }, { 0.0, 4.0, -8.0 }, 0.1 ) );
		std::vector< double > widths, blur;
		double atL = 0.0, minW = 1e9, minF = 0.0;
		for( double f : focuses )
		{
			rig.Set( PT_FOCUS, ParamFromFocus( f ) );
			if( !rig.Render( 1 ) )
				return 1;
			const double w = edgeWidth( rig.Output(), raster.w, raster.h );
			//The blur disc on the wall, in pixels: D |1 - L/f| metres, the
			//camera 16 m away with tanHalfV 0.1 over half the frame's height.
			const double ppm = 0.5 * raster.h / ( 16.0 * 0.1 );
			widths.push_back( w );
			blur.push_back( fixture::kLensDiameter * std::fabs( 1.0 - L / f ) * ppm );
			if( f == L )
				atL = w;
			if( w < minW )
			{
				minW = w;
				minF = f;
			}
		}
		//Linear in the blur where the blur is resolved (over 2 px).
		std::vector< double > xs, ys;
		for( size_t i = 0; i < widths.size(); ++i )
			if( blur[ i ] > 2.0 )
			{
				xs.push_back( blur[ i ] );
				ys.push_back( widths[ i ] );
			}
		double residual = 0.0, r2 = 0.0;
		const double slope = xs.size() >= 3 ? slopeOf( xs, ys, &residual ) : 0.0;
		{
			double my = 0.0, ss = 0.0, sr = 0.0;
			for( double y : ys )
				my += y / ys.size();
			double mx = 0.0;
			for( double x : xs )
				mx += x / xs.size();
			for( size_t i = 0; i < xs.size(); ++i )
			{
				const double fit = my + slope * ( xs[ i ] - mx );
				ss += ( ys[ i ] - my ) * ( ys[ i ] - my );
				sr += ( ys[ i ] - fit ) * ( ys[ i ] - fit );
			}
			r2 = ss > 0.0 ? 1.0 - sr / ss : 0.0;
		}
		const double far = std::max( widths.front(), widths.back() );
		//What the slope must be: a uniform disc of diameter B spreads a step's
		//10-90% over 2uB/2 = uB, where the share of the disc past a chord at
		//u radii, ( acos u - u sqrt( 1 - u^2 ) ) / pi, is 0.1. u = 0.687.
		//Bound 5%: the shader's 16 golden-spiral taps, box-filtered at none to
		//a third of the disc, give 0.663-0.698 (3.5% either side; computed
		//from the tap pattern in AGENTS.md), and the pixel grid the rest.
		double lo = 0.0, hi = 1.0;
		for( int i = 0; i < 100; ++i )
		{
			const double u = 0.5 * ( lo + hi );
			( ( std::acos( u ) - u * std::sqrt( 1.0 - u * u ) ) / kPi > 0.1 ? lo : hi ) = u;
		}
		const double disc = 0.5 * ( lo + hi );
		Check( minF == L && far > atL + 3.0 && xs.size() >= 3 && r2 > 0.99 && std::fabs( slope / disc - 1.0 ) < 0.05,
		       fmt( "%dx%d: edge 10-90%% %.2f px at Focus 10 m (sharpest at %.1f m); %.1f px at 4 m, %.1f px at 30 m; against the blur disc's "
		            "diameter: slope %.3f against a uniform disc's %.3f (%+.1f%%, bound 5%%), R^2 %.4f over %zu focuses",
		            raster.w, raster.h, atL, minF, widths.front(), widths.back(), slope, disc, 100.0 * ( slope / disc - 1.0 ), r2, xs.size() ) );
	}

	//The air: the Over with a fine grating as its gobo, a beam seen from above.
	for( const Raster& raster : kRasters )
	{
		Floats grating( static_cast< size_t >( raster.w ) * raster.h * 4 );
		const double period = raster.h / 32.0;//8 gobo texels: 256 across the gobo's diameter
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
			{
				const float v = std::fmod( x / period, 1.0 ) < 0.5 ? 1.0f : 0.0f;
				float* o      = &grating[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = v;
				o[ 3 ]                   = 1.0f;
			}
		for( double f : { 7.0, 9.0, 12.0 } )
		{
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &grating ) )
				return 1;
			opticsSetup( rig, perturb, shaders::kHookAirOnly );
			rig.Set( PT_CLIP_AS, static_cast< float >( ClipAs::Gobo ) );
			rig.Set( PT_MIX, 1.0f );
			rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
			rig.Set( PT_HAZE, ParamFromHaze( 0.001 ) );
			rig.Set( PT_FOCUS, ParamFromFocus( f ) );
			const Vec3 F = { -12.0, 6.0, 0.0 };
			rig.plugin.SetRigForTest( beamRig( F, { 1.0, 0.0, 0.0 }, { -2.0, 11.5, 0.0 }, { -2.0, 6.0, 0.0 }, 0.92 ) );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats px = rig.Output();
			const FrameData& fd = rig.plugin.LastFrame();
			//Each column is one r along the beam; the camera looks straight down.
			const double tanH = fd.camera.tanHalfV * raster.w / raster.h;
			std::map< int, std::pair< double, int > > bins;//r bin -> contrast sum, count
			for( int x = 0; x < raster.w; ++x )
			{
				const double ndc = ( x + 0.5 ) / raster.w * 2.0 - 1.0;
				const double r   = -2.0 + ndc * tanH * 5.5 - F[ 0 ];
				if( r < 3.0 || r > 18.0 )
					continue;
				const double radius = r * fd.tanAlpha;//metres at the beam
				const double rpx    = radius / ( 5.5 * fd.camera.tanHalfV ) * 0.5 * raster.h;
				const int lo = static_cast< int >( 0.5 * raster.h - 0.5 * rpx ), hi = static_cast< int >( 0.5 * raster.h + 0.5 * rpx );
				if( hi - lo < 6 )
					continue;
				double mean = 0.0, var = 0.0;
				for( int y = lo; y < hi; ++y )
					mean += px[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ] / ( hi - lo );
				for( int y = lo; y < hi; ++y )
				{
					const double d = px[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ] - mean;
					var += d * d / ( hi - lo );
				}
				if( mean <= 0.0 )
					continue;
				auto& b = bins[ static_cast< int >( std::floor( r / 0.5 ) ) ];
				b.first += std::sqrt( var ) / mean;
				b.second += 1;
			}
			double best = -1.0, bestR = 0.0;
			for( const auto& b : bins )
			{
				const double c = b.second.first / b.second.second;
				if( c > best )
				{
					best  = c;
					bestR = ( b.first + 0.5 ) * 0.5;
				}
			}
			auto contrastAt = [ & ]( double r ) {
				const auto it = bins.find( static_cast< int >( std::floor( r / 0.5 ) ) );
				return it == bins.end() ? -1.0 : it->second.first / it->second.second;
			};
			//Where the blur stays under one texel (mip level 0): the plateau.
			const double tex  = fd.tanAlpha / 128.0 / fixture::kLensDiameter;
			const double near = 1.0 / ( 1.0 / f + tex ), farR = 1.0 / std::max( 1.0 / f - tex, 1e-6 );
			const double tol  = std::max( 0.5, std::max( f - near, farR - f ) );
			const double lowSide = contrastAt( std::max( 3.25, f - 5.0 ) ), highSide = contrastAt( std::min( 17.75, f + 5.0 ) );
			const double side    = std::max( lowSide, highSide );
			Check( std::fabs( bestR - f ) <= tol + 0.25 && side >= 0.0 && side < 0.5 * best,
			       fmt( "%dx%d, Focus %.0f m: grating contrast peaks at r %.2f m (bound %.2f: a bin and the level-0 plateau), %.3f there; %.3f at "
			            "r %.0f, %.3f at r %.0f",
			            raster.w, raster.h, f, bestR, tol + 0.25, best, lowSide, std::max( 3.25, f - 5.0 ), highSide, std::min( 17.75, f + 5.0 ) ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --gobo: the clip lands upright and unmirrored.
//===========================================================================
Floats quadrantCard( int w, int h )
{
	Floats card( static_cast< size_t >( w ) * h * 4 );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const bool top = y >= h / 2, left = x < w / 2;//rows bottom first
			float c[ 3 ] = { 1.0f, 1.0f, 1.0f };
			if( top && left )
				c[ 1 ] = c[ 2 ] = 0.0f;//red
			else if( top )
				c[ 0 ] = c[ 2 ] = 0.0f;//green
			else if( left )
				c[ 0 ] = c[ 1 ] = 0.0f;//blue
			float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			o[ 0 ] = c[ 0 ];
			o[ 1 ] = c[ 1 ];
			o[ 2 ] = c[ 2 ];
			o[ 3 ] = 1.0f;
		}
	return card;
}

int runGobo( const Perturb& perturb )
{
	std::printf( "\n=== gobo: the Over's clip on the wall and the floor reads as the clip does: red top left, green top right, blue bottom left\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats card = quadrantCard( raster.w, raster.h );
		struct Case
		{
			Vec3 F, d, eye, target;
			double tanHalfV;
			const char* name;
		};
		const Case cases[] = {
			{ { 0.0, 4.0, 2.0 }, { 0.0, 0.0, -1.0 }, { 0.0, 4.0, 8.0 }, { 0.0, 4.0, -8.0 }, 0.1, "back wall, from behind the lens" },
			{ { 0.0, 7.0, -4.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 8.0, 6.0 }, { 0.0, 0.0, -4.0 }, 0.12, "floor, from the audience" },
		};
		for( const Case& c : cases )
		{
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
				return 1;
			opticsSetup( rig, perturb, shaders::kHookSurfacesOnly );
			rig.Set( PT_CLIP_AS, static_cast< float >( ClipAs::Gobo ) );
			rig.Set( PT_MIX, 1.0f );
			rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
			rig.Set( PT_HAZE, ParamFromHaze( 0.001 ) );
			const Vec3 throwV = c.target - c.F;
			rig.Set( PT_FOCUS, ParamFromFocus( std::sqrt( dot( throwV, throwV ) ) ) );
			rig.plugin.SetRigForTest( beamRig( c.F, c.d, c.eye, c.target, c.tanHalfV ) );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats px = rig.Output();
			//The spot's centroid, then the mean colour of each quadrant about it.
			double sx = 0.0, sy = 0.0, sw = 0.0;
			for( int y = 0; y < raster.h; ++y )
				for( int x = 0; x < raster.w; ++x )
				{
					const float* p = &px[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
					const double v = p[ 0 ] + p[ 1 ] + p[ 2 ];
					sx += v * x;
					sy += v * y;
					sw += v;
				}
			const double cx = sx / sw, cy = sy / sw;
			double q[ 4 ][ 3 ] = {};
			for( int y = 0; y < raster.h; ++y )
				for( int x = 0; x < raster.w; ++x )
				{
					const float* p = &px[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
					const int k    = ( y >= cy ? 0 : 2 ) + ( x < cx ? 0 : 1 );//0 TL, 1 TR, 2 BL, 3 BR
					for( int ch = 0; ch < 3; ++ch )
						q[ k ][ ch ] += p[ ch ];
				}
			auto dominant = [ & ]( int k ) {
				const double* v = q[ k ];
				if( v[ 0 ] > 3.0 * v[ 1 ] && v[ 0 ] > 3.0 * v[ 2 ] )
					return 'R';
				if( v[ 1 ] > 3.0 * v[ 0 ] && v[ 1 ] > 3.0 * v[ 2 ] )
					return 'G';
				if( v[ 2 ] > 3.0 * v[ 0 ] && v[ 2 ] > 3.0 * v[ 1 ] )
					return 'B';
				return 'W';
			};
			const std::string got = { dominant( 0 ), dominant( 1 ), dominant( 2 ), dominant( 3 ) };
			Check( sw > 0.0 && got == "RGBW", fmt( "%dx%d, %s: quadrants (top left, top right, bottom left, bottom right) read %s", raster.w, raster.h, c.name,
			                                       got.c_str() ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --density: the stir belongs to the place.
//===========================================================================
int runDensity( const Perturb& perturb )
{
	std::printf( "\n=== density: the stir's mean is 0 within its sampling error and |n| <= 1; it moves slowly; two crossing beams thin by the same "
	             "factor where they cross\n" );
	{
		const Raster raster = kRasters.back();
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.plugin.SetStirTimeForTest( 37.0 );
		if( !rig.Render( 1 ) )
			return 1;
		const Floats a = readTexture( rig.plugin.AtlasTextureID(), shaders::kAtlasW, shaders::kAtlasH, GL_RED, 1 );
		double mean = 0.0, var = 0.0, lo = 1e9, hi = -1e9;
		for( float v : a )
		{
			mean += v / a.size();
			lo = std::min( lo, static_cast< double >( v ) );
			hi = std::max( hi, static_cast< double >( v ) );
		}
		for( float v : a )
			var += ( v - mean ) * ( v - mean ) / a.size();
		const double cells = ( 40.0 / 3.0 ) * ( 12.0 / 3.0 ) * ( 28.0 / 3.0 );
		const double bound = 3.0 * std::sqrt( var ) / std::sqrt( cells );
		Check( std::fabs( mean ) < bound && lo >= -1.0 && hi <= 1.0 && std::sqrt( var ) > 0.05,
		       fmt( "the room's %zu samples: mean %+.4f (bound 3 sd / sqrt %.0f base cells = %.4f), sd %.3f, range %+.3f..%+.3f", a.size(), mean, cells, bound,
		            std::sqrt( var ), lo, hi ) );

		//Slow: a frame later almost the same field; a minute later another.
		auto correlation = [ & ]( double dt ) {
			rig.plugin.SetStirTimeForTest( 37.0 + dt - 1.0 / 60.0 );
			rig.Render( 1 );
			const Floats b = readTexture( rig.plugin.AtlasTextureID(), shaders::kAtlasW, shaders::kAtlasH, GL_RED, 1 );
			double mb = 0.0, sab = 0.0, sbb = 0.0;
			for( float v : b )
				mb += v / b.size();
			for( size_t i = 0; i < a.size(); ++i )
			{
				sab += ( a[ i ] - mean ) * ( b[ i ] - mb );
				sbb += ( b[ i ] - mb ) * ( b[ i ] - mb );
			}
			return sab / std::sqrt( var * a.size() * sbb );
		};
		const double frame = correlation( 1.0 / 60.0 ), minute = correlation( 60.0 );
		Check( frame > 0.999 && minute < 0.5, fmt( "correlation with itself a frame later %.5f, a minute later %.3f", frame, minute ) );
	}
	for( const Raster& raster : kRasters )
	{
		//A narrow beam along x seen from above: at each column the thinning
		//Stir 1 over Stir 0 is 1 + stir n where the column's ray meets the
		//axis, n read from the room's atlas by the harness's own trilinear
		//lookup (the beam's chord is symmetric there: n's slope cancels).
		const double tanHalfV = 0.15;
		auto render = [ & ]( double stir, Floats& out, Floats* atlas, double* stirValue ) {
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return false;
			opticsSetup( rig, perturb, shaders::kHookAirOnly );
			rig.Set( PT_STIR, static_cast< float >( stir ) );
			rig.Set( PT_ZOOM, ParamFromZoom( 2.5 ) );
			rig.Set( PT_HAZE, ParamFromHaze( 0.01 ) );
			rig.Set( PT_FOCUS, ParamFromFocus( 8.0 ) );
			rig.Set( PT_ANISOTROPY, ParamFromAnisotropy( 0.0 ) );
			rig.plugin.SetRigForTest( beamRig( { -8.0, 6.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 11.5, 0.0 }, { 0.0, 6.0, 0.0 }, tanHalfV ) );
			rig.plugin.SetStirTimeForTest( 23.0 );
			if( !rig.Render( 1 ) )
				return false;
			out = rig.Output();
			if( atlas )
				*atlas = readTexture( rig.plugin.AtlasTextureID(), shaders::kAtlasW, shaders::kAtlasH, GL_RED, 1 );
			if( stirValue )
				*stirValue = rig.plugin.LastFrame().stir;
			return true;
		};
		Floats s0, s1, atlas;
		double stir = 0.0;
		if( !render( 0.0, s0, nullptr, nullptr ) || !render( 1.0, s1, &atlas, &stir ) )
			return 1;
		auto texel = [ & ]( int x, int y ) {
			x = std::clamp( x, 0, shaders::kAtlasW - 1 );
			y = std::clamp( y, 0, shaders::kAtlasH - 1 );
			return static_cast< double >( atlas[ static_cast< size_t >( y ) * shaders::kAtlasW + x ] );
		};
		//The atlas: 64 x 32 x 64 cells over the room, z sliced into an 8 x 8 grid of 64 x 32 tiles.
		auto field = [ & ]( const Vec3& p ) {
			double c[ 3 ];
			for( int k = 0; k < 3; ++k )
				c[ k ] = ( p[ k ] - stage::kBoxMin[ k ] ) / ( stage::kBoxMax[ k ] - stage::kBoxMin[ k ] ) * ( k == 1 ? 32.0 : 64.0 );
			const double zf = std::clamp( c[ 2 ] - 0.5, 0.0, 63.0 );
			const double z0 = std::floor( zf ), z1 = std::min( z0 + 1.0, 63.0 );
			const double cx = std::clamp( c[ 0 ], 0.5, 63.5 ), cy = std::clamp( c[ 1 ], 0.5, 31.5 );
			auto slice = [ & ]( double z ) {
				const double px = std::fmod( z, 8.0 ) * 64.0 + cx - 0.5, py = std::floor( z / 8.0 ) * 32.0 + cy - 0.5;
				const int ix = static_cast< int >( std::floor( px ) ), iy = static_cast< int >( std::floor( py ) );
				const double fx = px - ix, fy = py - iy;
				return ( texel( ix, iy ) * ( 1 - fx ) + texel( ix + 1, iy ) * fx ) * ( 1 - fy ) + ( texel( ix, iy + 1 ) * ( 1 - fx ) + texel( ix + 1, iy + 1 ) * fx ) * fy;
			};
			return slice( z0 ) + ( slice( z1 ) - slice( z0 ) ) * ( zf - z0 );
		};
		const double tanH = tanHalfV * raster.w / raster.h;
		double worst = 0.0, lo = 1e9, hi = -1e9;
		int probes = 0;
		for( int x = raster.w / 40; x < raster.w; x += std::max( 1, raster.w / 40 ) )
		{
			const double ndc = ( x + 0.5 ) / raster.w * 2.0 - 1.0;
			const Vec3 meet  = { ndc * tanH * 5.5, 6.0, 0.0 };//the ray from ( 0, 11.5, 0 ) meets the axis 5.5 m down
			double m0 = 0.0, m1 = 0.0;
			for( int y = raster.h / 2 - 1; y <= raster.h / 2; ++y )
			{
				m0 += s0[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
				m1 += s1[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
			}
			if( m0 <= 0.0 )
				continue;
			const double want = 1.0 + stir * field( meet );
			worst             = std::max( worst, std::fabs( m1 / m0 - want ) );
			lo                = std::min( lo, want );
			hi                = std::max( hi, want );
			++probes;
		}
		Check( probes >= 30 && worst < 0.01 && hi - lo > 0.1,
		       fmt( "%dx%d: along a 2.5 deg beam at %d columns the thinning follows 1 + stir n( x ) from the atlas within %.4f (bound 0.01; it ranges "
		            "%.3f..%.3f)",
		            raster.w, raster.h, probes, worst, lo, hi ) );
	}
	for( const Raster& raster : kRasters )
	{
		//Beam A along x, beam B along z, crossing at ( 0, 6, 0 ); seen from above.
		const Vec3 X = { 0.0, 6.0, 0.0 };
		auto render = [ & ]( int which, double stir, Floats& out ) {
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return false;
			opticsSetup( rig, perturb, shaders::kHookAirOnly );
			rig.Set( PT_STIR, static_cast< float >( stir ) );
			rig.Set( PT_ZOOM, ParamFromZoom( 12.0 ) );
			rig.Set( PT_HAZE, ParamFromHaze( 0.01 ) );
			rig.Set( PT_FOCUS, ParamFromFocus( 8.0 ) );
			rig.Set( PT_ANISOTROPY, ParamFromAnisotropy( 0.0 ) );
			rig.plugin.SetStirTimeForTest( 11.0 );
			TestRig r = beamRig( { -8.0, 6.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 11.5, 0.0 }, X, 0.15 );
			r.count   = 2;
			r.position[ 1 ]  = { 0.0, 6.0, 8.0 };
			r.direction[ 1 ] = { 0.0, 0.0, -1.0 };
			if( which == 0 )
				r.count = 1;
			else
			{
				//B alone: as fixture 1, so a per-beam haze would show.
				r.position[ 0 ]  = { -8.0, 6.0, 40.0 };//aimed out of the room
				r.direction[ 0 ] = { 0.0, 0.0, 1.0 };
			}
			rig.plugin.SetRigForTest( r );
			rig.plugin.SetStirTimeForTest( 11.0 - 1.0 / 60.0 );
			if( !rig.Render( 1 ) )
				return false;
			out = rig.Output();
			return true;
		};
		Floats a0, a1, b0, b1;
		if( !render( 0, 0.0, a0 ) || !render( 0, 1.0, a1 ) || !render( 1, 0.0, b0 ) || !render( 1, 1.0, b1 ) )
			return 1;
		//The square of +-0.2 m round the crossing: the camera 5.5 m above, tanHalfV 0.15.
		const double ppm = 0.5 * raster.h / ( 5.5 * 0.15 );
		const int half   = std::max( 1, static_cast< int >( 0.2 * ppm ) );
		double worst = 0.0, ra = 0.0, rb = 0.0;
		int n = 0;
		for( int y = raster.h / 2 - half; y < raster.h / 2 + half; ++y )
			for( int x = raster.w / 2 - half; x < raster.w / 2 + half; ++x )
			{
				const size_t i = ( static_cast< size_t >( y ) * raster.w + x ) * 4;
				if( a0[ i ] <= 0.0f || b0[ i ] <= 0.0f )
					continue;
				const double fa = a1[ i ] / a0[ i ], fb = b1[ i ] / b0[ i ];
				worst = std::max( worst, std::fabs( fa / fb - 1.0 ) );
				ra += fa;
				rb += fb;
				++n;
			}
		//And the stir really moved the beam somewhere along it.
		double moved = 0.0;
		for( int x = 0; x < raster.w; ++x )
		{
			const size_t i = ( static_cast< size_t >( raster.h / 2 ) * raster.w + x ) * 4;
			if( a0[ i ] > 0.0f )
				moved = std::max( moved, std::fabs( a1[ i ] / a0[ i ] - 1.0 ) );
		}
		ra /= std::max( n, 1 );
		rb /= std::max( n, 1 );
		Check( n > 0 && worst < 0.02 && moved > 0.05,
		       fmt( "%dx%d: over %d pixels round the crossing, the stir scales A by %.4f and B by %.4f on average, worst disagreement %.2f%% (bound 2%%); "
		            "along A the stir moves it by up to %.0f%%",
		            raster.w, raster.h, n, ra, rb, 100.0 * worst, 100.0 * moved ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha: the Over's composite.
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: the Over at Mix 0 is the clip, its alpha mix( clip, 1, Mix ); Screen with nothing in front returns the clip through "
	             "the haze; the source is opaque\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats clip = noiseCard( raster.w, raster.h );
		{
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &clip ) )
				return 1;
			rig.Set( PT_CLIP_AS, static_cast< float >( ClipAs::Gobo ) );
			rig.Set( PT_MIX, 0.0f );
			if( !rig.Render( 2 ) )
				return 1;
			const Floats out = rig.Output();
			int differ       = 0;
			for( size_t i = 0; i < out.size(); ++i )
				differ += out[ i ] != clip[ i ];
			Check( differ == 0, fmt( "%dx%d: Mix 0 against a per-pixel noise card: %d of %zu floats differ", raster.w, raster.h, differ, out.size() ) );
		}
		{
			const Floats see = flatCard( raster.w, raster.h, 0.8f, 0.4f, 0.2f, 0.3f );
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &see ) )
				return 1;
			double worst = 0.0;
			for( float mix : { 0.25f, 0.6f, 1.0f } )
				for( ClipAs as : { ClipAs::Gobo, ClipAs::Backdrop, ClipAs::Screen } )
			{
				rig.Set( PT_CLIP_AS, static_cast< float >( as ) );
				rig.Set( PT_MIX, mix );
				if( !rig.Render( 1 ) )
					return 1;
				const Floats out = rig.Output();
				for( size_t i = 3; i < out.size(); i += 4 )
					worst = std::max( worst, std::fabs( out[ i ] - ( 0.3 + 0.7 * mix ) ) );
			}
			Check( worst < 1e-6, fmt( "%dx%d: a clip at alpha 0.3 under Mix 0.25, 0.6, 1, each Clip As: alpha off mix( 0.3, 1, Mix ) by at most %.1e", raster.w, raster.h, worst ) );
		}
		{
			const Floats see = flatCard( raster.w, raster.h, 0.6f, 0.3f, 0.1f, 1.0f );
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &see ) )
				return 1;
			rig.Set( PT_CLIP_AS, static_cast< float >( ClipAs::Screen ) );
			rig.Set( PT_MIX, 1.0f );
			rig.Set( PT_EXPOSURE, ParamFromExposure( 0.0 ) );
			rig.Set( PT_HAZE, ParamFromHaze( 0.01 ) );
			TestRig r = beamRig( { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 5.0, 10.0 }, { 0.0, 5.0, -8.0 }, 0.05 );
			r.count   = 0;
			rig.plugin.SetRigForTest( r );
			if( !rig.Render( 1 ) )
				return 1;
			const double sigma = rig.plugin.LastFrame().sigma;
			const double T     = std::exp( -sigma * 18.0 );
			double worst       = 0.0;
			const double code[ 3 ] = { 0.6, 0.3, 0.1 };
			for( int c = 0; c < 3; ++c )
			{
				const double lin    = code[ c ] <= 0.04045 ? code[ c ] / 12.92 : std::pow( ( code[ c ] + 0.055 ) / 1.055, 2.4 );
				const double outLin = 1.0 - std::pow( 1.0 - lin, T );
				const double want   = outLin <= 0.0031308 ? outLin * 12.92 : 1.055 * std::pow( outLin, 1.0 / 2.4 ) - 0.055;
				worst               = std::max( worst, std::fabs( blockMean( rig.Output(), raster.w, raster.h, 1.0 / raster.w, 1.0 / raster.h, c ) - want ) );
			}
			Check( worst < 1e-3, fmt( "%dx%d: Screen, no fixtures, 18 m of haze at %.3f /m (T %.4f): the clip back as 1 - (1 - c)^T within %.1e", raster.w,
			                          raster.h, sigma, T, worst ) );
		}
		{
			Rig rig( false );
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out = rig.Output();
			int notOpaque    = 0;
			for( size_t i = 3; i < out.size(); i += 4 )
				notOpaque += out[ i ] != 1.0f;
			Check( notOpaque == 0, fmt( "%dx%d: the source's alpha is 1 at %zu of %zu pixels", raster.w, raster.h, out.size() / 4 - notOpaque, out.size() / 4 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --beat and --audio: through the plugin.
//===========================================================================
int runBeat( const Perturb& perturb )
{
	std::printf( "\n=== beat: through the plugin, Resolume's beat steps the chase where the beat fell inside the frame\n" );
	const Raster raster = kRasters.back();
	Rig rig;
	rig.fps = 30.0;
	rig.bpm = 128.0;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	rig.Set( PT_STEP_ON, static_cast< float >( StepOn::Beat ) );
	rig.Set( PT_PATTERN, static_cast< float >( Pattern::Sweep ) );
	std::vector< double > steps;
	for( int f = 0; f < 121; ++f )
	{
		if( !rig.Render( 1 ) )
			return 1;
		for( double t : rig.plugin.ChaseState().StepTimes() )
			steps.push_back( t );
	}
	const double beat = 60.0 / 128.0;
	double worst      = 0.0;
	for( double t : steps )
		worst = std::max( worst, std::fabs( t - beat * std::round( t / beat ) ) );
	Check( steps.size() == 8 && worst < 1e-6, fmt( "128 BPM at 30 fps for 4 s: %zu steps, worst %.1e s off the beat (%.5f s, inside frames of %.4f s)", steps.size(),
	                                                worst, beat, 1.0 / 30.0 ) );
	return Verdict();
}

int runAudio( const Perturb& perturb )
{
	std::printf( "\n=== audio: Step On Onset, the harness's kicks in the FFT buffer: one step on each kick's own frame; nothing from the cold start\n" );
	const Raster raster = kRasters.back();
	Rig rig;
	rig.bpm = 0.0;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	rig.Set( PT_STEP_ON, static_cast< float >( StepOn::Onset ) );
	rig.beforeFrame = [ & ]( int f ) { feedKicks( rig.plugin, f ); };
	std::vector< int > kicks, stepped;
	for( int f = 0; f < 220; ++f )
	{
		if( f >= 20 && ( f - 20 ) % 30 == 0 )
			kicks.push_back( f );
		const long long before = rig.plugin.ChaseState().Steps();
		if( !rig.Render( 1 ) )
			return 1;
		if( rig.plugin.ChaseState().Steps() > before )
			stepped.push_back( f );
	}
	Rig cold;
	cold.bpm = 0.0;
	if( !startRig( cold, raster.w, raster.h, perturb ) )
		return 1;
	cold.Set( PT_STEP_ON, static_cast< float >( StepOn::Onset ) );
	cold.beforeFrame = [ & ]( int f ) { feedKicks( cold.plugin, f, true ); };
	if( !cold.Render( 20 ) )
		return 1;
	std::string at;
	for( int f : stepped )
		at += fmt( " %d", f );
	Check( stepped == kicks, fmt( "%zu kicks (frames 20, 50, ... 200), %zu steps, at frames%s: %s", kicks.size(), stepped.size(), at.c_str(),
	                              stepped == kicks ? "each on its kick's own frame" : "NOT the kicks' frames" ) );
	Check( cold.plugin.ChaseState().Steps() == 0,
	       fmt( "a fresh instance with music already loud on its first two frames: %lld steps in the first 20 (none before the first kick)",
	            cold.plugin.ChaseState().Steps() ) );
	return Verdict();
}

//===========================================================================
// --clock: Resolume's huge millisecond clock.
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: at 499,000,000 ms the frames are still 1/60 s apart and the stir still drifts by v dt a frame\n" );
	const Raster raster = kRasters.back();
	Rig rig;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	rig.plugin.SetClockScaleForTest( 0.001 );
	double worstDt = 0.0, worstDrift = 0.0;
	double lastNow = 0.0, lastX = 0.0;
	for( int f = 0; f < 20; ++f )
	{
		rig.plugin.SetTime( 499000000.0 + f * 1000.0 / 60.0 );
		glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
		glViewport( 0, 0, raster.w, raster.h );
		if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
			return 1;
		const double now = rig.plugin.HostNow();
		const double x   = rig.plugin.StirOffsets()[ 0 ][ 0 ];
		if( f > 0 )
		{
			worstDt = std::max( worstDt, std::fabs( ( now - lastNow ) - 1.0 / 60.0 ) );
			double step = ( lastX - x ) * 3.0;//metres: lattice units of 3 m, drifting downward
			if( step < 0.0 )
				step += 64.0 * 3.0;
			worstDrift = std::max( worstDrift, std::fabs( step - 0.25 / 60.0 ) );
		}
		lastNow = now;
		lastX   = x;
	}
	Check( worstDt < 1e-9 && worstDrift < 1e-6,
	       fmt( "20 frames: dt off 1/60 by at most %.1e s; the drift off 0.25 m/s x dt by at most %.1e m", worstDt, worstDrift ) );
	return Verdict();
}

//===========================================================================
// --resize and --state.
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: after the host's raster changes (and its shape), the picture is exactly a fresh instance's that saw the same frames\n" );
	const Raster small = kRasters.back(), big = { 2 * small.w, 2 * small.h }, tall = { small.w, small.w * 3 / 4 };
	for( const Raster& to : { small, tall } )
	{
		Rig resized;
		if( !startRig( resized, big.w, big.h, perturb ) )
			return 1;
		resized.bpm = 120.0;
		if( !resized.Render( 70 ) || !resized.Resize( to.w, to.h ) || !resized.Render( 3 ) )
			return 1;
		Rig fresh;
		if( !startRig( fresh, to.w, to.h, Perturb {} ) )
			return 1;
		fresh.bpm = 120.0;
		if( !fresh.Render( 73 ) )
			return 1;
		const Floats x = resized.Output(), y = fresh.Output();
		int differ = 0;
		double lit = 0.0;
		for( size_t i = 0; i < x.size() && i < y.size(); ++i )
		{
			differ += byteOf( x[ i ] ) != byteOf( y[ i ] );
			lit = std::max( lit, static_cast< double >( y[ i ] ) );
		}
		Check( differ == 0 && lit > 0.1 && resized.plugin.ChaseState().Steps() > 0,
		       fmt( "%dx%d for 70 frames, then %dx%d for 3, against %dx%d for all 73: %d bytes differ (%lld steps)", big.w, big.h, to.w, to.h, to.w, to.h, differ,
		            resized.plugin.ChaseState().Steps() ) );
	}
	return Verdict();
}

int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back, both plugins\n" );
	for( bool over : { false, true } )
	{
		Rig rig( over );
		if( !rig.Init( 320, 180 ) )
			return 1;
		GLuint hostArray = 0, hostBuffer = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostBuffer );
		int problems = 0;
		std::string what;
		for( int frame = 0; frame < 6; ++frame )
		{
			rig.Set( PT_STEP, frame == 1 ? 1.0f : 0.0f );
			rig.Set( PT_GOBO, static_cast< float >( frame % 6 ) );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 0, 0, 320, 180 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );
			rig.plugin.SetTime( frame / 60.0 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0;
			GLfloat clear[ 4 ]   = {};
			GLboolean mask[ 4 ]  = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			glGetBooleanv( GL_COLOR_WRITEMASK, mask );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == 320 && viewport[ 3 ] == 180, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
			for( int u = 0; u < 8; ++u )
			{
				GLint bound = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				expect( bound == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostBuffer );
		Check( problems == 0, fmt( "%s, six frames, a Step and every gobo: viewport, vertex array, array buffer, program, active unit, framebuffer, "
		                           "blend, scissor, clear colour, colour mask, eight texture units (%d wrong:%s)",
		                           over ? "SW Haze Over" : "SW Haze", problems, what.empty() ? " none" : what.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --names and --cues (no GL).
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too), within FFGL's 16 characters, the About block last\n" );
	for( bool over : { false, true } )
	{
		HazePlugin plugin( over );
		std::map< std::string, int > seen, address;
		int longNames = 0, dupes = 0, clashes = 0, slashes = 0;
		for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			if( seen[ name ]++ > 0 )
				++dupes;
			slashes += name.find( '/' ) != std::string::npos;
			std::string key;
			for( char c : name )
				if( c != ' ' )
					key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			if( address[ key ]++ > 0 )
				++clashes;
		}
		const unsigned int aboutFirst = plugin.GetNumParams() - stoatworks::about::kParamCount;
		const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
		Check( longNames == 0 && dupes == 0 && clashes == 0 && slashes == 0 && aboutLast,
		       fmt( "%s: %u parameters, %d too long, %d duplicated, %d clashing addresses, %d with a '/'; the About block is last (%s)",
		            over ? "SW Haze Over" : "SW Haze", plugin.GetNumParams(), longNames, dupes, clashes, slashes, aboutLast ? "yes" : "NO" ) );
	}
	return Verdict();
}

int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options and events, ramps standard controls, and names an option by its name\n" );
	HazePlugin plugin( false );
	std::istringstream sheet( "0 Pattern Fan\n60 Pattern Audience\n0 Step On Beat\n60 Step On 2 a Beat\n0 Step 0\n60 Step 1\n0 Haze 0\n60 Haze 1\n" );
	std::map< unsigned int, Cue > cues;
	std::string error;
	bindScript( plugin, sheet, "cues", cues, error );
	int bad = 0;
	std::string what;
	for( const auto& entry : cues )
	{
		const unsigned int type = plugin.GetParamType( entry.first );
		const bool ramp         = perturb.cuesRamp ? true : entry.second.ramp;
		const float mid         = valueAt( entry.second.track, 29, ramp );
		const float a = entry.second.track.front().second, b = entry.second.track.back().second;
		const bool expectRamp = type == FF_TYPE_STANDARD;
		const bool ok         = expectRamp ? ( mid > a && mid < b ) : ( mid == a && valueAt( entry.second.track, 60, ramp ) == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", plugin.GetParamName( entry.first ), expectRamp ? "ramps " : "steps ", mid );
	}
	const auto pattern = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_PATTERN ) ) );
	const auto stepOn  = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_STEP_ON ) ) );
	const bool named   = pattern != cues.end() && pattern->second.track.back().second == static_cast< float >( Pattern::Audience ) && stepOn != cues.end()
	                   && stepOn->second.track.back().second == static_cast< float >( StepOn::TwoABeat );
	Check( error.empty() && bad == 0 && cues.size() == 4 && named,
	       fmt( "%d wrong:%s; 'Audience' and '2 a Beat' read as their options: %s", bad, what.c_str(), named ? "yes" : "NO" ) );
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "phase", runPhase, false },     { "extinction", runExtinction, false }, { "lens", runLens, false }, { "servo", runServo, true },   { "late", runLate, true },
		{ "chase", runChaseCheck, true }, { "aim", runAim, false },               { "focus", runFocus, false },  { "gobo", runGobo, false },
		{ "density", runDensity, false }, { "alpha", runAlpha, false },           { "beat", runBeat, false },    { "audio", runAudio, false },
		{ "clock", runClock, false },     { "resize", runResize, false },         { "state", runState, false },  { "names", runNames, true },
		{ "cues", runCues, true },
	};
	return list;
}

bool isOffline( const char* name )
{
	for( const CheckEntry& c : checks() )
		if( std::strcmp( c.flag, name ) == 0 )
			return c.offline;
	return false;
}

int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	using namespace shaders;
	add( "phase", runPhase, "an isotropic phase function", []( Perturb& p ) { p.hooks = kHookIsotropic; } );
	add( "phase", runPhase, "cos theta negated: the forward peak thrown backward", []( Perturb& p ) { p.hooks = kHookBackward; } );
	add( "phase", runPhase, "the inverse square dropped", []( Perturb& p ) { p.hooks = kHookNoInvSquare; } );
	add( "extinction", runExtinction, "nothing lost between the lens and the droplet", []( Perturb& p ) { p.hooks = kHookNoLightExt; } );
	add( "extinction", runExtinction, "nothing lost between the droplet and the eye", []( Perturb& p ) { p.hooks = kHookNoEyeExt; } );
	add( "servo", runServo, "acceleration unbounded: no ramps", []( Perturb& p ) { p.motionHooks = kHookUnboundedAccel; } );
	add( "servo", runServo, "vectorial sync off", []( Perturb& p ) { p.motionHooks = kHookNoSync; } );
	add( "late", runLate, "a step at its frame's start, not where the beat fell", []( Perturb& p ) { p.motionHooks = kHookStepAtFrameStart; } );
	add( "late", runLate, "acceleration unbounded: no ramps", []( Perturb& p ) { p.motionHooks = kHookUnboundedAccel; } );
	add( "chase", runChaseCheck, "a step at its frame's start, not where the beat fell", []( Perturb& p ) { p.motionHooks = kHookStepAtFrameStart; } );
	add( "focus", runFocus, "the gobo sharp at every distance", []( Perturb& p ) { p.hooks = kHookNoFocus; } );
	add( "gobo", runGobo, "the gobo mirrored", []( Perturb& p ) { p.hooks = kHookMirror; } );
	add( "density", runDensity, "each beam sees its own haze", []( Perturb& p ) { p.hooks = kHookStirPerBeam; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( Perturb& p ) { p.hooks = kHookAlphaThrough; } );
	add( "beat", runBeat, "a step at its frame's start, not where the beat fell", []( Perturb& p ) { p.motionHooks = kHookStepAtFrameStart; } );
	add( "audio", runAudio, "the analyser never primed", []( Perturb& p ) { p.unprimed = true; } );
	add( "cues", runCues, "ramp every control between keys", []( Perturb& p ) { p.cuesRamp = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		bool over;
		std::function< void( Rig& ) > set;
		bool at4K;///< 4K once, short (a shared machine: no 4K loops)
	};
	const Case cases[] = {
		{ "source, defaults (Medium)", false, []( Rig& ) {}, true },
		{ "source, High", false, []( Rig& r ) { r.Set( PT_QUALITY, static_cast< float >( Quality::High ) ); }, false },
		{ "source, Low", false, []( Rig& r ) { r.Set( PT_QUALITY, static_cast< float >( Quality::Low ) ); }, false },
		{ "source, 12 fixtures, 30.8 deg, Medium", false,
		  []( Rig& r ) {
			  r.Set( PT_FIXTURES, 1.0f );
			  r.Set( PT_ZOOM, 1.0f );
		  },
		  false },
		{ "source, 12 fixtures, 30.8 deg, Low", false,
		  []( Rig& r ) {
			  r.Set( PT_FIXTURES, 1.0f );
			  r.Set( PT_ZOOM, 1.0f );
			  r.Set( PT_QUALITY, static_cast< float >( Quality::Low ) );
		  },
		  false },
		{ "Over, defaults (Medium)", true, []( Rig& ) {}, true },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), after 240 frames of warm-up, median and worst of 120 (4K: 30 and 30, defaults only)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			const bool fourK = size.w == 3840;
			if( fourK && !c.at4K )
				continue;
			Rig rig( c.over );
			rig.bpm = 120.0;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			c.set( rig );
			if( !rig.Render( fourK ? 30 : 240 ) )
				return 1;
			const int kTimed = fourK ? 30 : 120;
			std::vector< double > times, cpu;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				times.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( times.begin(), times.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::printf( "  %-38s %-6s GPU median %6.2f ms, worst %6.2f  (%5.1f%% of a 60 fps frame); wall %5.2f ms\n", c.what, size.name, times[ kTimed / 2 ],
			             times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ), cpu[ kTimed / 2 ] );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
