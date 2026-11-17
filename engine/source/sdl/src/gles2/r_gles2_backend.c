
/* SOFT=1 builds replace this whole file with r_soft_backend.c. */
#ifndef SONICR_SOFT_RENDER

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>

#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "r_texture.h"
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include <string.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, js_perf2, (int drawMs10, int polys), {
    if (window.SonicRPerf2) window.SonicRPerf2(drawMs10 / 10, polys);
});

static double s_drawMs = 0.0;
static int s_drawPolys = 0;
static double s_stateMs = 0.0;
static double s_primMs = 0.0;
static double s_upMs = 0.0;
static int s_ups = 0;
static int s_reportFrames = 0;
static double s_reportStart = 0.0;

EM_JS(void, js_perf3, (int stateMs10, int primMs10, int upMs10, int ups), {
    if (window.SonicRPerf3) window.SonicRPerf3(stateMs10 / 10, primMs10 / 10, upMs10 / 10, ups);
});

void WebDrawReport(void)
{
    double now = emscripten_get_now();
    if (s_reportStart == 0.0) {
        s_reportStart = now;
    }
    s_reportFrames++;
    if (now - s_reportStart >= 1000.0) {
        js_perf2((int)(s_drawMs * 10.0 / s_reportFrames), s_drawPolys / s_reportFrames);
        js_perf3((int)((s_stateMs - s_upMs) * 10.0 / s_reportFrames), (int)(s_primMs * 10.0 / s_reportFrames), (int)(s_upMs * 10.0 / s_reportFrames), s_ups / s_reportFrames);
        s_reportStart = now;
        s_reportFrames = 0;
        s_drawMs = 0.0;
        s_stateMs = 0.0;
        s_primMs = 0.0;
        s_upMs = 0.0;
        s_ups = 0;
        s_drawPolys = 0;
    }
}
#endif

/* Forward declarations for existing frame functions in render_gl.c */
extern void BeginFrame(void);
extern void EndFrame(void);
extern void FlipD3D(void);
extern void ProcessTpageStates(void);

/* Forward declarations for existing GL_* texture functions in render_gl.c */
extern void GL_UploadTpage(int tpage);
extern void GL_UploadTpageRGBA(int tpage, unsigned char *rgba, int w, int h);
extern void GL_UploadTpageSubRect(int tpage, int destX, int destY, int width, int height);

#ifdef __EMSCRIPTEN__
static void WebUpload(int tp)
{
    double t0 = emscripten_get_now();
    GL_UploadTpage(tp);
    s_upMs += emscripten_get_now() - t0;
    s_ups++;
}
#else
static void WebUpload(int tp)
{
    GL_UploadTpage(tp);
}
#endif
extern void GL_MarkTpageDirty(int tpage);
extern void GL_ClearTpageDirty(int tpage);
extern void GL_FreezeTpage(int tpage);
extern void GL_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h);
extern void GL_SetNoColorKey(int tpage);
extern void GL_ClearNoColorKey(int tpage);
extern void GL_SetTpageGreen6(int tpage, int on);
extern void GL_InitTextures(void);
extern void GL_SetTpageRGBA8(int tpage, unsigned char *rgba);

/* =====================================================================
 * Internal state snapshot
 * ===================================================================== */

typedef struct {
    int          textureId;    /* tpage index, or -1 for untextured */
    R_BlendMode  blendMode;
    int          depthTest;
    R_DepthFunc  depthFunc;
    int          depthWrite;
    R_TexEnvMode texEnv;
    R_FilterMode filter;
    R_CullMode   cullMode;
    int          alphaTest;
    float        alphaRef;
    int          scissorEnabled;
    int          scissorX, scissorY, scissorW, scissorH;
} R_StateSnapshot;

static R_StateSnapshot s_desired;
static R_StateSnapshot s_current;

/* State stack for R_PushState / R_PopState */
#define R_STATE_STACK_DEPTH 4
static R_StateSnapshot s_stateStack[R_STATE_STACK_DEPTH];
static int s_stackDepth = 0;

