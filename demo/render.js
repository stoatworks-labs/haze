/**
 * Haze's four passes in WebGL2: the plugin's own GLSL, unedited.
 *
 * `shaders.js` is the seven `R"( ... )"` pieces of `source/Shaders.cpp` plus
 * kVersion, spliced by `demo/tools/check_shaders.py --write` and never typed;
 * the same script compares them character for character, and checks they are
 * joined here as `Assemble()` and InitGL join them in the plugin.
 *
 *   1. noise   R16F 512 x 256: the stir, an 8 x 8 atlas of 64 x 32 slices of
 *              a 64 x 32 x 64 field over the room
 *   2. gobo    RGBA16F 512 x 512, mipmapped: the wheel's gobo, drawn once per
 *              change, or (the Over in Clip As Gobo) the clip, every frame
 *   3. air     RGBA32F at Quality's raster: the light the beams scatter toward
 *              the eye, ray-marched per beam
 *   4. final   the canvas: the lit floor and back wall, the lenses, the air
 *              upsampled, exposure, the tone curve, sRGB, the Over's composite
 *
 * The same buffer sizes and formats as HazePlugin::ProcessOpenGL, the same
 * filtering (linear; the gobo trilinear through its mips), the same texture
 * units, and every uniform the plugin sets, with the values port.js computed.
 *
 * Two things a browser makes conditional, both said on the page:
 *   - rendering into R16F, RGBA16F and RGBA32F needs EXT_color_buffer_float
 *     (core in desktop GL). Without it this stops and says so.
 *   - sampling the RGBA32F air buffer bilinearly needs OES_texture_float_linear
 *     (core in desktop GL). Without it the air buffer is RGBA16F instead -- the
 *     one place the page departs from the plugin's formats -- and the line under
 *     the picture says which the browser got.
 */

