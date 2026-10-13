#include "Renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

const char* kVert = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
layout(location=3) in float aMode;
layout(location=4) in vec4 aShape;   // halfW, halfH, radius, stroke — in px
uniform vec2 uViewport;
out vec2 vUV;
out vec4 vColor;
out float vMode;
out vec4 vShape;
void main() {
    // pixel space -> clip space, y down
    vec2 ndc = vec2(aPos.x / uViewport.x * 2.0 - 1.0,
                    1.0 - aPos.y / uViewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
    vMode = aMode;
    vShape = aShape;
}
)";

const char* kFrag = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
in float vMode;
in vec4 vShape;
uniform sampler2D uAtlas;
uniform sampler2D uRaster;
out vec4 FragColor;

// Signed distance to a rounded rectangle centred on the origin. Negative
// inside. This is the whole of the shape layer (DESIGN_SYSTEM.md §2.1): a
// scrollbar thumb, a progress fill and a caption box are all this function
// with different parameters.
float sdRoundRect(vec2 p, vec2 half_, float r) {
    r = min(r, min(half_.x, half_.y));
    vec2 d = abs(p) - (half_ - r);
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

void main() {
    int mode = int(vMode + 0.5);
    if (mode == 1) {
        // The glyph atlas is single-channel coverage: the cell colour masked
        // by that coverage.
        FragColor = vec4(vColor.rgb, vColor.a * texture(uAtlas, vUV).r);
    } else if (mode == 2) {
        vec2 half_ = vShape.xy;
        float r = vShape.z, stroke = vShape.w;
        // 0.5 - d gives one pixel of analytic coverage across the edge, which
        // is why a 5 px thumb keeps clean round ends instead of stair-stepping.
        float cov = clamp(0.5 - sdRoundRect(vUV, half_, r), 0.0, 1.0);
        if (stroke > 0.0) {
            vec2 ih = max(half_ - stroke, vec2(0.0));
            float inner = clamp(0.5 - sdRoundRect(vUV, ih, max(r - stroke, 0.0)), 0.0, 1.0);
            cov = clamp(cov - inner, 0.0, 1.0);
        }
        FragColor = vec4(vColor.rgb, vColor.a * cov);
    } else if (mode == 4) {
        // A segment with round ends (the charts' lines, §8.20): the distance
        // to the line from the local origin to vShape.xy, minus half its
        // thickness — the same one pixel of analytic edge as the rectangles.
        vec2 ab = vShape.xy;
        float h = clamp(dot(vUV, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
        float d = length(vUV - ab * h) - vShape.z;
        FragColor = vec4(vColor.rgb, vColor.a * clamp(0.5 - d, 0.0, 1.0));
    } else if (mode == 3) {
        // The vertex colour is white for an ordinary picture and a palette
        // colour for a tinted mask (the caption icons).
        vec4 px = texture(uRaster, vUV);
        FragColor = vec4(px.rgb * vColor.rgb, px.a * vColor.a);
    } else {
        FragColor = vColor;
    }
}
)";

GLuint compile(GLenum type, const char* src, const char* label) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        gl::GetShaderInfoLog(s, (GLsizei)sizeof log, &n, log);
        std::fprintf(stderr, "Renderer: %s shader failed:\n%.*s\n", label, (int)n, log);
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

bool Renderer::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVert, "vertex");
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag, "fragment");
    if (!vs || !fs) return false;

    prog_ = gl::CreateProgram();
    gl::AttachShader(prog_, vs);
    gl::AttachShader(prog_, fs);
    gl::LinkProgram(prog_);
    GLint ok = 0;
    gl::GetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        gl::GetProgramInfoLog(prog_, (GLsizei)sizeof log, &n, log);
        std::fprintf(stderr, "Renderer: link failed:\n%.*s\n", (int)n, log);
        return false;
    }
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);

    u_viewport_ = gl::GetUniformLocation(prog_, "uViewport");
    u_atlas_    = gl::GetUniformLocation(prog_, "uAtlas");
    u_raster_   = gl::GetUniformLocation(prog_, "uRaster");

    // The buffer is shared between contexts; the vertex array object that
    // describes it is not, so VAOs are created lazily per context in vaoFor().
    gl::GenBuffers(1, &vbo_);

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // The raster page is filtered, unlike the glyph atlas: a picture gets
    // whatever box the layout hands it and is not pixel-aligned (§16).
    glGenTextures(1, &rtex_);
    glBindTexture(GL_TEXTURE_2D, rtex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

GLuint Renderer::vaoFor(void* ctx) {
    auto it = vaos_.find(ctx);
    if (it != vaos_.end()) return it->second;

    // OpenGL shares programs, buffers and textures between contexts created
    // with `share`, but NOT vertex array objects (nor framebuffers). A second
    // window therefore needs its own VAO over the same VBO — using the first
    // window's would be undefined behaviour that happens to look like nothing
    // being drawn.
    GLuint vao = 0;
    gl::GenVertexArrays(1, &vao);
    gl::BindVertexArray(vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = sizeof(Vertex);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, x));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, u));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, r));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, mode));
    gl::EnableVertexAttribArray(4);
    gl::VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, hw));
    gl::BindVertexArray(0);

    vaos_[ctx] = vao;
    return vao;
}

