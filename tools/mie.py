"""Mie scattering by a stage-haze droplet: where SW Haze's default anisotropy comes from.

A haze droplet is a sphere of oil, so its scattering is Mie's (Lorenz-Mie), and the
Henyey-Greenstein phase function the plugin uses is a one-parameter stand-in for it,
matched in its asymmetry g = <cos theta>. This computes Mie's solution for one sphere
(Bohren & Huffman, "Absorption and Scattering of Light by Small Particles", 1983,
appendix A: BHMIE, the downward-recurrence logarithmic derivative), and averages it
over a lognormal distribution of diameters, to give:

- the efficiencies Q_ext and Q_sca and the asymmetry g for each diameter;
- the size-averaged g and the extinction per unit mass of oil, b/C = (3/2) <Q d^2> /
  (rho <d^3>), which turns a measured mass concentration into an extinction
  coefficient;
- how far the Henyey-Greenstein function with that g is from the Mie phase function
  it stands in for, at a few angles.

Inputs, and what they are:

- n = 1.467: light mineral oil, n20/D (Sigma-Aldrich M3516, literature value), taken
  at 550 nm (its dispersion, about 0.006 across the visible for such oils, is ignored);
  no absorption (k = 0: the oil is clear).
- rho = 840 kg/m^3: the same datasheet's density at 25 C.
- The droplet size is ASSUMED. No published size distribution of a hazer's output was
  found; Teschke et al. (J Occup Environ Hyg 2005, 2(5):277-84) report that 61% of the
  aerosol mass on set was under 3.5 um aerodynamic diameter, which bounds nothing at
  the small end. The distributions below are a choice, and the script prints how g
  moves with it.

    python3 -I tools/mie.py            the table SPEC-haze.md and AGENTS.md quote
    python3 -I tools/mie.py --check    Mie against two published values (exit 1 on a miss)

numpy only.
"""
import math
import sys

import numpy as np

N_OIL = 1.467
RHO_OIL = 840.0
WAVELENGTH = 550e-9


def bhmie(x, m, nang=181):
    """Bohren & Huffman's BHMIE for a size parameter x and relative index m: returns
    Q_ext, Q_sca, g and the unpolarised phase function p(theta) on nang angles from 0 to
    180 degrees, normalised so that its integral over the sphere is 1."""
    mx = m * x
    nstop = int(x + 4.0 * x ** (1.0 / 3.0) + 2.0)
    nmx = int(max(nstop, abs(mx)) + 15)
    theta = np.linspace(0.0, math.pi, nang)
    mu = np.cos(theta)
    d = np.zeros(nmx + 1, dtype=complex)
    for n in range(nmx, 0, -1):
        en = n / mx
        d[n - 1] = en - 1.0 / (d[n] + en)
    psi0, psi1 = math.cos(x), math.sin(x)
    chi0, chi1 = -math.sin(x), math.cos(x)
    xi1 = complex(psi1, -chi1)
    s1 = np.zeros(nang, dtype=complex)
    s2 = np.zeros(nang, dtype=complex)
    pi0 = np.zeros(nang)
    pi1 = np.ones(nang)
    qsca = 0.0
    gsca = 0.0
    an1 = bn1 = 0j
    for n in range(1, nstop + 1):
        fn = (2.0 * n + 1.0) / (n * (n + 1.0))
        psi = (2.0 * n - 1.0) * psi1 / x - psi0
        chi = (2.0 * n - 1.0) * chi1 / x - chi0
        xi = complex(psi, -chi)
        an = ((d[n] / m + n / x) * psi - psi1) / ((d[n] / m + n / x) * xi - xi1)
        bn = ((d[n] * m + n / x) * psi - psi1) / ((d[n] * m + n / x) * xi - xi1)
        qsca += (2.0 * n + 1.0) * (abs(an) ** 2 + abs(bn) ** 2)
        gsca += (2.0 * n + 1.0) / (n * (n + 1.0)) * (an.real * bn.real + an.imag * bn.imag)
        if n > 1:
            gsca += (n - 1.0) * (n + 1.0) / n * (an1.real * an.real + an1.imag * an.imag
                                                 + bn1.real * bn.real + bn1.imag * bn.imag)
        pin = pi1
        tau = n * mu * pin - (n + 1.0) * pi0
        s1 += fn * (an * pin + bn * tau)
        s2 += fn * (an * tau + bn * pin)
        psi0, psi1 = psi1, psi
        chi0, chi1 = chi1, chi
        xi1 = complex(psi1, -chi1)
        an1, bn1 = an, bn
        pi1 = ((2.0 * n + 1.0) * mu * pi1 - (n + 1.0) * pi0) / n
        pi0 = pin
    qsca *= 2.0 / x ** 2
    qext = 4.0 / x ** 2 * s1[0].real  # the optical theorem, forward amplitude
    g = 4.0 * gsca / (qsca * x ** 2)  # BHMIE: 2 gsca over the raw sum, which is qsca x^2 / 2
    intensity = 0.5 * (np.abs(s1) ** 2 + np.abs(s2) ** 2)
    norm = np.trapezoid(intensity * np.sin(theta), theta) * 2.0 * math.pi
    bhmie.last_s1 = s1  # for the checks: S1 at 0 and 180 degrees
    return qext, qsca, g, theta, intensity / norm


def hg(g, theta):
    return (1.0 - g * g) / (4.0 * math.pi * (1.0 + g * g - 2.0 * g * np.cos(theta)) ** 1.5)