/* Default state values */
static const R_StateSnapshot s_defaults = {
    .textureId      = -1,
    .blendMode      = R_BLEND_ALPHA,
    .depthTest      = 1,
    .depthFunc      = R_DEPTH_LEQUAL,
    .depthWrite     = 1,
    .texEnv         = R_TEXENV_MODULATE,
    .filter         = R_FILTER_NEAREST,
    .cullMode       = R_CULL_NONE,
    .alphaTest      = 1,
    .alphaRef       = 0.01f,
    .scissorEnabled = 0,
    .scissorX       = 0,
    .scissorY       = 0,
    .scissorW       = 640,
    .scissorH       = 480,
};

/* =====================================================================
 * Texture backend state
 * ===================================================================== */

/* GL texture objects and dirty flags — still owned by render_gl.c,
 * accessed here via extern for texture binding during R_FlushState. */
extern GLuint s_glTextures[];       /* render_gl.c */

/* Per-tpage filter override, and the filter actually resident on each GL
 * texture object.
 *
 * GL keeps filter state on the TEXTURE OBJECT, not globally — so a single
 * cached "current filter" is wrong the moment a different texture is bound.
 * It is also wrong after any upload: every GL_UploadTpage* path hard-sets
 * GL_NEAREST (render_gl.c:139, :247), so a filter applied earlier is stomped
 * whenever the tpage is re-uploaded. Together those made the old global
 * R_SetFilter path reach only whichever texture happened to be bound at the
 * moment the value changed, and nothing else.
 *
 * The filter is therefore resolved and applied per tpage at BIND time. Both
 * arrays encode 0 = unset and (mode + 1) otherwise, so the zero initialiser
 * means "no override" / "resident filter unknown". s_glTextureFilter is
 * invalidated by R_MarkTextureDirty, which precedes every upload path. */
static unsigned char s_tpageFilter[52];
static unsigned char s_glTextureFilter[52];

/* Apply the effective filter to tpage `tp`, which must be bound already. */
static void GL_ApplyTpageFilter(int tp)
{
    unsigned char want = s_tpageFilter[tp];
    if (want == 0) {
        want = (unsigned char)(s_desired.filter + 1);
    }
    if (s_glTextureFilter[tp] != want) {
        GLenum f = (want == (unsigned char)(R_FILTER_LINEAR + 1)) ? GL_LINEAR
                                                                  : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        s_glTextureFilter[tp] = want;
    }
}

/* =====================================================================
 * R_FlushState — apply only the GL calls for state that changed
 * ===================================================================== */

#define GV_MAX 12000

typedef struct {
    float x, y, z, w;
    unsigned char r, g, b, a;
    float u, v;
} GVert;

static GVert s_gv[GV_MAX];
static int s_gvCount = 0;
static GLuint s_prog = 0;
static GLuint s_vbo = 0;
static GLint s_uTexOn = -1;
static GLint s_uAlphaTest = -1;
static GLint s_uAlphaRef = -1;
static float s_shTexOn = -1.0f;
static float s_shAlphaTest = -1.0f;
static float s_shAlphaRef = -2.0f;
static int s_dirtyApply = 1;
static int s_inDraw = 0;

static const char *kVertSrc =
    "attribute vec4 aPos;\n"
    "attribute vec4 aCol;\n"
    "attribute vec2 aUv;\n"
    "varying vec4 vCol;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "  gl_Position = aPos;\n"
    "  vCol = aCol;\n"
    "  vUv = aUv;\n"
    "}\n";

static const char *kFragSrc =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec4 vCol;\n"
    "varying vec2 vUv;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uTexOn;\n"
    "uniform float uAlphaTest;\n"
    "uniform float uAlphaRef;\n"
    "void main() {\n"
    "  vec4 c = vCol;\n"
    "  if (uTexOn > 0.5) {\n"
    "    c *= texture2D(uTex, vUv);\n"
    "  }\n"
    "  if (uAlphaTest > 0.5 && c.a <= uAlphaRef) {\n"
    "    discard;\n"
    "  }\n"
    "  gl_FragColor = c;\n"
    "}\n";

static GLuint R_CompileShader(GLenum type, const char *text)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &text, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        log[0] = 0;
        glGetShaderInfoLog(s, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "gles2: shader compile failed: %s\n", log);
    }
    return s;
}