import { Program, PassBuffer, GLError, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';
import { f32, kAtlasW, kAtlasH, kGoboSize, Gobo } from './port.js';

export function createPasses(gl, quad) {
  if (!gl.getExtension('EXT_color_buffer_float')) {
    throw new GLError('EXT_color_buffer_float is missing. Haze renders its stir into an R16F texture, its gobo into RGBA16F and its air into RGBA32F, as the plugin does in desktop GL, and this browser cannot render into any of them. An 8-bit air buffer would draw a plausible wrong picture, so the page stops here instead.');
  }
  const floatLinear = gl.getExtension('OES_texture_float_linear') !== null;
  const airFormat = floatLinear ? gl.RGBA32F : gl.RGBA16F;

  // InitGL: the vertex stage is kVersion + kQuadVertex; each fragment stage is
  // Assemble( pass ), kVersion + kCommon + (kScene +) the pass.
  const vertex = S.VERSION + S.QUAD_VERTEX;
  const text = S.VERSION + S.COMMON;
  const assemble = {
    noise: text + S.NOISE_FRAGMENT,
    gobo: text + S.GOBO_FRAGMENT,
    air: text + S.SCENE + S.AIR_FRAGMENT,
    final: text + S.SCENE + S.FINAL_FRAGMENT,
  };
  const programs = {
    noise: new Program(gl, vertex, assemble.noise, 'noise'),
    gobo: new Program(gl, vertex, assemble.gobo, 'gobo'),
    air: new Program(gl, vertex, assemble.air, 'air'),
    final: new Program(gl, vertex, assemble.final, 'final'),
  };

  const atlas = new PassBuffer(gl, { filter: 'linear' });
  const gobo = new PassBuffer(gl, { filter: 'linear', mip: true });
  const air = new PassBuffer(gl, { filter: 'linear' });
  let goboBuilt = -1;

  const ivec2 = (p, name, a, b) => {
    const loc = p.location(name);
    if (loc !== null) gl.uniform2i(loc, a, b);
  };
  const ivec4 = (p, name, a, b, c, d) => {
    const loc = p.location(name);
    if (loc !== null) gl.uniform4i(loc, a, b, c, d);
  };

  /** sceneUniforms(): the camera, the fixtures, the beam and the haze. */
  function sceneUniforms(p, u) {
    p.set('Eye', u.Eye);
    p.set('Forward', u.Forward);
    p.set('Right', u.Right);
    p.set('Up', u.Up);
    p.setInt('Count', u.Count);
    p.setArray('FixPos', u.FixPos, 3);
    p.setArray('FixDir', u.FixDir, 3);
    p.setArray('FixRight', u.FixRight, 3);
    p.setArray('FixUp', u.FixUp, 3);
    p.setArray('FixI0', u.FixI0, 3);
    p.set('TanAlpha', u.TanAlpha);
    p.set('Focus', u.Focus);
    p.set('LensRadius', u.LensRadius);
    p.set('Sigma', u.Sigma);
    p.setInt('Hooks', 0);
  }

  return {
    floatLinear,
    airFormatName: floatLinear ? 'RGBA32F' : 'RGBA16F',

    /** A new instance: the gobo is drawn again on its first frame. */
    reset() {
      goboBuilt = -1;
    },

    /**
     * Draw one frame. `frame` is what port.js's Haze.ProcessFrame returned;
     * `input` the clip ({ texture, width, height }, premultiplied, uv origin
     * bottom left) or null for the source; the output goes to the canvas, at
     * `width` x `height` from the origin.
     */
    draw(frame, input, width, height) {
      const U = frame.uniforms;
      gl.disable(gl.BLEND);
      atlas.ensure(kAtlasW, kAtlasH, gl.R16F);
      gobo.ensure(kGoboSize, kGoboSize, gl.RGBA16F);
      air.ensure(U.air.size[0], U.air.size[1], airFormat);

      // 1. The stir.
      atlas.clearTo(0, 0, 0, 0);
      {
        const p = programs.noise.use();
        p.setArray('Offset', U.noise.Offset, 4);
        p.setInt('Hooks', 0);
        quad.draw();
      }

      // 2. The gobo: the clip every frame, a wheel gobo when it changes.
      const kind = U.gobo.GoboKind;
      if (kind === Gobo.Clip || kind !== goboBuilt) {
        gobo.clearTo(0, 0, 0, 0);
        const p = programs.gobo.use();
        // Always a real texture on the clip's unit, even in the source: the
        // plugin binds the stir there when there is no clip.
        const clip = kind === Gobo.Clip ? input : null;
        bindTexture(gl, 0, clip ? clip.texture : atlas.texture);
        p.setSampler('InputTexture', 0);
        // GetMaxGLTexCoords: a browser texture is exactly the raster, so 1.
        p.set('MaxUV', 1.0, 1.0);
        p.set('ClipAspect', clip ? f32(f32(clip.width) / f32(Math.max(clip.height, 1))) : 1.0);
        p.setInt('GoboKind', kind);
        p.setInt('Hooks', 0);
        quad.draw();
        bindTexture(gl, 0, null);
        gobo.generateMipmap();
        goboBuilt = kind;
      }

      // 3. The air, at Quality's raster.
      air.clearTo(0, 0, 0, 0);
      {
        const p = programs.air.use();
        bindTexture(gl, 0, gobo.texture);
        bindTexture(gl, 1, atlas.texture);
        p.setSampler('Gobo', 0);
        p.setSampler('Density', 1);
        sceneUniforms(p, U.scene);
        p.set('TanHalf', U.scene.TanHalf);
        p.set('G', U.air.G);
        p.set('Stir', U.air.Stir);
        p.setInt('Samples', U.air.Samples);
        p.setInt('AirScale', U.air.AirScale);
        ivec2(p, 'FullSize', U.air.FullSize[0], U.air.FullSize[1]);
        quad.draw();
        bindTexture(gl, 1, null);
        bindTexture(gl, 0, null);
      }

      // 4. The frame, into the canvas.
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      {
        const p = programs.final.use();
        bindTexture(gl, 0, air.texture);
        bindTexture(gl, 1, input ? input.texture : atlas.texture);
        bindTexture(gl, 2, gobo.texture);
        p.setSampler('Air', 0);
        p.setSampler('InputTexture', 1);
        p.setSampler('Gobo', 2);
        sceneUniforms(p, U.scene);
        p.set('TanHalf', U.scene.TanHalf);
        p.set('MaxUV', 1.0, 1.0);
        p.set('ClipAspect', input ? f32(f32(input.width) / f32(Math.max(input.height, 1))) : f32(16.0 / 9.0));
        p.setInt('IsOver', U.final.IsOver);
        p.setInt('ClipAs', U.final.ClipAs);
        p.set('MixAmount', U.final.MixAmount);
        p.set('Gain', U.final.Gain);
        ivec4(p, 'Viewport', 0, 0, width, height);
        p.setInt('AirScale', U.final.AirScale);
        ivec2(p, 'AirSize', U.final.AirSize[0], U.final.AirSize[1]);
        p.set('BackAlbedo', U.final.BackAlbedo);
        p.set('FloorAlbedo', U.final.FloorAlbedo);
        p.set('ScreenHeight', U.final.ScreenHeight);
        p.set('ScreenBottom', U.final.ScreenBottom);
        quad.draw();
        bindTexture(gl, 2, null);
        bindTexture(gl, 1, null);
        bindTexture(gl, 0, null);
        gl.activeTexture(gl.TEXTURE0);
      }
    },

    /** Uniform names a program was asked for and does not have (the compiler drops unused ones). */
    missing() {
      return Object.fromEntries(Object.entries(programs).map(([name, p]) => [name, [...p.missing]]));
    },
  };
}
