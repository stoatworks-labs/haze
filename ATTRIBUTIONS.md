# Attributions

Haze is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is a provisional hand copy: the master lists live in the `stoatworks-backend` repo and
are pushed out by `scripts/sync-attributions.py` once a project is registered there. Haze
is not registered yet, so this file was written by hand on 2026-11-16, adapted from
xenon's, and will be replaced by the generated one.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The plugins' shape, the harness and the tooling — Stoatworks ferrofluid

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

Two plugins on one OBJECT core with an `isEffect` flag, the table of controls, the host-clock vote, the sRGB encode, finding a beat's crossing inside the frame (ferrofluid's Drive), the harness's host, PNG writer, cue sheets, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout exits 1), `--negative` with its perturbation hooks, `--state`, `--names`, `--cues`, `--offline` and `--bench`, and tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py, the CI and release workflows and scripts/release-lib.sh are ferrofluid's (zoetrope's, schlieren's and tinsel's before it), adapted.

### The audio analyser, the pass buffers and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Audio.{h,cpp} is millpond's onset analyser (rosette's, adapted from macroblock's), with its primed first frame, by way of schlieren; source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's. All adapted.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is radar's, which is vectrix's, which lifted it from `resolume-scopes`' Scopes.cpp: the host's GL state captured and put back.

### The About block and the harness's kicks — Stoatworks xenon

<https://github.com/stoatworks-labs/xenon>  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout.h, StoatworksAboutLinks.h and StoatworksAboutParams.h are provisional hand copies of xenon's, with `guide` empty (no user guide exists). The harness's `--kicks` feed (a kick every half second, alternately soft and hard, over a steady floor) is xenon's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Bohren & Huffman's BHMIE, and Wiscombe's MIEV0 test values

C. F. Bohren and D. R. Huffman, *Absorption and Scattering of Light by Small Particles* (Wiley, 1983), appendix A; W. J. Wiscombe, "Improved Mie scattering algorithms", Applied Optics 19(9), 1505 (1980), doi:10.1364/AO.19.001505, with its test values as tabulated by miepython 3.0.4's validation pages. tools/mie.py is a port of the book's algorithm written from its description (no code taken from miepython or anyone else), and `tools/mie.py --check` holds it to three of Wiscombe's values; the default Anisotropy and the Haze range are read off its table.

### Clay Paky's Mythos manual and datasheets

The Mythos user manual's technical specifications (157 mm front lens, zoom 4.5°–30.8° and the 2.5° beam mode, 24,000 lm, pan 540° in 3.517 s and tilt 244° in 2.180 s at Fast, vectorial movement), the Mythos2 datasheet (160 mm lens) and the Sharpy datasheet (pan 540° in 2.45 s, tilt 250° in 1.30 s). The fixture's optics and servo limits are these numbers; `hztest --servo` checks the shipped constants against the manual's literals, not against themselves.

### Teschke et al. on haze on set

K. Teschke et al., "Exposures to atmospheric effects in the entertainment industry", J Occup Environ Hyg 2(5), 277–284 (2005), doi:10.1080/15459620590952215: the measured aerosol concentrations on film, television, theatre and concert sets that set the Haze control's range.

## Standards and published specifications

What the implementation is measured against.

- **Henyey–Greenstein** — L. G. Henyey and J. L. Greenstein, "Diffuse radiation in the Galaxy", ApJ 93, 70 (1941), doi:10.1086/144246: the phase function in source/Shaders.cpp's `phase()`, which `hztest --phase` recovers from the picture.
- **Beer–Lambert** — transmittance exp(−σL) along both paths, which `hztest --extinction` measures as a slope.
- **Equiangular sampling** — C. Kulla and M. Fajardo, "Importance Sampling Techniques for Path Tracing in Participating Media", Computer Graphics Forum 31(4), 1519–1528 (2012), doi:10.1111/j.1467-8659.2012.03148.x: how the air pass samples each beam.
- **Light mineral oil** — Sigma-Aldrich M3516's listing: n20/D 1.467, density 0.84 g/mL at 25 °C, tools/mie.py's droplet.
- **IEC 61966-2-1 (sRGB)** — the encode in source/Shaders.cpp.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Moving heads in a hazed room

Every concert lighting rig: the beams you only see because the crew is running a hazer, the gobo sharp on the cyc and soft in the air, and the chase that can't keep up with the drummer. Built from how the things work; nothing is copied from anyone's software, visualiser, fixture profile or design.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