void R_GLES2_InitShaders(void)
{
    if (s_prog != 0) {
        return;
    }
    GLuint vs = R_CompileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = R_CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glBindAttribLocation(s_prog, 0, "aPos");
    glBindAttribLocation(s_prog, 1, "aCol");
    glBindAttribLocation(s_prog, 2, "aUv");
    glLinkProgram(s_prog);
    GLint ok = 0;
    glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        log[0] = 0;
        glGetProgramInfoLog(s_prog, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "gles2: program link failed: %s\n", log);
    }
    glUseProgram(s_prog);
    s_uTexOn = glGetUniformLocation(s_prog, "uTexOn");
    s_uAlphaTest = glGetUniformLocation(s_prog, "uAlphaTest");
    s_uAlphaRef = glGetUniformLocation(s_prog, "uAlphaRef");
    glUniform1i(glGetUniformLocation(s_prog, "uTex"), 0);
    glGenBuffers(1, &s_vbo);
    s_shTexOn = -1.0f;
    s_shAlphaTest = -1.0f;
    s_shAlphaRef = -2.0f;
    s_dirtyApply = 1;
}

void R_GLES2_FlushVerts(void)
{
    if (s_gvCount == 0) {
        return;
    }
    double t0 = emscripten_get_now();
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)s_gvCount * sizeof(GVert)), s_gv, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(GVert), (const void *)0);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)sizeof(GVert), (const void *)16);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(GVert), (const void *)20);
    glDrawArrays(GL_TRIANGLES, 0, s_gvCount);
    s_gvCount = 0;
    double dt = emscripten_get_now() - t0;
    s_primMs += dt;
    if (!s_inDraw) {
        s_drawMs += dt;
    }
}

void R_GLES2_Sync(void)
{
    R_GLES2_FlushVerts();
    s_current.textureId = -99;
    s_dirtyApply = 1;
}

static void R_GLES2_Apply(void)
{
    double t0 = emscripten_get_now();
    if (s_prog == 0) {
        R_GLES2_InitShaders();
    }
    int tp = s_desired.textureId;
    float texOn = 0.0f;
    if (tp >= 0 && tp < 52 && g_tpagePixelBuf[tp] != NULL) {
        if (s_glTextureDirty[tp]) {
            WebUpload(tp);
        }
        glBindTexture(GL_TEXTURE_2D, s_glTextures[tp]);
        GL_ApplyTpageFilter(tp);
        texOn = 1.0f;
    }
    s_current.textureId = s_desired.textureId;

    if (s_desired.depthTest != s_current.depthTest) {
        if (s_desired.depthTest) {
            glEnable(GL_DEPTH_TEST);
        }
        else {
            glDisable(GL_DEPTH_TEST);
        }
        s_current.depthTest = s_desired.depthTest;
    }
    if (s_desired.depthFunc != s_current.depthFunc) {
        switch (s_desired.depthFunc) {
            case R_DEPTH_LEQUAL:
                glDepthFunc(GL_LEQUAL);
                break;
            case R_DEPTH_LESS:
                glDepthFunc(GL_LESS);
                break;
            case R_DEPTH_ALWAYS:
                glDepthFunc(GL_ALWAYS);
                break;
        }
        s_current.depthFunc = s_desired.depthFunc;
    }
    if (s_desired.depthWrite != s_current.depthWrite) {
        glDepthMask(s_desired.depthWrite ? GL_TRUE : GL_FALSE);
        s_current.depthWrite = s_desired.depthWrite;
    }
    s_current.texEnv = s_desired.texEnv;
    if (s_desired.blendMode != s_current.blendMode) {
        switch (s_desired.blendMode) {
            case R_BLEND_NONE:
                glDisable(GL_BLEND);
                break;
            case R_BLEND_ALPHA:
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case R_BLEND_ADDITIVE:
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
        }
        s_current.blendMode = s_desired.blendMode;
    }
    s_current.filter = s_desired.filter;
    glDisable(GL_CULL_FACE);

    float at = s_desired.alphaTest ? 1.0f : 0.0f;
    if (texOn != s_shTexOn) {
        glUniform1f(s_uTexOn, texOn);
        s_shTexOn = texOn;
    }
    if (at != s_shAlphaTest) {
        glUniform1f(s_uAlphaTest, at);
        s_shAlphaTest = at;
    }
    if (s_desired.alphaRef != s_shAlphaRef) {
        glUniform1f(s_uAlphaRef, s_desired.alphaRef);
        s_shAlphaRef = s_desired.alphaRef;
    }
    s_current.alphaTest = s_desired.alphaTest;
    s_current.alphaRef = s_desired.alphaRef;

    if (s_desired.scissorEnabled != s_current.scissorEnabled) {
        if (s_desired.scissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        }
        else {
            glDisable(GL_SCISSOR_TEST);
        }
        s_current.scissorEnabled = s_desired.scissorEnabled;
    }
    if (s_desired.scissorEnabled &&
        (s_desired.scissorX != s_current.scissorX ||
         s_desired.scissorY != s_current.scissorY ||
         s_desired.scissorW != s_current.scissorW ||
         s_desired.scissorH != s_current.scissorH))
    {
        glScissor(s_desired.scissorX, s_desired.scissorY,
                  s_desired.scissorW, s_desired.scissorH);
        s_current.scissorX = s_desired.scissorX;
        s_current.scissorY = s_desired.scissorY;
        s_current.scissorW = s_desired.scissorW;
        s_current.scissorH = s_desired.scissorH;
    }
    s_stateMs += emscripten_get_now() - t0;
}

