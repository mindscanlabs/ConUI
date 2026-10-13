#pragma once

// Test scenes for the render core: what --dump prints and what the self-test
// photographs. They belong to the core, not to an application, and are built
// into the playground and the skeleton alike.

class CellBuffer;
class RasterStore;

/**
 * A stand-in screen for stages 2 and 2b: it exercises the parts of the
 * renderer that can fail quietly — frame joints, the eighth-blocks, the
 * shades, inverted rows and Cyrillic, and now the three layers around the
 * grid: surfaces, sub-cell geometry and raster. The last panel is an overlay,
 * so the band ordering is under test too.
 *
 * @param rasterId id from MakeDemoRaster(), or -1 when there is no GL context
 *                 (the --dump path builds the same scene without one).
 */
void BuildSelfTestScene(CellBuffer& cb, int rasterId = -1);

/**
 * Fills a LIVE window's body — everything below the caption.
 * Unlike BuildSelfTestScene it does not own the buffer: the window is
 * resizable, so the grid is whatever the framebuffer currently divides into
 * and the layout has to cope with any size, including absurdly small ones.
 *
 * @param top      first row it may use (chrome::CONTENT_TOP).
 * @param rasterId from MakeDemoRaster(), or -1.
 * @param compact  the secondary window's variant: fewer panels, no menu bar.
 */
void BuildStageScene(CellBuffer& cb, int top, int rasterId, bool compact);

/** Builds the demo picture and adds it to the store. Returns its id, or -1. */
int MakeDemoRaster(RasterStore& store);