def lognormal(cmd, gsd, k=401):
    """Diameters and number weights of a lognormal by count: count median cmd, geometric
    standard deviation gsd, sampled out to 4 sigma."""
    s = math.log(gsd)
    z = np.linspace(-4.0, 4.0, k)
    d = cmd * np.exp(s * z)
    w = np.exp(-0.5 * z * z)
    return d, w / w.sum()


def averaged(cmd, gsd):
    d, w = lognormal(cmd, gsd)
    qe = np.zeros_like(d)
    qs = np.zeros_like(d)
    gg = np.zeros_like(d)
    phase = None
    for i, di in enumerate(d):
        x = math.pi * di / WAVELENGTH
        qe[i], qs[i], gg[i], theta, p = bhmie(x, complex(N_OIL, 0.0))
        cs = qs[i] * di * di  # scattering cross-section, up to pi/4
        phase = p * w[i] * cs if phase is None else phase + p * w[i] * cs
    csca = (w * qs * d * d).sum()
    gbar = (w * qs * d * d * gg).sum() / csca
    phase /= csca
    # Extinction per unit mass: b = N <Q pi d^2 / 4>, C = N rho pi <d^3> / 6.
    per_mass = 1.5 * (w * qe * d * d).sum() / (RHO_OIL * (w * d ** 3).sum())
    mmd = mass_median(d, w)
    return gbar, per_mass, theta, phase, mmd


def mass_median(d, w):
    m = w * d ** 3
    c = np.cumsum(m) / m.sum()
    return float(np.interp(0.5, c, d))


def check():
    """Against Wiscombe's published MIEV0 values, as tabulated in miepython's validation
    pages (miepython.readthedocs.io, 3.0.4, "Backscattering Efficiency Validation"):
    Q_back = |2 S1(180) / x|^2 for two clear spheres, and Q_ext, Q_sca and g for his test
    case 12 (m = 1.5 - 1.0i there; BHMIE writes absorption as + i)."""
    ok = True
    for m, x, ref in ((1.55, 2.0 * math.pi * 0.525 / 0.6328, 2.925340), (1.5, 10.0, 1.695084)):
        bhmie(x, complex(m, 0.0))
        qb = abs(2.0 * bhmie.last_s1[-1] / x) ** 2
        good = abs(qb / ref - 1.0) < 1e-4
        ok &= good
        print(f"  m {m}, x {x:.4f}: Q_back {qb:.6f} against Wiscombe's {ref:.6f}  {'ok' if good else 'MISS'}")
    qe, qs, g, _, _ = bhmie(0.055, complex(1.5, 1.0))
    good = abs(qe / 0.101491 - 1.0) < 1e-4 and abs(qs / 0.000011 - 1.0) < 0.05 and abs(g / 0.000491 - 1.0) < 1e-2
    ok &= good
    print(f"  MIEV0 case 12: Q_ext {qe:.6f} (0.101491), Q_sca {qs:.6f} (0.000011), g {g:.6f} (0.000491)  {'ok' if good else 'MISS'}")
    # The Rayleigh limit: Q_sca -> (8/3) x^4 ((m^2-1)/(m^2+2))^2 and g -> 0.
    x = 0.01
    qe, qs, g, _, _ = bhmie(x, complex(N_OIL, 0.0))
    ray = 8.0 / 3.0 * x ** 4 * ((N_OIL ** 2 - 1) / (N_OIL ** 2 + 2)) ** 2
    good = abs(qs / ray - 1.0) < 1e-3 and abs(g) < 1e-3
    ok &= good
    print(f"  Rayleigh, x 0.01: Q_sca over the closed form {qs / ray:.6f}, g {g:.1e}  {'ok' if good else 'MISS'}")
    # A clear sphere absorbs nothing: Q_ext (optical theorem) = Q_sca (the series).
    qe, qs, g, _, _ = bhmie(8.0, complex(N_OIL, 0.0))
    good = abs(qe / qs - 1.0) < 1e-9
    ok &= good
    print(f"  x 8, clear oil: Q_ext / Q_sca - 1 = {qe / qs - 1.0:.1e}  {'ok' if good else 'MISS'}")
    print("  PASS" if ok else "  FAIL")
    return ok


def main():
    if "--check" in sys.argv:
        return 0 if check() else 1
    print(f"mineral oil n {N_OIL}, rho {RHO_OIL:.0f} kg/m^3, at {WAVELENGTH * 1e9:.0f} nm\n")
    print("one droplet:")
    print("  d (um)   Q_ext   g")
    for dum in (0.2, 0.3, 0.5, 0.7, 1.0, 1.5, 2.0, 3.0):
        x = math.pi * dum * 1e-6 / WAVELENGTH
        qe, qs, g, _, _ = bhmie(x, complex(N_OIL, 0.0))
        print(f"  {dum:5.2f}   {qe:6.3f}  {g:5.3f}")
    print("\nlognormal by count (the haze is ASSUMED to be one of these):")
    print("  CMD (um)  GSD   MMD (um)  g      b/C (m^2/g)   HG/Mie at 10, 30, 90, 150 deg")
    for cmd, gsd in ((0.3, 1.6), (0.5, 1.6), (0.5, 2.0), (0.8, 1.6), (1.0, 1.8)):
        gbar, per_mass, theta, phase, mmd = averaged(cmd * 1e-6, gsd)
        h = hg(gbar, theta)
        idx = [int(round(a)) for a in (10, 30, 90, 150)]
        ratios = " ".join(f"{h[i] / phase[i]:5.2f}" for i in idx)
        print(f"  {cmd:6.2f}   {gsd:4.1f}  {mmd * 1e6:6.2f}    {gbar:5.3f}  {per_mass / 1e3:8.2f}       {ratios}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