void R_FlushState(void)
{
    R_GLES2_FlushVerts();
    R_GLES2_Apply();
    s_dirtyApply = 0;
}

/* =====================================================================
 * Render state API implementation
 * ===================================================================== */

void R_SetTexture(int tpageIndex)
{
    s_desired.textureId = tpageIndex;
}

void R_SetBlendMode(R_BlendMode mode)
{
    s_desired.blendMode = mode;
}

void R_SetDepthTest(int enable)
{
    s_desired.depthTest = enable;
}

void R_SetDepthFunc(R_DepthFunc func)
{
    s_desired.depthFunc = func;
}

void R_SetDepthWrite(int enable) 
{
    s_desired.depthWrite = enable;
}

void R_SetTexEnv(R_TexEnvMode mode)
{
    s_desired.texEnv = mode;
}

void R_SetFilter(R_FilterMode mode)
{
    s_desired.filter = mode;
}

/* Per-tpage filter pin. Takes priority over the global R_SetFilter value;
 * applied to the texture object at bind time by GL_ApplyTpageFilter. */
void R_SetTpageFilter(int tpage, R_FilterMode mode)
{
    R_GLES2_Sync();
    if (tpage >= 0 && tpage < 52) {
        s_tpageFilter[tpage] = (unsigned char)(mode + 1);
    }
}

void R_ClearTpageFilter(int tpage)
{
    R_GLES2_Sync();
    if (tpage >= 0 && tpage < 52) {
        s_tpageFilter[tpage] = 0;
    }
}

/* Chroma gain is a DC-only upload-time transform — GL uploads RGBA directly
 * and the SDL path renders true Add Signed, so there is nothing to correct. */
void R_SetTpageSatBoost(int tpage, int k256)
{
    R_GLES2_Sync();
    (void)tpage;
    (void)k256;
}

void R_SetCullMode(R_CullMode mode)
{
    s_desired.cullMode = mode;
}

void R_SetAlphaTest(int enable)
{
    s_desired.alphaTest = enable;
}

void R_SetAlphaRef(float ref)
{
    s_desired.alphaRef = ref;
}

void R_SetScissor(int x, int y, int w, int h)
{
    s_desired.scissorEnabled = 1;
    s_desired.scissorX = x;
    s_desired.scissorY = y;
    s_desired.scissorW = w;
    s_desired.scissorH = h;
}

void R_DisableScissor(void)
{
    s_desired.scissorEnabled = 0;
}

void R_PushState(void)
{
    if (s_stackDepth < R_STATE_STACK_DEPTH) {
        s_stateStack[s_stackDepth++] = s_desired;
    }
}