void Renderer::shutdown() {
    if (rtex_) { glDeleteTextures(1, &rtex_); rtex_ = 0; }
    if (tex_)  { glDeleteTextures(1, &tex_); tex_ = 0; }
    if (vbo_)  { gl::DeleteBuffers(1, &vbo_); vbo_ = 0; }
    // Each VAO belongs to a context that may already be gone; the driver frees
    // them with their context, so they are only dropped from the map here.
    vaos_.clear();
    if (prog_) { gl::DeleteProgram(prog_); prog_ = 0; }
}

void Renderer::syncAtlas(FontAtlas& atlas) {
    if (!atlas.dirty()) return;
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas.texW(), atlas.texH(), 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas.pixels());
    tex_w_ = atlas.texW();
    tex_h_ = atlas.texH();
    atlas.clearDirty();
}

void Renderer::syncRasters(RasterStore& rasters) {
    if (!rasters.dirty() || rasters.texW() <= 0) return;
    glBindTexture(GL_TEXTURE_2D, rtex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (!rasters.dirtyAll() && rtex_w_ == rasters.texW() && rtex_h_ == rasters.texH()) {
        // Only images redrawn in place: send their box, not the page — an
        // animation drawn on the CPU updates every frame.
        int x0, y0, x1, y1;
        rasters.dirtyRect(x0, y0, x1, y1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rasters.texW());
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE,
                        rasters.pixels() + ((size_t)y0 * rasters.texW() + x0) * 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        rasters.clearDirty();
        return;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rasters.texW(), rasters.texH(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rasters.pixels());
    rtex_w_ = rasters.texW();
    rtex_h_ = rasters.texH();
    rasters.clearDirty();
}

namespace {

/** Trims a pixel quad to a cell clip. False when nothing is left. */
bool clipQuad(const ClipRect& c, int cw, int ch, int ox, int oy,
              float& x0, float& y0, float& x1, float& y1) {
    const float cx0 = (float)(ox + c.x0 * cw), cy0 = (float)(oy + c.y0 * ch);
    const float cx1 = (float)(ox + c.x1 * cw), cy1 = (float)(oy + c.y1 * ch);
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    return x1 > x0 && y1 > y0;
}

} // namespace

void Renderer::pushQuad(float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        const Rgba& c, float alphaScale, Mode mode) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float a = (c.a / 255.0f) * alphaScale;
    const float m = (float)mode;
    const Vertex tl{ x,     y,     u0, v0, r, g, b, a, m, 0, 0, 0, 0 };
    const Vertex tr{ x + w, y,     u1, v0, r, g, b, a, m, 0, 0, 0, 0 };
    const Vertex br{ x + w, y + h, u1, v1, r, g, b, a, m, 0, 0, 0, 0 };
    const Vertex bl{ x,     y + h, u0, v1, r, g, b, a, m, 0, 0, 0, 0 };
    verts_.push_back(tl); verts_.push_back(tr); verts_.push_back(br);
    verts_.push_back(tl); verts_.push_back(br); verts_.push_back(bl);
}

void Renderer::pushSegment(const Shape& s, const Rgba& c, int cw, int ch,
                           float ox, float oy) {
    // Both ends in pixels; the quad is the bounding box, grown by half the
    // thickness plus the pixel of edge, trimmed to the clip. Local
    // coordinates are relative to the start, so the shader measures the
    // distance to the segment from 0 to (dx, dy).
    const float ax = ox + s.x * cw, ay = oy + s.y * ch;
    const float dx = s.w * cw, dy = s.h * ch;
    const float half = s.stroke_px * 0.5f, pad = half + 1.0f;
    float x0 = std::min(ax, ax + dx) - pad, x1 = std::max(ax, ax + dx) + pad;
    float y0 = std::min(ay, ay + dy) - pad, y1 = std::max(ay, ay + dy) + pad;
    if (!clipQuad(s.clip, cw, ch, (int)ox, (int)oy, x0, y0, x1, y1)) return;

    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float a = (c.a / 255.0f) * s.alpha;
    const float m = (float)MODE_SEGMENT;
    auto v = [&](float px_, float py_) {
        return Vertex{ px_, py_, px_ - ax, py_ - ay, r, g, b, a, m, dx, dy, half, 0.0f };
    };
    const Vertex tl = v(x0, y0), tr = v(x1, y0), br = v(x1, y1), bl = v(x0, y1);
    verts_.push_back(tl); verts_.push_back(tr); verts_.push_back(br);
    verts_.push_back(tl); verts_.push_back(br); verts_.push_back(bl);
}

void Renderer::pushShape(const Shape& s, const Rgba& c, int cw, int ch,
                         float ox, float oy) {
    if (s.kind == SHAPE_SEGMENT) { pushSegment(s, c, cw, ch, ox, oy); return; }
    // Cell fractions -> pixels. The radius is a fraction of the shorter side,
    // so a pill stays a pill whatever the cell aspect ratio is.
    const float px = ox + s.x * cw, py = oy + s.y * ch;
    const float pw = s.w * cw,      ph = s.h * ch;
    const float radius = s.rfrac * (pw < ph ? pw : ph);

    // One pixel of bleed on every side, so the analytic edge has somewhere to
    // fade out; without it the coverage gets clipped by the quad itself.
    const float pad = 1.0f;
    const float hw = pw * 0.5f, hh = ph * 0.5f;
    const float cx = px + hw,   cy = py + hh;

    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float a = (c.a / 255.0f) * s.alpha;
    const float m = (float)MODE_SHAPE;

    // The quad is trimmed to the clip; the SDF is not. Local coordinates stay
    // relative to the centre of the FULL shape, so a thumb half out of a
    // scroll region is cut straight at the edge rather than re-rounded there.
    float x0 = cx - hw - pad, x1 = cx + hw + pad;
    float y0 = cy - hh - pad, y1 = cy + hh + pad;
    if (!clipQuad(s.clip, cw, ch, ox, oy, x0, y0, x1, y1)) return;

    auto v = [&](float px_, float py_) {
        return Vertex{ px_, py_, px_ - cx, py_ - cy,
                       r, g, b, a, m, hw, hh, radius, s.stroke_px };
    };
    const Vertex tl = v(x0, y0), tr = v(x1, y0), br = v(x1, y1), bl = v(x0, y1);
    verts_.push_back(tl); verts_.push_back(tr); verts_.push_back(br);
    verts_.push_back(tl); verts_.push_back(br); verts_.push_back(bl);
}

Rgba Renderer::layerColor(const Theme& theme, uint8_t slot, uint8_t band) const {
    Rgba c = theme.c[slot < SLOT_COUNT ? slot : 0];
    // The section background has its own alpha (§6), applied to everything
    // painted in `bg.panel` in the content band: panel surfaces and the seam
    // fill that continues them. Overlays keep theirs — a modal must stay
    // readable over the dim whatever the sections are set to.
    if (slot == BG_PANEL && band == Z_CONTENT && panel_alpha_ < 1.0f)
        c.a = (uint8_t)std::lround(c.a * std::clamp(panel_alpha_, 0.0f, 1.0f));
    return c;
}

void Renderer::emitSurfaces(const CellBuffer& cells, const Theme& theme,
                            int cw, int ch, int ox, int oy, uint8_t band) {
    // A panel's frame line runs through the middle of its edge cells; its
    // ground stops at the line's outer edge there, not at the cell's, so it
    // never shows outside the frame (§8.1).
    const BoxLine L = BoxLineIn(cw, ch);
    for (const Surface& s : cells.surfaces()) {
        if (s.z != band) continue;
        const Rgba bg = layerColor(theme, s.slot, band);
        if (bg.a == 0) continue;
        int x0 = ox + s.x * cw, y0 = oy + s.y * ch;
        int x1 = ox + (s.x + s.w) * cw, y1 = oy + (s.y + s.h) * ch;
        if (s.framed & EDGE_L) x0 += L.x0;
        if (s.framed & EDGE_T) y0 += L.y0;
        if (s.framed & EDGE_R) x1 -= cw - (L.x0 + L.stroke);
        if (s.framed & EDGE_B) y1 -= ch - (L.y0 + L.stroke);
        if (s.framed & EDGE_CAP) y1 = oy + (s.y + s.h - 1) * ch + L.y0;   // down to the line's top
        // A title's cap stays inside its brackets: from the `[`'s ink to the
        // `]`'s, and no higher than their top.
        if (atlas_ && (s.framed & EDGE_INK_L)) {
            const GlyphInfo& g = atlas_->glyph(cells.top(s.x, s.y).cp);
            if (!g.blank) { x0 = ox + s.x * cw + g.dx; y0 = std::max(y0, oy + s.y * ch + g.dy); }
        }
        if (atlas_ && (s.framed & EDGE_INK_R)) {
            const int lx = s.x + s.w - 1;
            const GlyphInfo& g = atlas_->glyph(cells.top(lx, s.y).cp);
            if (!g.blank) { x1 = ox + lx * cw + g.dx + g.aw; y0 = std::max(y0, oy + s.y * ch + g.dy); }
        }
        // A raised top frame (under a seam) takes the ground's top edge — and
        // a title's cap, which is all in that row — up with it.
        if (band == Z_CONTENT) {
            const int r = raiseAt(cells, s.x, s.y);
            y0 -= r;
            if (s.framed & EDGE_CAP) y1 -= r;
        }
        if (x1 <= x0 || y1 <= y0) continue;
        pushQuad((float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0), 0, 0, 0, 0, bg, 1.0f, MODE_FLAT);
    }
}

void Renderer::emitShapes(const CellBuffer& cells, const Theme& theme,
                          int cw, int ch, int ox, int oy, uint8_t band) {
    for (const Shape& s : cells.shapes()) {
        if (s.z != band) continue;
        const Rgba c = layerColor(theme, s.slot, band);
        if (c.a == 0 || s.alpha <= 0.0f) continue;
        pushShape(s, c, cw, ch, (float)ox, (float)oy);
    }
}

void Renderer::emitSprites(const CellBuffer& cells, const Theme& theme, const RasterStore* rasters,
                           int cw, int ch, int ox, int oy, uint8_t band) {
    if (!rasters || rtex_w_ <= 0) return;
    const float tw = (float)rtex_w_, th = (float)rtex_h_;
    for (const SpriteRef& s : cells.sprites()) {
        if (s.z != band) continue;
        const RasterInfo& ri = rasters->get(s.id);
        if (!ri.valid) continue;
        Rgba white{ 255, 255, 255, 255 };
        if (s.tint < SLOT_COUNT) { white = theme.c[s.tint]; white.a = 255; }
        const float fx0 = ox + s.x * cw, fy0 = oy + s.y * ch;
        const float fx1 = fx0 + s.w * cw, fy1 = fy0 + s.h * ch;
        float x0 = fx0, y0 = fy0, x1 = fx1, y1 = fy1;
        if (!clipQuad(s.clip, cw, ch, ox, oy, x0, y0, x1, y1)) continue;
        // UVs follow the trimmed edges, so a clipped picture is cropped, not
        // squeezed into what is left of its box.
        const float u0 = ri.ax / tw, v0 = ri.ay / th;
        const float du = ri.aw / tw, dv = ri.ah / th;
        pushQuad(x0, y0, x1 - x0, y1 - y0,
                 u0 + du * (x0 - fx0) / (fx1 - fx0), v0 + dv * (y0 - fy0) / (fy1 - fy0),
                 u0 + du * (x1 - fx0) / (fx1 - fx0), v0 + dv * (y1 - fy0) / (fy1 - fy0),
                 white, s.alpha, MODE_SPRITE);
    }
}

void Renderer::draw(const CellBuffer& cells, FontAtlas& atlas, const Theme& theme,
                    int fbW, int fbH, int ox, int oy, RasterStore* rasters, void* ctx,
                    float windowAlpha, float row0PadPx, float panelAlpha) {
    panel_alpha_ = panelAlpha;
    atlas_ = &atlas;
    syncAtlas(atlas);
    if (rasters) syncRasters(*rasters);
    verts_.clear();

    const int cw = atlas.cellW(), ch = atlas.cellH();
    const float tw = tex_w_ > 0 ? (float)tex_w_ : 1.0f;
    const float th = tex_h_ > 0 ? (float)tex_h_ : 1.0f;
    const BoxLine L = BoxLineIn(cw, ch);
    // How far a panel's top frame under a horizontal seam is drawn raised:
    // half the space between the seam's line (on the seam row's top edge,
    // as thick as the vertical seam's hairline) and the frame line (§8.17).
    {
        const int seamLine = std::max(1, (int)std::lround(cw * 0.12f));
        raise_px_ = std::max(0, (ch + L.y0 - seamLine) / 2);
        half_px_ = ch / 2;
    }
    raised_rows_.assign((size_t)std::max(0, cells.height()), 0);
    for (const CellBuffer::Raise& r : cells.raises())
        if (r.y >= 0 && r.y < cells.height()) raised_rows_[(size_t)r.y] = 1;
    // A framed surface — a panel, a popup — paints its own ground, cut to its
    // frame line (§8.1). A cell inside it whose background is that same slot
    // must not paint its whole cell over it, or the ground spills half a cell
    // past the frame again. Per band: the topmost framed surface over a cell.
    const int gw = cells.width(), gh = cells.height();
    for (std::vector<uint8_t>& g : ground_) g.assign((size_t)std::max(0, gw * gh), kNoGround);
    for (const Surface& s : cells.surfaces()) {
        if (!(s.framed & EDGE_ALL)) continue;
        std::vector<uint8_t>& g = ground_[s.z == Z_OVERLAY ? 1 : 0];
        for (int y = std::max(0, (int)s.y); y < std::min(gh, s.y + s.h); ++y)
            for (int x = std::max(0, (int)s.x); x < std::min(gw, s.x + s.w); ++x)
                g[(size_t)y * gw + x] = s.slot;
    }

    // Pass 1 — the window background, one rectangle under everything. It is a
    // layer of its own, not a property of the cells that sit on it (§2). It
    // covers the FULL framebuffer, not just the cell grid: the grid is
    // floor(size/cell), and the leftover strip along the right/bottom edge
    // (§8.1) is still window background and must carry the same alpha.
    {
        const Rgba& wbg = theme.c[BG_WINDOW];
        if (wbg.a) {
            Rgba bg = wbg;
            bg.a = (uint8_t)std::clamp(std::lround(bg.a * std::clamp(windowAlpha, 0.0f, 1.0f)),
                                       0L, 255L);
            pushQuad(0.0f, 0.0f, (float)fbW, (float)fbH, 0, 0, 0, 0, bg, 1.0f, MODE_FLAT);
        }
    }

    // Passes 2–4 — the window's own content: its surfaces, its geometry, its
    // pictures. All of it finished before anything floating is laid on top.
    emitSurfaces(cells, theme, cw, ch, ox, oy, Z_CONTENT);
    emitShapes  (cells, theme, cw, ch, ox, oy, Z_CONTENT);
    emitSprites (cells, theme, rasters, cw, ch, ox, oy, Z_CONTENT);

    // Pass 5 — the window's per-cell backgrounds. BG_WINDOW and BG_PANEL are
    // LAYER colours: a cell carrying one of them inherits what the surface
    // below already painted instead of painting again, which is what stops
    // the alpha being applied twice to the same pixels. Every cell is
    // painted, an overlaid one included: a popup's ground stops at its frame
    // line (§8.1), and what shows in the half cell outside it is the window
    // carrying on underneath, not bare panel.
    for (int y = 0; y < cells.height(); ++y) {
        for (int x = 0; x < cells.width(); ++x) {
            const Cell& c = cells.base(x, y);
            if (c.bg == BG_WINDOW || c.bg == BG_PANEL) continue;
            if (ground_[0][(size_t)y * gw + x] == c.bg) continue;
            const Rgba& bg = theme.c[c.bg < SLOT_COUNT ? c.bg : 0];
            if (bg.a == 0) continue;
            const int r = raiseAt(cells, x, y);
            pushQuad((float)(ox + x * cw), (float)(oy + y * ch - r), (float)cw, (float)ch,
                     0, 0, 0, 0, bg, 1.0f, MODE_FLAT);
        }
    }

    // Pass 6 — the modal tint (§8.9): a solid translucent wash over the whole
    // window, drawn once the window is finished and before the modal itself,
    // so the card on top stays at full strength and what surrounds it — right
    // up to its frame line — is the dimmed window showing through.
    if (cells.dimBase()) {
        const Rgba& sh = theme.c[SHADOW];
        if (sh.a) pushQuad(0.0f, 0.0f, (float)fbW, (float)fbH, 0, 0, 0, 0, sh, 1.0f, MODE_FLAT);
    }

    // Pass 7 — the overlays: their surfaces, then the cells on them, then
    // their geometry. Menus, tooltips, modals.
    emitSurfaces(cells, theme, cw, ch, ox, oy, Z_OVERLAY);
    for (int y = 0; y < cells.height(); ++y) {
        for (int x = 0; x < cells.width(); ++x) {
            if (!cells.overlaid(x, y)) continue;
            const Cell c = cells.top(x, y);
            if (c.bg == BG_WINDOW || c.bg == BG_PANEL) continue;
            if (ground_[1][(size_t)y * gw + x] == c.bg) continue;
            const Rgba& bg = theme.c[c.bg < SLOT_COUNT ? c.bg : 0];
            if (bg.a == 0) continue;
            pushQuad((float)(ox + x * cw), (float)(oy + y * ch), (float)cw, (float)ch,
                     0, 0, 0, 0, bg, 1.0f, MODE_FLAT);
        }
    }
    emitShapes(cells, theme, cw, ch, ox, oy, Z_OVERLAY);

    // Pass 8 — glyphs, always opaque (alpha applies to the cell, not the
    // text). Box glyphs overhang their cell on purpose and are not clipped.
    for (int y = 0; y < cells.height(); ++y) {
        for (int x = 0; x < cells.width(); ++x) {
            const Cell c = cells.top(x, y);   // overlay over content, dim applied
            if (c.cp == ' ' || c.cp == 0) continue;
            const GlyphInfo& gi = atlas.glyph(c.cp);
            if (gi.blank) continue;

            Rgba fg = theme.c[c.fg < SLOT_COUNT ? c.fg : 8];
            fg.a = 255;
            if (c.attr & ATTR_DIM) {
                fg.r = (uint8_t)(fg.r * 0.6f);
                fg.g = (uint8_t)(fg.g * 0.6f);
                fg.b = (uint8_t)(fg.b * 0.6f);
            }

            // The caption's own row is a third taller than the rest (§8.1);
            // the caller says how far to nudge row 0's glyphs so the title
            // sits padded inside that band. In fullscreen it is 0.
            const float rowPad = y == 0 ? row0PadPx : 0.0f;
            // A raised top frame (§8.17): the row is drawn higher, and a frame
            // stroke that ran down to the cell's bottom is carried on to it,
            // so the verticals below still meet the raised line.
            const int raise = cells.overlaid(x, y) ? 0 : raiseAt(cells, x, y);
            const float gx = (float)(ox + x * cw + gi.dx);
            const float gy = (float)(oy + y * ch + gi.dy - raise) + rowPad;
            const float u0 = gi.ax / tw, v0 = gi.ay / th;
            const float u1 = (gi.ax + gi.aw) / tw, v1 = (gi.ay + gi.ah) / th;

            pushQuad(gx, gy, (float)gi.aw, (float)gi.ah, u0, v0, u1, v1, fg, 1.0f, MODE_GLYPH);
            if (raise > 0 && FontAtlas::hasDownArm(c.cp))
                pushQuad((float)(ox + x * cw + L.x0), (float)(oy + y * ch + ch - raise),
                         (float)L.stroke, (float)raise, 0, 0, 0, 0, fg, 1.0f, MODE_FLAT);
            // Synthetic bold: the same glyph again, one pixel right (§7).
            if (c.attr & ATTR_BOLD)
                pushQuad(gx + 1, gy, (float)gi.aw, (float)gi.ah, u0, v0, u1, v1, fg, 1.0f, MODE_GLYPH);
            if (c.attr & ATTR_UNDER)
                pushQuad((float)(ox + x * cw), (float)(oy + y * ch + ch - 2),
                         (float)cw, 1.0f, 0, 0, 0, 0, fg, 1.0f, MODE_FLAT);
        }
    }

    // Pass 8 — raster elements belonging to the overlays (§16).
    emitSprites(cells, theme, rasters, cw, ch, ox, oy, Z_OVERLAY);

    last_verts_ = verts_.size();
    if (verts_.empty()) return;

    glViewport(0, 0, fbW, fbH);
    glEnable(GL_BLEND);
    // Colour and alpha need DIFFERENT blend factors here. GL_SRC_ALPHA for
    // both (the naive glBlendFunc call) makes the alpha channel SHRINK every
    // time something translucent draws over something opaque — dstA' =
    // srcA² + dstA·(1−srcA), not srcA + dstA·(1−srcA) — because it applies
    // srcA to the alpha channel a second time. That is invisible with a
    // solid backdrop, but this window's framebuffer alpha is what DWM
    // composites against the desktop (§6): every antialiased glyph edge
    // — and a thin box-drawing stroke like │ is nearly ALL edge — was
    // quietly eating into the window's own opacity, which read as the glyph
    // itself looking translucent. GL_ONE for the alpha channel's source
    // factor is the standard fix: it lets alpha accumulate toward 1 instead
    // of being multiplied down by every layer drawn on top of it.
    gl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    gl::UseProgram(prog_);
    gl::Uniform2f(u_viewport_, (float)fbW, (float)fbH);
    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    gl::Uniform1i(u_atlas_, 0);
    gl::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, rtex_);
    gl::Uniform1i(u_raster_, 1);
    gl::ActiveTexture(GL_TEXTURE0);

    gl::BindVertexArray(vaoFor(ctx));
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, (GLsizeiptrCONUI)(verts_.size() * sizeof(Vertex)),
                   verts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts_.size());
    gl::BindVertexArray(0);
}
