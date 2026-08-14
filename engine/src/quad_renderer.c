#include "microfx/quad_renderer.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    float position[2];
    float color[4];
} QuadVertex;

#define CIRCLE_SEGMENTS 32
#define MAX_VERTICES_PER_ELEMENT (CIRCLE_SEGMENTS*3)

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "MICROFX_QUAD shader failure: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool MicroFxQuadRendererInit(MicroFxQuadRenderer *renderer)
{
    *renderer = (MicroFxQuadRenderer){ 0 };
    static const char *vs =
        "#version 100\nprecision highp float;attribute vec2 aPosition;"
        "attribute vec4 aColor;uniform vec2 uViewport;varying lowp vec4 vColor;"
        "void main(){vec2 p=vec2(aPosition.x/uViewport.x*2.0-1.0,"
        "1.0-aPosition.y/uViewport.y*2.0);gl_Position=vec4(p,0.0,1.0);"
        "vColor=aColor;}\n";
    static const char *fs =
        "#version 100\nprecision lowp float;varying lowp vec4 vColor;"
        "void main(){gl_FragColor=vColor;}\n";
    GLuint vertex = Compile(GL_VERTEX_SHADER, vs);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) return false;
    renderer->program = glCreateProgram();
    glAttachShader(renderer->program, vertex);
    glAttachShader(renderer->program, fragment);
    glBindAttribLocation(renderer->program, 0, "aPosition");
    glBindAttribLocation(renderer->program, 1, "aColor");
    glLinkProgram(renderer->program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(renderer->program, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    renderer->viewportLocation = glGetUniformLocation(renderer->program, "uViewport");
    glGenBuffers(3, renderer->vertexBuffers);
    return renderer->vertexBuffers[0] != 0 && renderer->vertexBuffers[1] != 0 &&
           renderer->vertexBuffers[2] != 0;
}

static void Color(float out[4], uint32_t color)
{
    out[0] = ((color >> 24) & 255)/255.0f;
    out[1] = ((color >> 16) & 255)/255.0f;
    out[2] = ((color >> 8) & 255)/255.0f;
    out[3] = (color & 255)/255.0f;
}

static void SetVertex(QuadVertex *vertex, const MicroFxQuadElement *element,
                      float localX, float localY, uint32_t color)
{
    float c = cosf(element->rotation), s = sinf(element->rotation);
    vertex->position[0] = element->x + c*localX - s*localY;
    vertex->position[1] = element->y + s*localX + c*localY;
    Color(vertex->color, color);
    vertex->color[3] *= element->opacity;
}

static void Rebuild(MicroFxQuadRenderer *renderer, MicroFxScene *scene)
{
    QuadVertex vertices[MICROFX_MAX_QUAD_ELEMENTS*MAX_VERTICES_PER_ELEMENT];
    int cursor = 0;
    renderer->backgroundOpaque = true;
    for (int pass=0;pass<2;pass++) {
      for (int i = 0; i < scene->quadCount; i++) {
        const MicroFxQuadElement *e = &scene->quad[i];
        if(e->background!=(pass==0))continue;
        if (!e->visible) continue;
        if (pass == 0 &&
            (((e->topColor & 255u) != 255u) ||
             ((e->bottomColor & 255u) != 255u) || e->opacity < 0.999f)) {
            renderer->backgroundOpaque = false;
        }
        if (e->kind == MICROFX_QUAD_CIRCLE) {
            const float tau = 6.28318530717958647692f;
            float radius = e->width*0.5f;
            for (int segment = 0; segment < CIRCLE_SEGMENTS; segment++) {
                float a0 = tau*(float)segment/CIRCLE_SEGMENTS;
                float a1 = tau*(float)(segment + 1)/CIRCLE_SEGMENTS;
                SetVertex(&vertices[cursor++], e, 0.0f, 0.0f, e->topColor);
                SetVertex(&vertices[cursor++], e, cosf(a0)*radius,
                          sinf(a0)*radius, e->topColor);
                SetVertex(&vertices[cursor++], e, cosf(a1)*radius,
                          sinf(a1)*radius, e->topColor);
            }
            continue;
        }
        float left = -e->width*0.5f, right = e->width*0.5f;
        float top = -e->height*0.5f, bottom = e->height*0.5f;
        SetVertex(&vertices[cursor++], e, left, top, e->topColor);
        SetVertex(&vertices[cursor++], e, right, top, e->topColor);
        SetVertex(&vertices[cursor++], e, right, bottom, e->bottomColor);
        SetVertex(&vertices[cursor++], e, left, top, e->topColor);
        SetVertex(&vertices[cursor++], e, right, bottom, e->bottomColor);
        SetVertex(&vertices[cursor++], e, left, bottom, e->bottomColor);
      }
        if(pass==0)renderer->backgroundVertexCount=cursor;
    }
    renderer->activeVertexBuffer = (renderer->activeVertexBuffer + 1u)%3u;
    glBindBuffer(GL_ARRAY_BUFFER,
                 renderer->vertexBuffers[renderer->activeVertexBuffer]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(cursor*sizeof(*vertices)),
                 vertices, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    renderer->vertexCount = cursor;
    scene->quadDirty = false;
}

void MicroFxQuadRendererDraw(MicroFxQuadRenderer *renderer, MicroFxScene *scene,
                            int width, int height, bool background)
{
    if (!renderer->program || scene->quadCount == 0) return;
    if (scene->quadDirty) Rebuild(renderer, scene);
    glUseProgram(renderer->program);
    glUniform2f(renderer->viewportLocation, (float)width, (float)height);
    glBindBuffer(GL_ARRAY_BUFFER,
                 renderer->vertexBuffers[renderer->activeVertexBuffer]);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          (void *)offsetof(QuadVertex, position));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          (void *)offsetof(QuadVertex, color));
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (background && renderer->backgroundOpaque) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    int first=background?0:renderer->backgroundVertexCount;
    int count=background?renderer->backgroundVertexCount:
                         renderer->vertexCount-renderer->backgroundVertexCount;
    if(count>0)glDrawArrays(GL_TRIANGLES,first,count);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void MicroFxQuadRendererDestroy(MicroFxQuadRenderer *renderer)
{
    glDeleteBuffers(3, renderer->vertexBuffers);
    if (renderer->program) glDeleteProgram(renderer->program);
    *renderer = (MicroFxQuadRenderer){ 0 };
}