void R_PopState(void)
{
    if (s_stackDepth > 0) {
        s_desired = s_stateStack[--s_stackDepth];
    }
}

void R_DebugGetState(int *depthTest, int *depthWrite, int *depthFunc,
                     int *blendMode, int *alphaTest, float *alphaRef)
{
    if (depthTest)  *depthTest  = s_desired.depthTest;
    if (depthWrite) *depthWrite = s_desired.depthWrite;
    if (depthFunc)  *depthFunc  = (int)s_desired.depthFunc;
    if (blendMode)  *blendMode  = (int)s_desired.blendMode;
    if (alphaTest)  *alphaTest  = s_desired.alphaTest;
    if (alphaRef)   *alphaRef   = s_desired.alphaRef;
}

void R_ResetState(void)
{
    s_desired = s_defaults;
    /* Force full re-sync on next R_FlushState by invalidating current.
     * Can't use plain memset(0xFF) because 0xFFFFFFFF == -1, which matches
     * s_defaults.textureId — so textureId would appear "unchanged" and
     * R_FlushState would skip the glDisable(GL_TEXTURE_2D) call. */
    memset(&s_current, 0xFF, sizeof(s_current));
    s_current.textureId = -99;   /* won't match any valid id or -1 */
    s_current.alphaRef = -1.0f;  /* ensure float comparison triggers */
    /* Resident texture-object filters are not part of this snapshot; drop
     * what we believe about them so the next bind re-applies. */
    memset(s_glTextureFilter, 0, sizeof(s_glTextureFilter));
    s_stackDepth = 0;
}

/* =====================================================================
 * Geometry submission — immediate draw
 * ===================================================================== */

static void R_GLES2_Push(const RenderVertex *v, float sw, float sh)
{
    GVert *o = &s_gv[s_gvCount++];
    float w = (v->rhw > 0.0f) ? (1.0f / v->rhw) : 1.0f;
    o->x = (v->sx * sw - 1.0f) * w;
    o->y = (1.0f - v->sy * sh) * w;
    o->z = (v->sz * 2.0f - 1.0f) * w;
    o->w = w;
    uint32_t c = v->color;
    o->r = (unsigned char)((c >> 16) & 0xFF);
    o->g = (unsigned char)((c >> 8) & 0xFF);
    o->b = (unsigned char)(c & 0xFF);
    o->a = (unsigned char)((c >> 24) & 0xFF);
    o->u = v->u;
    o->v = v->v;
}

void R_DrawTriFan(const RenderVertex *v, int count)
{
    if (count < 3) {
        return;
    }
    double t0 = emscripten_get_now();
    s_inDraw = 1;
    int need = s_dirtyApply || memcmp(&s_desired, &s_current, sizeof(R_StateSnapshot)) != 0;
    int tex = s_desired.textureId;
    if (!need && tex >= 0 && tex < 52 && s_glTextureDirty[tex]) {
        need = 1;
    }
    if (need) {
        R_GLES2_FlushVerts();
        R_GLES2_Apply();
        s_dirtyApply = 0;
    }
    if (s_gvCount + (count - 2) * 3 > GV_MAX) {
        R_GLES2_FlushVerts();
    }
    float sw = 2.0f / (float)(g_screenWidth > 0 ? g_screenWidth : 640);
    float sh = 2.0f / (float)(g_screenHeight > 0 ? g_screenHeight : 480);
    for (int i = 1; i + 1 < count; i++) {
        R_GLES2_Push(&v[0], sw, sh);
        R_GLES2_Push(&v[i], sw, sh);
        R_GLES2_Push(&v[i + 1], sw, sh);
    }
    s_inDraw = 0;
    s_drawMs += emscripten_get_now() - t0;
    s_drawPolys++;
}

void R_DrawTri(const RenderVertex v[3])
{
    R_DrawTriFan(v, 3);
}

void R_DrawQuad(const RenderVertex v[4])
{
    R_DrawTriFan(v, 4);
}

/* =====================================================================
 * 2D quad helpers
 * ===================================================================== */

void R_DrawQuad2D(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  float z, uint32_t color)
{
    float rhw = (z > 0.0f) ? (1.0f / z) : 1.0f;
    float farSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 1.0f;
    float normZ = z / farSafe;

    RenderVertex v[4];
    v[0] = (RenderVertex){ x0, y0, normZ, rhw, color, 0, u0, v0 };
    v[1] = (RenderVertex){ x1, y0, normZ, rhw, color, 0, u1, v0 };
    v[2] = (RenderVertex){ x1, y1, normZ, rhw, color, 0, u1, v1 };
    v[3] = (RenderVertex){ x0, y1, normZ, rhw, color, 0, u0, v1 };
    R_DrawQuad(v);
}

void R_DrawQuad2DSolid(float x0, float y0, float x1, float y1,
                       float z, uint32_t color)
{
    /* Temporarily force untextured rendering */
    int savedTex = s_desired.textureId;
    s_desired.textureId = -1;

    float rhw = (z > 0.0f) ? (1.0f / z) : 1.0f;
    float farSafe = (g_farClipFloat > 0.0f) ? g_farClipFloat : 1.0f;
    float normZ = z / farSafe;

    RenderVertex v[4];
    v[0] = (RenderVertex){ x0, y0, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[1] = (RenderVertex){ x1, y0, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[2] = (RenderVertex){ x1, y1, normZ, rhw, color, 0, 0.0f, 0.0f };
    v[3] = (RenderVertex){ x0, y1, normZ, rhw, color, 0, 0.0f, 0.0f };
    R_DrawQuad(v);

    /* Restore previous texture state */
    s_desired.textureId = savedTex;
}

/* =====================================================================
 * Texture API (forward to render_gl.c)
 * ===================================================================== */

void R_InitTextures(void)
{
    R_GLES2_Sync();
    GL_InitTextures();
}

void R_UploadTexture(int tpage)
{
    R_GLES2_Sync();
    GL_UploadTpage(tpage);
}

void R_MarkTextureDirty(int tpage)
{
    R_GLES2_Sync();
    /* The upload this schedules will hard-set GL_NEAREST on the texture
     * object (render_gl.c:139, :247), so forget the resident filter and let
     * the next bind re-apply it. */
    if (tpage >= 0 && tpage < 52) {
        s_glTextureFilter[tpage] = 0;
    }
    GL_MarkTpageDirty(tpage);
}

void R_ClearTextureDirty(int tpage)
{
    R_GLES2_Sync();
    GL_ClearTpageDirty(tpage);
}

void R_FreezeTexture(int tpage)
{
    R_GLES2_Sync();
    GL_FreezeTpage(tpage);
}

void R_ThawTexture(int tpage)
{
    R_GLES2_Sync();
    (void)tpage; /* GL has no persistent freeze flag */
}

void R_SetNoColorKey(int tpage)
{
    R_GLES2_Sync();
    GL_SetNoColorKey(tpage);
}

void R_ClearNoColorKey(int tpage)
{
    R_GLES2_Sync();
    GL_ClearNoColorKey(tpage);
}

void R_SetTpageGreen6(int tpage, int on)
{
    R_GLES2_Sync();
    GL_SetTpageGreen6(tpage, on);
}

void R_SetTpageRGBA8(int tpage, unsigned char *rgba) {
    R_GLES2_Sync();
    GL_SetTpageRGBA8(tpage, rgba);
}

void R_UploadTextureRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    R_GLES2_Sync();
    GL_UploadTpageRGBA(tpage, rgba, w, h);
}

void R_UploadTextureSubRect(int tpage, int x, int y, int w, int h)
{
    R_GLES2_Sync();
    GL_UploadTpageSubRect(tpage, x, y, w, h);
}

void R_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    R_GLES2_Sync();
    GL_SetPendingRGBA(tpage, rgba, w, h);
}

void R_EndFrame(void)
{
    EndFrame();
    /* EndFrame is just glFlush — no GL state changes, tracker stays valid. */
}

void R_Flip(void)
{
    FlipD3D();
}

void R_ClearAndReset(void)
{
    R_GLES2_Sync();
    ProcessTpageStates();
}

void R_ClearDepth(void)
{
    R_FlushState();
    glClear(GL_DEPTH_BUFFER_BIT);
}

#endif /* !SONICR_SOFT_RENDER */
