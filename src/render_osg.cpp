#include "s376/airport.hpp"
#include "s376/config.hpp"
#include "s376/control.hpp"
#include "s376/display.hpp"
#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/lights.hpp"
#include "s376/landmarks.hpp"
#include "s376/missiles.hpp"
#include "s376/traffic.hpp"
#include "s376/image.hpp"
#include "s376/log.hpp"
#include "s376/render.hpp"
#include "s376/terrain.hpp"
#include "s376/version.hpp"

#ifdef S376_HAS_OSG

#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/CullSettings>
#include <osg/CullFace>
#include <osg/Depth>
#include <osg/MatrixTransform>
#include <osg/DisplaySettings>
#include <osg/GL>
#include <osg/GLExtensions>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GraphicsContext>
#include <osg/Notify>
#include <osg/Point>
#include <osg/PolygonOffset>
#include <osg/Program>
#include <osg/Shader>
#include <osg/State>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/Viewport>
#include <osgDB/DatabasePager>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgGA/GUIEventAdapter>
#include <osgGA/GUIEventHandler>
#include <osgText/Text>
#include <osgUtil/CullVisitor>
#include <osgUtil/SceneView>
#include <osgViewer/CompositeViewer>
#include <osgViewer/Renderer>
#include <osgViewer/Viewer>

#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_VERTEX_PROGRAM_POINT_SIZE
#define GL_VERTEX_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM_ARB
#define GL_COMPRESSED_RGBA_BPTC_UNORM_ARB 0x8E8C
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

#ifdef S376_HAS_OSGEARTH
#include <osgEarth/MapNode>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#ifdef __linux__
#include <unistd.h>
#endif
#include <string>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace s376 {
namespace {

osg::Vec3d ecef_to_osg(const geo::Ecef& p) {
    return osg::Vec3d(p.x, p.y, p.z);
}

// GLSL 120: legal on a Linux legacy/compat context and on 3.3 +
// COMPATIBILITY_PROFILE. Lighting, fog, and point size live here so we
// never emit Material / LightModel / LIGHTING / FOG / POINT_SMOOTH /
// PointSprite / TEXTURE_2D modes (illegal on a CORE profile).
const char* kLitTexVS = R"(
#version 120
varying vec3 vN;
varying vec2 vUV;
varying vec2 vWorld;
varying float vEyeDist;
varying float vWaterMask;
varying vec3 vView;
varying vec3 vPos;
void main() {
    vN = gl_Normal;
    vUV = gl_MultiTexCoord0.xy;
    vWorld = gl_MultiTexCoord1.xy;
    vWaterMask = gl_MultiTexCoord1.z;
    vPos = gl_Vertex.xyz;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    vView = -eye.xyz;
    gl_Position = ftransform();
}
)";

const char* kLitTexFS = R"(
#version 120
uniform sampler2D sTex;
uniform sampler2D sDetail;
uniform sampler2D sShadow;
uniform sampler2D sShadowFar;
uniform vec3 uSunDir;
uniform vec3 uAmbient;
uniform vec3 uDiffuse;
uniform vec3 uFogColor;
uniform vec3 uUp;
uniform float uFogDensity;
uniform float uHaze;
uniform float uExposure;
uniform float uDetail;
uniform float uWater;
uniform float uNight;
uniform float uTime;
uniform float uBloom;
uniform float uFilmic;
uniform float uAtmosphere;
uniform float uShadows;
uniform mat4 uShadowMatrix;
uniform mat4 uShadowMatrixFar;
uniform int uLightCount;
uniform vec4 uLightPosR[32];
uniform vec3 uLightCol[32];
uniform float uClustered;
uniform float uLodFade;
varying vec3 vN;
varying vec2 vUV;
varying vec2 vWorld;
varying float vEyeDist;
varying float vWaterMask;
varying vec3 vView;
varying vec3 vPos;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 grade(vec3 x, float filmicOn) {
    vec3 t = aces(x);
    vec3 f = pow(max(t, vec3(0.0)), vec3(0.96)) * 1.03 + vec3(0.008);
    return mix(t, clamp(f, 0.0, 1.0), filmicOn);
}
float aerial(float dist, vec3 n) {
    float g = 1.0 - clamp(abs(dot(normalize(n), normalize(uUp))), 0.0, 1.0);
    // Beer plus a 40 nm far fade so the land/sky join is haze, not a knife.
    float beer = exp(-uFogDensity * dist * 0.38);
    float hz = exp(-uHaze * dist * (2.2 + 7.0 * pow(g, 1.15)));
    float km = dist * 0.001;
    float far = exp(-km * km * 0.00028);
    return clamp(beer * hz * far, 0.0, 1.0);
}
float shadow_sample(sampler2D smp, mat4 M, vec3 world) {
    vec4 sp = M * vec4(world, 1.0);
    if (sp.w <= 0.0)
        return 1.0;
    vec3 sc = sp.xyz / sp.w;
    sc = sc * 0.5 + 0.5;
    if (sc.x <= 0.001 || sc.x >= 0.999 || sc.y <= 0.001 || sc.y >= 0.999 || sc.z >= 0.999)
        return -1.0;
    float d = texture2D(smp, sc.xy).r;
    return sc.z > d + 0.004 ? 0.38 : 1.0;
}
float shadow_term(vec3 world) {
    if (uShadows < 0.5)
        return 1.0;
    float n = shadow_sample(sShadow, uShadowMatrix, world);
    if (n >= 0.0)
        return n;
    float f = shadow_sample(sShadowFar, uShadowMatrixFar, world);
    return f < 0.0 ? 1.0 : f;
}
vec3 cluster_light(vec3 world, vec3 n) {
    if (uClustered < 0.5)
        return vec3(0.0);
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 32; ++i) {
        float on = step(float(i) + 0.5, float(uLightCount));
        vec3 L = uLightPosR[i].xyz - world;
        float r = max(uLightPosR[i].w, 1.0);
        float d2 = dot(L, L);
        float att = clamp(1.0 - d2 / (r * r), 0.0, 1.0);
        att *= att;
        acc += uLightCol[i] * att * max(dot(n, normalize(L + vec3(1e-5))), 0.0) * on;
    }
    return acc * uClustered;
}
void main() {
    vec3 n = normalize(vN);
    vec3 albedo = texture2D(sTex, vUV).rgb;
    float wet = 0.0;
    if (uWater > 0.5)
        wet = smoothstep(0.03, 0.16, albedo.b - max(albedo.r, albedo.g) * 0.90);
    float ndl = max(dot(n, normalize(uSunDir)), 0.0);
    ndl *= shadow_term(vPos);
    float luma = dot(albedo, vec3(0.30, 0.50, 0.20));
    // Imagery chroma only. vWaterMask is a GSHHS land_coverage knife; Jamaica
    // Bay marsh and the Rockaway inlet must read from the ortho, not a navy
    // flood fill. The mask is a soft hint and only where the texel is already
    // bluish/dark.
    float chroma = albedo.b - max(albedo.r, albedo.g) * 0.85;
    float darkWater = (1.0 - smoothstep(0.04, 0.12, luma)) * smoothstep(0.04, 0.14, chroma);
    if (uWater > 0.5) {
        // Jamaica Bay ortho/synth is olive (g>b); do not require blue chroma.
        float maskHint = clamp(vWaterMask, 0.0, 1.0);
        wet = max(wet, max(darkWater, maskHint * 0.82));
    }
    vec2 wu = (dot(vWorld, vWorld) > 1.0) ? vWorld : vUV;
    float gness = clamp((albedo.g - albedo.r) * 4.0, 0.0, 1.0);
    if (wet < 0.55) {
        float macro = 0.82 + 0.18 * noise(wu * 80.0);
        float micro = 0.90 + 0.10 * noise(wu * 600.0);
        vec3 land = albedo * mix(1.0, macro * micro, uDetail);
        gness = clamp((land.g - land.r) * 4.0, 0.0, 1.0);
        float paved = clamp((luma - 0.22) * 3.5 * (1.0 - gness), 0.0, 1.0);
        float close = clamp(1.0 - vEyeDist / 2200.0, 0.0, 1.0);
        float mid = clamp(1.0 - vEyeDist / 8000.0, 0.12, 1.0);
        vec3 grass = vec3(0.20, 0.30, 0.13) * (0.82 + 0.18 * noise(wu * 900.0));
        vec3 dirt = vec3(0.30, 0.23, 0.14) * (0.88 + 0.12 * noise(wu * 300.0));
        vec2 duv = wu * 480.0;
        float qx = paved > 0.45 ? 0.5 : 0.0;
        float qy = gness > 0.35 ? 0.0 : 0.5;
        vec3 atlas = texture2D(sDetail, vec2(fract(duv.x) * 0.5 + qx, fract(duv.y) * 0.5 + qy)).rgb;
        land = mix(mix(land, dirt, 0.08 * (1.0 - paved)), grass, gness * 0.22);
        land = mix(land, land * (0.50 + 0.95 * atlas), close);
        albedo = mix(albedo, land, uDetail * mid * (1.0 - wet));
        // Distant land desaturates into haze so the horizon is not cartoon green.
        float farland = clamp((vEyeDist - 6000.0) / 38000.0, 0.0, 1.0);
        albedo = mix(albedo, mix(uFogColor, albedo, 0.42), farland);
    }
    vec3 col = albedo * (uAmbient + uDiffuse * ndl);
    col += albedo * cluster_light(vPos, n);
    if (wet > 0.01 && uWater > 0.5) {
        vec3 view = normalize(vView);
        vec3 sun = normalize(uSunDir);
        float n1 = noise(wu * 520.0 + vec2(uTime * 0.07, uTime * 0.03));
        float n2 = noise(wu * 1280.0 + vec2(-uTime * 0.05, uTime * 0.09) + 11.0);
        float wave = mix(0.06, 0.22, smoothstep(500.0, 4500.0, vEyeDist));
        vec3 wn = normalize(n + vec3((n1 - 0.5) * wave, (n2 - 0.5) * wave, 0.0));
        float fres = pow(1.0 - clamp(dot(wn, view), 0.0, 1.0), 4.0);
        // Cap grazing Fresnel — Jamaica Bay at 3° is water, not a white slab.
        fres *= mix(1.0, 0.38, smoothstep(0.55, 0.94, fres));
        float latw = vWorld.y;
        float lonw = vWorld.x;
        float bay = step(40.54, latw) * step(latw, 40.68) * step(-74.02, lonw) * step(lonw, -73.72);
        vec3 deep = mix(vec3(0.02, 0.07, 0.14), vec3(0.10, 0.20, 0.16), bay);
        vec3 shallow = mix(vec3(0.04, 0.13, 0.20), vec3(0.16, 0.26, 0.18), bay);
        vec3 wcol = mix(deep, shallow, n1 * 0.40);
        vec3 skyref = mix(uFogColor * 0.52, vec3(0.14, 0.26, 0.40), 0.40);
        wcol = mix(wcol, skyref, 0.10 + 0.34 * fres);
        float glint = pow(max(dot(reflect(-sun, wn), view), 0.0), 280.0);
        float close = clamp(1.0 - vEyeDist / 1800.0, 0.0, 1.0);
        wcol += vec3(1.0, 0.95, 0.82) * glint * mix(0.06, 0.015, max(close, bay * 0.65)) *
                (1.0 - uNight * 0.90);
        wcol *= (uAmbient + uDiffuse * (0.32 + 0.50 * max(dot(wn, sun), 0.0)));
        float foam = smoothstep(0.42, 0.68, wet) * (1.0 - smoothstep(0.78, 0.98, wet));
        foam *= 0.40 + 0.35 * noise(wu * 900.0 + vec2(uTime * 0.15, 0.0));
        wcol = mix(wcol, vec3(0.70, 0.76, 0.80), foam * 0.35);
        // Keep most of the ortho so marsh / shallow bay stay photographic.
        col = mix(col, wcol, wet * mix(0.34, 0.48, bay));
    }
    if (uBloom > 0.5 && uNight > 0.25 && wet < 0.40) {
        float urban = smoothstep(0.14, 0.36, luma) * (1.0 - gness);
        col += vec3(1.0, 0.70, 0.32) * urban * uNight * uBloom * 0.14;
    }
    float f = aerial(vEyeDist, n);
    col = mix(uFogColor, col, clamp(f, 0.0, 1.0));
    col = mix(uFogColor, col, clamp(uLodFade, 0.0, 1.0));
    gl_FragColor = vec4(grade(col * uExposure, uFilmic), 1.0);
}
)";

const char* kLitColorVS = R"(
#version 120
varying vec3 vN;
varying vec4 vCol;
varying float vEyeDist;
varying vec3 vPos;
varying vec3 vView;
void main() {
    vN = gl_Normal;
    vCol = gl_Color;
    vPos = gl_Vertex.xyz;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    vView = -eye.xyz;
    gl_Position = ftransform();
}
)";

const char* kLitColorFS = R"(
#version 120
uniform vec3 uSunDir;
uniform vec3 uAmbient;
uniform vec3 uDiffuse;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uHaze;
uniform vec3 uUp;
uniform vec3 uOrigin;
uniform float uExposure;
uniform float uDetail;
uniform float uSsao;
uniform float uNight;
uniform float uWetness;
uniform float uFilmic;
uniform float uShadows;
uniform sampler2D sShadow;
uniform sampler2D sShadowFar;
uniform mat4 uShadowMatrix;
uniform mat4 uShadowMatrixFar;
uniform int uLightCount;
uniform vec4 uLightPosR[32];
uniform vec3 uLightCol[32];
uniform float uClustered;
uniform float uLodFade;
varying vec3 vN;
varying vec4 vCol;
varying float vEyeDist;
varying vec3 vPos;
varying vec3 vView;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 grade(vec3 x, float filmicOn) {
    vec3 t = aces(x);
    vec3 f = pow(max(t, vec3(0.0)), vec3(0.96)) * 1.03 + vec3(0.008);
    return mix(t, clamp(f, 0.0, 1.0), filmicOn);
}
float shadow_sample(sampler2D smp, mat4 M, vec3 world) {
    vec4 sp = M * vec4(world, 1.0);
    if (sp.w <= 0.0)
        return 1.0;
    vec3 sc = sp.xyz / sp.w;
    sc = sc * 0.5 + 0.5;
    if (sc.x <= 0.001 || sc.x >= 0.999 || sc.y <= 0.001 || sc.y >= 0.999 || sc.z >= 0.999)
        return -1.0;
    float d = texture2D(smp, sc.xy).r;
    return sc.z > d + 0.004 ? 0.38 : 1.0;
}
float shadow_term(vec3 world) {
    if (uShadows < 0.5)
        return 1.0;
    float n = shadow_sample(sShadow, uShadowMatrix, world);
    if (n >= 0.0)
        return n;
    float f = shadow_sample(sShadowFar, uShadowMatrixFar, world);
    return f < 0.0 ? 1.0 : f;
}
vec3 cluster_light(vec3 world, vec3 n) {
    if (uClustered < 0.5)
        return vec3(0.0);
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 32; ++i) {
        float on = step(float(i) + 0.5, float(uLightCount));
        vec3 L = uLightPosR[i].xyz - world;
        float r = max(uLightPosR[i].w, 1.0);
        float d2 = dot(L, L);
        float att = clamp(1.0 - d2 / (r * r), 0.0, 1.0);
        att *= att;
        acc += uLightCol[i] * att * max(dot(n, normalize(L + vec3(1e-5))), 0.0) * on;
    }
    return acc * uClustered;
}
void main() {
    vec3 n = normalize(vN);
    vec3 sun = normalize(uSunDir);
    float ndl = max(dot(n, sun), 0.0) * shadow_term(vPos);
    float mat = vCol.a;
    vec3 albedo = vCol.rgb;
    if (mat < 0.5) {
        vec2 uv = vPos.xy * 0.08;
        float n1 = noise(uv);
        float n2 = noise(uv * 4.3 + 3.1);
        vec3 nmap = normalize(n + vec3((n1 - 0.5) * 0.28, (n2 - 0.5) * 0.28, 0.0));
        float rub = 0.88 + 0.12 * noise(vPos.xy * 0.08);
        albedo *= mix(1.0, (0.78 + 0.22 * noise(vPos.xy * 0.35)) * rub, uDetail);
        float rough = mix(0.74, 0.18, clamp(uWetness, 0.0, 1.0));
        vec3 view = normalize(vView);
        float fres = pow(1.0 - clamp(dot(nmap, view), 0.0, 1.0), 5.0);
        float glint = pow(max(dot(reflect(-sun, nmap), view), 0.0), mix(40.0, 180.0, 1.0 - rough));
        albedo = mix(albedo, mix(albedo * 0.55, uFogColor, 0.30 + 0.50 * fres), uWetness * 0.85);
        albedo += vec3(0.85, 0.90, 1.0) * glint * uWetness * 0.55;
        n = nmap;
        ndl = max(dot(n, sun), 0.0) * shadow_term(vPos);
    } else if (mat > 1.5 && mat < 2.5) {
        float n1 = noise(vPos.xy * 0.22);
        albedo *= mix(1.0, 0.88 + 0.12 * n1, uDetail);
        float rough = mix(0.62, 0.20, clamp(uWetness, 0.0, 1.0));
        vec3 view = normalize(vView);
        vec3 nmap = normalize(n + vec3((n1 - 0.5) * 0.16, 0.0, 0.0));
        float fres = pow(1.0 - clamp(dot(nmap, view), 0.0, 1.0), 5.0);
        float glint = pow(max(dot(reflect(-sun, nmap), view), 0.0), mix(32.0, 140.0, 1.0 - rough));
        albedo = mix(albedo, mix(albedo * 0.70, uFogColor, 0.22 + 0.40 * fres), uWetness * 0.55);
        albedo += vec3(0.80, 0.86, 0.95) * glint * uWetness * 0.30;
    } else if (mat > 2.5 && mat < 3.5) {
        vec3 up = normalize(uUp);
        float hgt = abs(dot(vPos - uOrigin, up));
        vec3 tng = normalize(cross(n, up));
        if (dot(tng, tng) < 1e-6)
            tng = vec3(1.0, 0.0, 0.0);
        vec2 fu = vec2(dot(vPos, tng) * 0.35, hgt * 0.55);
        float win = step(0.22, fract(fu.x)) * step(0.28, fract(fu.y));
        float mortar = smoothstep(0.08, 0.18, abs(fract(fu.x * 2.0) - 0.5));
        albedo *= mix(0.82, 1.0, mortar);
        albedo = mix(albedo, albedo * vec3(0.22, 0.28, 0.36), win * 0.70);
        float glow = win * uNight * (0.40 + 0.60 * hash(floor(fu)));
        albedo += vec3(1.0, 0.82, 0.48) * glow * 3.10;
        albedo *= 0.75 + 0.25 * ndl;
        albedo *= mix(1.0, 0.85 + 0.15 * uSsao, 1.0);
    } else if (mat > 3.5 && mat < 4.5) {
        float tile = 0.84 + 0.16 * noise(vPos.xy * 0.55);
        albedo *= tile;
    }
    if (mat > 0.5 && mat < 1.5) {
        float fade = clamp(1.0 - vEyeDist / 12000.0, 0.35, 1.0);
        albedo = mix(vec3(0.34, 0.34, 0.35), albedo, fade);
    }
    float lit = (mat < 0.5 || (mat > 1.5 && mat < 2.5)) ? mix(0.72, ndl, 0.28) : ndl;
    vec3 col = albedo * (uAmbient + uDiffuse * lit);
    col += albedo * cluster_light(vPos, n);
    if (mat < 0.5 && uNight > 0.05) {
        // Faint HIRL/CL wash so CAT II/III asphalt reads as lit, not a black wedge.
        float sheen = (0.07 + 0.09 * noise(vPos.xy * 0.18)) * uNight;
        float fres = pow(1.0 - clamp(dot(n, normalize(vView)), 0.0, 1.0), 2.8);
        col += vec3(0.58, 0.60, 0.68) * sheen * (0.65 + 0.55 * fres);
    }
    float g = 1.0 - clamp(abs(dot(n, normalize(uUp))), 0.0, 1.0);
    float beer = exp(-uFogDensity * vEyeDist * 0.38);
    float hz = exp(-uHaze * vEyeDist * (2.2 + 7.0 * pow(g, 1.15)));
    float km = vEyeDist * 0.001;
    float f = clamp(beer * hz * exp(-km * km * 0.00028), 0.0, 1.0);
    col = mix(uFogColor, col, f);
    // Distant hangar/jetbridge walls become hairline cards. Lift them
    // into aerial haze so they are not floating black slivers.
    if (mat > 2.5 && mat < 4.5 && vEyeDist > 8000.0) {
        float k = clamp((vEyeDist - 8000.0) / 14000.0, 0.0, 0.28);
        col = mix(col, uFogColor, k);
        col = max(col, uFogColor * 0.28);
    }
    col = mix(uFogColor, col, clamp(uLodFade, 0.0, 1.0));
    gl_FragColor = vec4(grade(col * uExposure, uFilmic), 1.0);
}
)";

// Stylized classic (pre-2001) Lower Manhattan — art-directed, not surveyed.
const char* kLandmarkVS = R"(
#version 120
varying vec3 vN;
varying vec3 vView;
varying vec2 vUV;
varying float vEyeDist;
varying float vMat;
void main() {
    vN = gl_Normal;
    vUV = gl_MultiTexCoord0.xy;
    vMat = gl_Color.a;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    vView = -eye.xyz;
    gl_Position = ftransform();
}
)";

const char* kLandmarkFS = R"(
#version 120
uniform vec3 uSunDir;
uniform vec3 uAmbient;
uniform vec3 uDiffuse;
uniform vec3 uFogColor;
uniform vec3 uUp;
uniform float uFogDensity;
uniform float uHaze;
uniform float uExposure;
uniform float uSsao;
uniform float uNight;
uniform float uFilmic;
varying vec3 vN;
varying vec3 vView;
varying vec2 vUV;
varying float vEyeDist;
varying float vMat;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 grade(vec3 x, float filmicOn) {
    vec3 t = aces(x);
    vec3 f = pow(max(t, vec3(0.0)), vec3(0.96)) * 1.03 + vec3(0.008);
    return mix(t, clamp(f, 0.0, 1.0), filmicOn);
}
void main() {
    vec3 n = normalize(vN);
    vec3 view = normalize(vView);
    vec3 sun = normalize(uSunDir);
    float ndl = max(dot(n, sun), 0.0);
    float fres = pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 4.0);
    float fin = smoothstep(0.12, 0.22, abs(fract(vUV.x * 48.0) - 0.5));
    float win = step(0.18, fract(vUV.x * 22.0)) * step(0.22, fract(vUV.y * 70.0));
    vec3 alum = vec3(0.72, 0.74, 0.78);
    vec3 glass = vec3(0.18, 0.28, 0.38);
    vec3 albedo = mix(alum, glass, win * 0.85);
    albedo = mix(albedo * 0.55, albedo, fin);
    if (vMat > 4.5)
        albedo = vec3(0.55, 0.56, 0.58);
    if (vMat > 3.5 && vMat < 4.5)
        albedo = vec3(0.16, 0.17, 0.18);
    vec3 spec = vec3(0.85, 0.90, 1.0) * pow(max(dot(reflect(-sun, n), view), 0.0), 48.0);
    vec3 skyref = mix(uFogColor, vec3(0.45, 0.65, 0.95), 0.45) * fres;
    float glow = win * uNight * (0.35 + 0.65 * hash(floor(vUV * vec2(22.0, 70.0))));
    vec3 nightwin = vec3(1.0, 0.82, 0.45) * glow * 1.8;
    float ao = mix(1.0, 0.72 + 0.28 * ndl, uSsao);
    vec3 col = albedo * (uAmbient * 1.15 + uDiffuse * (0.35 + 0.85 * ndl)) * ao;
    col += spec * 0.55 + skyref * 0.22 + nightwin;
    float rim = pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 2.5) * (0.15 + 0.25 * uNight);
    col += vec3(1.0, 0.92, 0.75) * rim;
    float g = 1.0 - clamp(abs(dot(n, normalize(uUp))), 0.0, 1.0);
    float h = uFogDensity + uHaze * (0.10 + 0.40 * g);
    float f = exp(-h * h * vEyeDist * vEyeDist);
    col = mix(uFogColor, col, clamp(f, 0.0, 1.0));
    gl_FragColor = vec4(grade(col * uExposure * 1.08, uFilmic), 1.0);
}
)";

const char* kUnlitColorVS = R"(
#version 120
varying vec4 vCol;
varying float vEyeDist;
void main() {
    vCol = gl_Color;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    gl_Position = ftransform();
}
)";

const char* kUnlitColorFS = R"(
#version 120
uniform vec3 uFogColor;
uniform vec3 uSunDir;
uniform vec3 uDiffuse;
uniform float uFogDensity;
uniform float uHaze;
uniform float uExposure;
uniform float uFilmic;
varying vec4 vCol;
varying float vEyeDist;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 grade(vec3 x, float filmicOn) {
    vec3 t = aces(x);
    vec3 f = pow(max(t, vec3(0.0)), vec3(0.96)) * 1.03 + vec3(0.008);
    return mix(t, clamp(f, 0.0, 1.0), filmicOn);
}
void main() {
    float edge = 0.55 + 0.45 * hash(gl_FragCoord.xy * 0.07 + vEyeDist * 0.001);
    float wrap = 0.65 + 0.35 * max(dot(normalize(uSunDir), vec3(0.0, 0.0, 1.0)), 0.0);
    vec3 col = vCol.rgb * wrap * (0.75 + 0.25 * uDiffuse);
    float h = uFogDensity + uHaze;
    float f = exp(-h * h * vEyeDist * vEyeDist);
    col = mix(uFogColor, col, clamp(f, 0.0, 1.0));
    gl_FragColor = vec4(grade(col * uExposure, uFilmic), vCol.a * edge);
}
)";

const char* kPointVS = R"(
#version 120
varying vec4 vCol;
varying float vEyeDist;
uniform float uPointSize;
uniform float uPointSizeFar;
uniform float uSizeNearM;
uniform float uNight;
void main() {
    vCol = gl_Color;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    float t = clamp(vEyeDist / max(uSizeNearM, 1.0), 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);
    float sz = gl_MultiTexCoord1.x;
    if (sz < 0.5)
        sz = mix(uPointSize, uPointSizeFar, t);
    // Night: grow the sprite so the Raster halo has room around the core.
    sz *= 1.0 + uNight * 0.45;
    gl_PointSize = sz;
    gl_Position = ftransform();
}
)";

const char* kPointFS = R"(
#version 120
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uExposure;
uniform float uBloom;
uniform float uNight;
uniform float uFilmic;
varying vec4 vCol;
varying float vEyeDist;
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 grade(vec3 x, float filmicOn) {
    vec3 t = aces(x);
    vec3 f = pow(max(t, vec3(0.0)), vec3(0.96)) * 1.03 + vec3(0.008);
    return mix(t, clamp(f, 0.0, 1.0), filmicOn);
}
void main() {
    vec2 pc = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(pc, pc);
    if (r2 > 1.0)
        discard;
    float core = exp(-r2 * mix(9.0, 3.2, uNight));
    float halo = exp(-r2 * 0.40) * mix(0.0, 0.85, uNight) * uBloom;
    float fogH = exp(-uFogDensity * vEyeDist * 0.12);
    float f = exp(-uFogDensity * uFogDensity * vEyeDist * vEyeDist);
    float a = vCol.a * (core + halo * fogH) * clamp(f, 0.0, 1.0);
    vec3 rgb = vCol.rgb * uExposure * (1.0 + 1.35 * uNight);
    // Night cores skip ACES crush so ALS / edge / PAPI stay the brightest thing.
    vec3 mapped = mix(grade(rgb, uFilmic), min(rgb, vec3(1.35)), uNight);
    gl_FragColor = vec4(mapped, a);
}
)";

const char* kSkyVS = R"(
#version 120
varying vec2 vNdc;
void main() {
    vNdc = gl_Vertex.xy;
    gl_Position = vec4(gl_Vertex.xy, 0.999, 1.0); // clip-space; verts oversized vs NDC
}
)";

const char* kSkyFS = R"(
#version 120
uniform vec3 uSunDir;
uniform vec3 uUp;
uniform vec3 uFwd;
uniform vec3 uRight;
uniform vec3 uCamUp;
uniform vec3 uFogColor;
uniform float uExposure;
uniform float uTanHalfH;
uniform float uTanHalfV;
uniform float uFilmic;
uniform float uAtmosphere;
uniform vec2 uViewport;
uniform vec3 uCityDir;
varying vec2 vNdc;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 grade(vec3 x, float filmicOn) {
    vec3 t = aces(x);
    vec3 f = pow(max(t, vec3(0.0)), vec3(0.96)) * 1.03 + vec3(0.008);
    return mix(t, clamp(f, 0.0, 1.0), filmicOn);
}
void main() {
    vec3 up = normalize(uUp);
    vec3 fwd = normalize(uFwd);
    vec3 right = normalize(uRight);
    vec3 camUp = normalize(uCamUp);
    if (dot(camUp, camUp) < 1e-8)
        camUp = normalize(cross(right, fwd));
    vec2 ndc = vNdc;
    if (uViewport.x > 8.0 && uViewport.y > 8.0)
        ndc = (gl_FragCoord.xy / uViewport) * 2.0 - 1.0;
    vec3 d = normalize(fwd + right * (ndc.x * uTanHalfH) + camUp * (ndc.y * uTanHalfV));
    float h = clamp(dot(d, up), -1.0, 1.0);
    vec3 sun = normalize(uSunDir);
    float m = max(dot(d, sun), 0.0);
    float sunH = dot(sun, up);
    vec3 zenith = vec3(0.10, 0.38, 0.92);
    vec3 horiz = mix(uFogColor, vec3(0.52, 0.68, 0.88), 0.55);
    vec3 ground = vec3(0.28, 0.36, 0.48);
    float elev = smoothstep(0.0, 1.0, pow(clamp(h, 0.0, 1.0), 0.70));
    vec3 simple = mix(horiz, zenith, elev);
    if (h < 0.0)
        simple = mix(horiz, ground, smoothstep(0.0, 1.0, clamp(-h * 2.0, 0.0, 1.0)));
    simple += vec3(1.0, 0.96, 0.82) * smoothstep(0.9994, 0.99985, m) * 0.55;
    simple += vec3(1.0, 0.90, 0.70) * pow(m, 512.0) * 0.18;
    float hz = pow(1.0 - abs(h), 1.05);
    float band = smoothstep(0.12, 0.0, abs(h));
    simple = mix(simple, uFogColor, 0.22 * hz + 0.28 * band);
    // Rayleigh / Mie as a modulation on the proven gradient (GLSL 120).
    // A full optical-depth integral goes brown at noon on Maxwell ALU.
    float cosT = dot(d, sun);
    float rayP = 0.75 * (1.0 + cosT * cosT);
    float g = 0.76;
    float mieP = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosT, 1e-4), 1.5);
    float am = 1.0 / max(h * 0.70 + 0.30, 0.10);
    vec3 rayleigh = vec3(0.10, 0.34, 0.90) * rayP * exp(-0.08 * am);
    vec3 mie = vec3(1.0, 0.88, 0.65) * mieP * 0.045;
    float twilight = pow(1.0 - clamp(sunH, 0.0, 1.0), 2.4) * smoothstep(-0.22, 0.06, sunH);
    vec3 dusk = vec3(1.0, 0.42, 0.12) * mieP * twilight * 0.40 * exp(-abs(h) * 3.2);
    vec3 pbr = (rayleigh + mie) * (0.45 + 0.70 * max(sunH, 0.0)) + dusk;
    pbr = mix(simple, pbr + simple * 0.35, 0.55);
    pbr += vec3(1.0, 0.96, 0.85) * smoothstep(0.9995, 0.99992, m) * clamp(sunH + 0.05, 0.0, 1.0);
    if (h < 0.0)
        pbr = mix(pbr, ground * (0.25 + 0.75 * max(sunH, 0.0)), clamp(-h * 2.2, 0.0, 1.0));
    pbr = mix(pbr, uFogColor, 0.18 * hz + 0.22 * band);
    vec3 sky = mix(simple, pbr, clamp(uAtmosphere, 0.0, 1.0));
    // 0 at +6° sun, 1 by −8° — 3 AM is navy, not a leftover dusk glow.
    float night = smoothstep(0.10, -0.14, sunH);
    vec3 moon = normalize(-sun + 0.15 * up);
    float mm = max(dot(d, moon), 0.0);
    vec3 nightcol = vec3(0.003, 0.006, 0.018);
    nightcol += vec3(0.55, 0.60, 0.72) * pow(mm, 80.0) * 0.55;
    nightcol += vec3(0.28, 0.32, 0.42) * pow(mm, 8.0) * 0.05;
    float star = step(0.996, hash(d.xy * 90.0 + d.z * 17.0));
    nightcol += vec3(star) * step(0.05, h) * 0.85;
    // NYC skyline glow from a real city direction (Manhattan ECEF), not a lattice.
    // 04R front barely clips it; Left wrap sees the warm band.
    float cityH = pow(clamp(1.0 - abs(h), 0.0, 1.0), 5.2);
    float cityDot = 0.0;
    if (dot(uCityDir, uCityDir) > 0.05)
        cityDot = pow(max(dot(d, normalize(uCityDir)), 0.0), 3.2);
    float city = cityH * (0.18 + 0.82 * cityDot);
    nightcol += vec3(0.62, 0.36, 0.12) * city * 0.42;
    sky += vec3(0.40, 0.24, 0.10) * city * 0.10 * (1.0 - night);
    sky = mix(sky, nightcol, night);
    gl_FragColor = vec4(grade(sky * uExposure, uFilmic), 1.0);
}
)";

const char* kFxaaVS = R"(
#version 120
varying vec2 vUV;
void main() {
    vUV = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)";

const char* kFxaaFS = R"(
#version 120
uniform sampler2D sColor;
uniform vec2 uInvRes;
varying vec2 vUV;
void main() {
    vec3 rgb = texture2D(sColor, vUV).rgb;
    vec3 rgbN = texture2D(sColor, vUV + vec2(0.0, uInvRes.y)).rgb;
    vec3 rgbS = texture2D(sColor, vUV - vec2(0.0, uInvRes.y)).rgb;
    vec3 rgbE = texture2D(sColor, vUV + vec2(uInvRes.x, 0.0)).rgb;
    vec3 rgbW = texture2D(sColor, vUV - vec2(uInvRes.x, 0.0)).rgb;
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float l = dot(rgb, luma);
    float lN = dot(rgbN, luma);
    float lS = dot(rgbS, luma);
    float lE = dot(rgbE, luma);
    float lW = dot(rgbW, luma);
    float lMin = min(l, min(min(lN, lS), min(lE, lW)));
    float lMax = max(l, max(max(lN, lS), max(lE, lW)));
    vec2 dir = vec2(-(lN + lS) + (lE + lW), (lN + lW) - (lS + lE));
    float dirR = 1.0 / (min(abs(dir.x), abs(dir.y)) + (lN + lS + lE + lW) * 0.25 + 1e-4);
    dir = clamp(dir * dirR, vec2(-8.0), vec2(8.0)) * uInvRes;
    vec3 a = 0.5 * (texture2D(sColor, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture2D(sColor, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture2D(sColor, vUV + dir * -0.5).rgb +
                               texture2D(sColor, vUV + dir * 0.5).rgb);
    float lb = dot(b, luma);
    gl_FragColor = vec4((lb < lMin || lb > lMax) ? a : b, 1.0);
}
)";

const char* kTaaFS = R"(
#version 120
uniform sampler2D sColor;
uniform sampler2D sHist;
uniform vec2 uInvRes;
uniform float uTaa;
varying vec2 vUV;
void main() {
    vec3 c = texture2D(sColor, vUV).rgb;
    if (uTaa < 0.5) {
        gl_FragColor = vec4(c, 1.0);
        return;
    }
    vec3 n0 = texture2D(sColor, vUV + vec2(-uInvRes.x, -uInvRes.y)).rgb;
    vec3 n1 = texture2D(sColor, vUV + vec2( uInvRes.x, -uInvRes.y)).rgb;
    vec3 n2 = texture2D(sColor, vUV + vec2(-uInvRes.x,  uInvRes.y)).rgb;
    vec3 n3 = texture2D(sColor, vUV + vec2( uInvRes.x,  uInvRes.y)).rgb;
    vec3 nmin = min(c, min(min(n0, n1), min(n2, n3)));
    vec3 nmax = max(c, max(max(n0, n1), max(n2, n3)));
    vec3 h = clamp(texture2D(sHist, vUV).rgb, nmin, nmax);
    gl_FragColor = vec4(mix(c, h, 0.86), 1.0);
}
)";

void strip_ff(osg::StateSet* ss) {
    if (!ss)
        return;
    ss->removeMode(GL_LIGHTING);
    ss->removeMode(GL_LIGHT0);
    ss->removeMode(GL_FOG);
    ss->removeMode(GL_ALPHA_TEST);
    ss->removeMode(GL_POINT_SMOOTH);
    ss->removeMode(GL_POINT_SPRITE);
    ss->removeAttribute(osg::StateAttribute::MATERIAL);
    ss->removeAttribute(osg::StateAttribute::LIGHTMODEL);
    ss->removeAttribute(osg::StateAttribute::LIGHT);
    ss->removeAttribute(osg::StateAttribute::FOG);
    ss->removeAttribute(osg::StateAttribute::POINTSPRITE);
    ss->removeTextureMode(0, GL_TEXTURE_2D);
    ss->removeTextureAttribute(0, osg::StateAttribute::POINTSPRITE);
}

void configure_gl_state(osgViewer::CompositeViewer& viewer, bool check_errors) {
    const auto mode = check_errors ? osg::State::ONCE_PER_ATTRIBUTE
                                   : osg::State::NEVER_CHECK_GL_ERRORS;
    for (unsigned i = 0; i < viewer.getNumViews(); ++i) {
        osgViewer::View* view = viewer.getView(i);
        if (!view)
            continue;
        osg::Camera* cam = view->getCamera();
        if (!cam)
            continue;
        strip_ff(cam->getOrCreateStateSet());
        if (auto* gc = cam->getGraphicsContext()) {
            if (auto* st = gc->getState())
                st->setCheckForGLErrors(mode);
        }
        auto* renderer = dynamic_cast<osgViewer::Renderer*>(cam->getRenderer());
        if (!renderer)
            continue;
        for (int s = 0; s < 2; ++s) {
            if (auto* sv = renderer->getSceneView(s)) {
                strip_ff(sv->getGlobalStateSet());
                strip_ff(sv->getLocalStateSet());
                if (auto* st = sv->getState())
                    st->setCheckForGLErrors(mode);
            }
        }
    }
}

osg::Program* make_program(const char* vs, const char* fs) {
    auto* p = new osg::Program;
    p->addShader(new osg::Shader(osg::Shader::VERTEX, vs));
    p->addShader(new osg::Shader(osg::Shader::FRAGMENT, fs));
    return p;
}

osg::Texture2D* make_detail_atlas() {
    constexpr int N = 128;
    auto* img = new osg::Image;
    img->allocateImage(N, N, 1, GL_RGB, GL_UNSIGNED_BYTE);
    auto* p = img->data();
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const int qx = x / (N / 2);
            const int qy = y / (N / 2);
            const float n =
                0.5f + 0.5f * std::sin(x * 0.37f + y * 0.21f) * std::cos(x * 0.11f - y * 0.29f);
            float r = 0.22f, g = 0.36f, b = 0.16f; // grass
            if (qx == 1 && qy == 0) {              // asphalt
                r = 0.16f + 0.04f * n;
                g = 0.16f + 0.03f * n;
                b = 0.17f + 0.03f * n;
            } else if (qx == 0 && qy == 1) { // dirt
                r = 0.32f + 0.06f * n;
                g = 0.24f + 0.04f * n;
                b = 0.14f;
            } else if (qx == 1 && qy == 1) { // concrete
                r = g = b = 0.42f + 0.08f * n;
            } else {
                r = 0.18f + 0.10f * n;
                g = 0.32f + 0.10f * n;
                b = 0.12f + 0.04f * n;
            }
            const int i = (y * N + x) * 3;
            p[i] = static_cast<unsigned char>(r * 255.f);
            p[i + 1] = static_cast<unsigned char>(g * 255.f);
            p[i + 2] = static_cast<unsigned char>(b * 255.f);
        }
    }
    auto* tex = new osg::Texture2D(img);
    tex->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    tex->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    tex->setUnRefImageDataAfterApply(true);
    return tex;
}

osg::Texture2D* shared_detail_atlas() {
    static osg::ref_ptr<osg::Texture2D> t;
    if (!t)
        t = make_detail_atlas();
    return t.get();
}

bool have_nvidia_device() {
#ifdef __linux__
    return access("/dev/nvidiactl", F_OK) == 0 || access("/dev/nvidia0", F_OK) == 0;
#else
    return false;
#endif
}

bool prefer_rgb_textures() {
    const char* sw = std::getenv("LIBGL_ALWAYS_SOFTWARE");
    if (sw && sw[0] && std::strcmp(sw, "0") != 0)
        return true;
    const char* g = std::getenv("GALLIUM_DRIVER");
    if (g && (std::strstr(g, "llvmpipe") || std::strstr(g, "softpipe") || std::strstr(g, "swrast")))
        return true;
    // CI xvfb / this box: Mesa llvmpipe with neither env set. Hangar 980 Ti
    // / 5060 Ti have /dev/nvidiactl and keep the NVIDIA 4.6 compat path.
    if (!have_nvidia_device())
        return true;
    return false;
}

void configure_terrain_tex(osg::Texture2D* tex, osg::Image* img) {
    if (!tex)
        return;
    tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    tex->setResizeNonPowerOfTwoHint(false);
    tex->setUnRefImageDataAfterApply(true);
    tex->setInternalFormatMode(osg::Texture::USE_IMAGE_DATA_FORMAT);
    const bool compressed = img && img->isCompressed();
    const bool have_mips = img && !img->getMipmapLevels().empty();
    if (compressed && !have_mips) {
        // OSG compileGLObjects + glGenerateMipmap(DXT1) is GL_INVALID_ENUM
        // (445× “invalid enumerant at after stateset.compileGLObjects”).
        tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        tex->setUseHardwareMipMapGeneration(false);
    } else {
        tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
        tex->setUseHardwareMipMapGeneration(true);
    }
    // 16× when the GPU reports it; 980 Ti / 5060 Ti do. Floor 8×.
    tex->setMaxAnisotropy(16.f);
}

void attach_lit_tex(osg::StateSet* ss, osg::Texture2D* tex) {
    ss->setTextureAttributeAndModes(0, tex, osg::StateAttribute::ON);
    ss->addUniform(new osg::Uniform("sTex", 0));
    ss->setTextureAttributeAndModes(1, shared_detail_atlas(), osg::StateAttribute::ON);
    ss->addUniform(new osg::Uniform("sDetail", 1));
    ss->setAttributeAndModes(make_program(kLitTexVS, kLitTexFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

void attach_lit_color(osg::StateSet* ss) {
    ss->setAttributeAndModes(make_program(kLitColorVS, kLitColorFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

void attach_landmark(osg::StateSet* ss) {
    ss->setAttributeAndModes(make_program(kLandmarkVS, kLandmarkFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

void attach_unlit_color(osg::StateSet* ss) {
    ss->setAttributeAndModes(make_program(kUnlitColorVS, kUnlitColorFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

void attach_points(osg::StateSet* ss, float size) {
    ss->setAttributeAndModes(new osg::Point(size));
    ss->addUniform(new osg::Uniform("uPointSize", size));
    ss->addUniform(new osg::Uniform("uPointSizeFar", size));
    ss->addUniform(new osg::Uniform("uSizeNearM", lights::kSizeNearM));
    ss->setAttributeAndModes(make_program(kPointVS, kPointFS), osg::StateAttribute::ON);
    strip_ff(ss);
    ss->setMode(GL_VERTEX_PROGRAM_POINT_SIZE, osg::StateAttribute::ON);
    ss->setMode(GL_POINT_SPRITE, osg::StateAttribute::ON);
}

struct ShaderEnv {
    osg::ref_ptr<osg::Uniform> sun_dir;
    osg::ref_ptr<osg::Uniform> ambient;
    osg::ref_ptr<osg::Uniform> diffuse;
    osg::ref_ptr<osg::Uniform> fog_color;
    osg::ref_ptr<osg::Uniform> fog_density;
    osg::ref_ptr<osg::Uniform> exposure;
    osg::ref_ptr<osg::Uniform> detail;
    osg::ref_ptr<osg::Uniform> ssao;
    osg::ref_ptr<osg::Uniform> up;
    osg::ref_ptr<osg::Uniform> haze;
    osg::ref_ptr<osg::Uniform> water;
    osg::ref_ptr<osg::Uniform> night;
    osg::ref_ptr<osg::Uniform> fwd;
    osg::ref_ptr<osg::Uniform> right;
    osg::ref_ptr<osg::Uniform> tan_half_h;
    osg::ref_ptr<osg::Uniform> tan_half_v;
    osg::ref_ptr<osg::Uniform> cam_up;
    osg::ref_ptr<osg::Uniform> time;
    osg::ref_ptr<osg::Uniform> wetness;
    osg::ref_ptr<osg::Uniform> bloom;
    osg::ref_ptr<osg::Uniform> filmic;
    osg::ref_ptr<osg::Uniform> atmosphere;
    osg::ref_ptr<osg::Uniform> shadows;
    osg::ref_ptr<osg::Uniform> origin;
    osg::ref_ptr<osg::Uniform> shadow_matrix;
    osg::ref_ptr<osg::Uniform> shadow_matrix_far;
    osg::ref_ptr<osg::Uniform> light_count;
    osg::ref_ptr<osg::Uniform> light_pos;
    osg::ref_ptr<osg::Uniform> light_col;
    osg::ref_ptr<osg::Uniform> clustered;
    osg::ref_ptr<osg::Uniform> lod_fade;
    osg::ref_ptr<osg::Uniform> viewport;
    osg::ref_ptr<osg::Uniform> city_dir;
};

constexpr unsigned kCastShadow = 0x2u;

void no_shadow(osg::Node* n) {
    if (n)
        n->setNodeMask(n->getNodeMask() & ~kCastShadow);
}

void attach_env_uniforms(osg::StateSet* ss, ShaderEnv& env) {
    env.sun_dir = new osg::Uniform("uSunDir", osg::Vec3(1.f, 0.f, 0.f));
    env.ambient = new osg::Uniform("uAmbient", osg::Vec3(0.15f, 0.15f, 0.18f));
    env.diffuse = new osg::Uniform("uDiffuse", osg::Vec3(1.f, 0.95f, 0.85f));
    env.fog_color = new osg::Uniform("uFogColor", osg::Vec3(0.62f, 0.70f, 0.78f));
    env.fog_density = new osg::Uniform("uFogDensity", 1e-6f);
    env.exposure = new osg::Uniform("uExposure", 1.f);
    env.detail = new osg::Uniform("uDetail", 1.f);
    env.ssao = new osg::Uniform("uSsao", 0.f);
    env.up = new osg::Uniform("uUp", osg::Vec3(0.f, 0.f, 1.f));
    env.haze = new osg::Uniform("uHaze", 4e-6f);
    env.water = new osg::Uniform("uWater", 1.f);
    env.night = new osg::Uniform("uNight", 0.f);
    env.fwd = new osg::Uniform("uFwd", osg::Vec3(0.f, 1.f, 0.f));
    env.right = new osg::Uniform("uRight", osg::Vec3(1.f, 0.f, 0.f));
    env.tan_half_h = new osg::Uniform("uTanHalfH", 0.466f);
    env.tan_half_v = new osg::Uniform("uTanHalfV", 0.292f);
    env.cam_up = new osg::Uniform("uCamUp", osg::Vec3(0.f, 0.f, 1.f));
    env.time = new osg::Uniform("uTime", 12.f);
    env.wetness = new osg::Uniform("uWetness", 0.f);
    env.bloom = new osg::Uniform("uBloom", 1.f);
    env.filmic = new osg::Uniform("uFilmic", 1.f);
    env.atmosphere = new osg::Uniform("uAtmosphere", 1.f);
    env.shadows = new osg::Uniform("uShadows", 0.f);
    env.origin = new osg::Uniform("uOrigin", osg::Vec3(0.f, 0.f, 0.f));
    env.shadow_matrix = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "uShadowMatrix");
    env.shadow_matrix->set(osg::Matrixf());
    env.shadow_matrix_far = new osg::Uniform(osg::Uniform::FLOAT_MAT4, "uShadowMatrixFar");
    env.shadow_matrix_far->set(osg::Matrixf());
    env.light_count = new osg::Uniform("uLightCount", 0);
    env.light_pos = new osg::Uniform(osg::Uniform::FLOAT_VEC4, "uLightPosR", 32);
    env.light_col = new osg::Uniform(osg::Uniform::FLOAT_VEC3, "uLightCol", 32);
    for (int i = 0; i < 32; ++i) {
        env.light_pos->setElement(i, osg::Vec4(0.f, 0.f, 0.f, 1.f));
        env.light_col->setElement(i, osg::Vec3(0.f, 0.f, 0.f));
    }
    env.clustered = new osg::Uniform("uClustered", 0.f);
    env.lod_fade = new osg::Uniform("uLodFade", 1.f);
    env.viewport = new osg::Uniform("uViewport", osg::Vec2(1920.f, 1200.f));
    env.city_dir = new osg::Uniform("uCityDir", osg::Vec3(0.f, 0.f, 0.f));
    ss->addUniform(env.sun_dir);
    ss->addUniform(env.ambient);
    ss->addUniform(env.diffuse);
    ss->addUniform(env.fog_color);
    ss->addUniform(env.fog_density);
    ss->addUniform(env.exposure);
    ss->addUniform(env.detail);
    ss->addUniform(env.ssao);
    ss->addUniform(env.up);
    ss->addUniform(env.haze);
    ss->addUniform(env.water);
    ss->addUniform(env.night);
    ss->addUniform(env.fwd);
    ss->addUniform(env.right);
    ss->addUniform(env.tan_half_h);
    ss->addUniform(env.tan_half_v);
    ss->addUniform(env.cam_up);
    ss->addUniform(env.time);
    ss->addUniform(env.wetness);
    ss->addUniform(env.bloom);
    ss->addUniform(env.filmic);
    ss->addUniform(env.atmosphere);
    ss->addUniform(env.shadows);
    ss->addUniform(env.origin);
    ss->addUniform(env.shadow_matrix);
    ss->addUniform(env.shadow_matrix_far);
    ss->addUniform(env.light_count);
    ss->addUniform(env.light_pos);
    ss->addUniform(env.light_col);
    ss->addUniform(env.clustered);
    ss->addUniform(env.lod_fade);
    ss->addUniform(env.viewport);
    ss->addUniform(env.city_dir);
    ss->addUniform(new osg::Uniform("sShadow", 2));
    ss->addUniform(new osg::Uniform("sShadowFar", 3));
    strip_ff(ss);
}

osg::Camera* make_fxaa_cam(osg::Texture2D* color, int w, int h) {
    auto* cam = new osg::Camera;
    cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    cam->setProjectionMatrix(osg::Matrix::identity());
    cam->setViewMatrix(osg::Matrix::identity());
    cam->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    cam->setCullingMode(osg::CullSettings::NO_CULLING);
    cam->setClearMask(0);
    cam->setAllowEventFocus(false);
    cam->setRenderOrder(osg::Camera::POST_RENDER, 20);
    cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER);
    cam->setViewport(0, 0, w, h);
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    verts->push_back(osg::Vec3(-1.f, -1.f, 0.f));
    verts->push_back(osg::Vec3(1.f, -1.f, 0.f));
    verts->push_back(osg::Vec3(1.f, 1.f, 0.f));
    verts->push_back(osg::Vec3(-1.f, 1.f, 0.f));
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    idx->push_back(0);
    idx->push_back(1);
    idx->push_back(2);
    idx->push_back(0);
    idx->push_back(2);
    idx->push_back(3);
    geom->setVertexArray(verts);
    geom->addPrimitiveSet(idx);
    geom->setCullingActive(false);
    auto* ss = geom->getOrCreateStateSet();
    ss->setTextureAttributeAndModes(0, color, osg::StateAttribute::ON);
    ss->addUniform(new osg::Uniform("sColor", 0));
    ss->addUniform(new osg::Uniform("uInvRes", osg::Vec2(1.f / float(std::max(1, w)),
                                                         1.f / float(std::max(1, h)))));
    ss->setAttributeAndModes(make_program(kFxaaVS, kFxaaFS), osg::StateAttribute::ON);
    ss->setMode(GL_BLEND, osg::StateAttribute::OFF);
    ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, false));
    strip_ff(ss);
    geode->addDrawable(geom);
    geode->setCullingActive(false);
    cam->addChild(geode);
    cam->setCullingActive(false);
    no_shadow(cam);
    return cam;
}

osg::Camera* make_taa_cam(osg::Texture2D* color, osg::Texture2D* hist, int w, int h,
                          osg::Uniform*& taa_u, osg::StateSet*& out_ss) {
    auto* cam = new osg::Camera;
    cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    cam->setProjectionMatrix(osg::Matrix::identity());
    cam->setViewMatrix(osg::Matrix::identity());
    cam->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    cam->setCullingMode(osg::CullSettings::NO_CULLING);
    cam->setClearMask(0);
    cam->setAllowEventFocus(false);
    cam->setRenderOrder(osg::Camera::POST_RENDER, 20);
    cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER);
    cam->setViewport(0, 0, w, h);
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    verts->push_back(osg::Vec3(-1.f, -1.f, 0.f));
    verts->push_back(osg::Vec3(1.f, -1.f, 0.f));
    verts->push_back(osg::Vec3(1.f, 1.f, 0.f));
    verts->push_back(osg::Vec3(-1.f, 1.f, 0.f));
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    idx->push_back(0);
    idx->push_back(1);
    idx->push_back(2);
    idx->push_back(0);
    idx->push_back(2);
    idx->push_back(3);
    geom->setVertexArray(verts);
    geom->addPrimitiveSet(idx);
    geom->setCullingActive(false);
    auto* ss = geom->getOrCreateStateSet();
    ss->setTextureAttributeAndModes(0, color, osg::StateAttribute::ON);
    ss->addUniform(new osg::Uniform("sColor", 0));
    ss->setTextureAttributeAndModes(1, hist, osg::StateAttribute::ON);
    ss->addUniform(new osg::Uniform("sHist", 1));
    ss->addUniform(new osg::Uniform("uInvRes", osg::Vec2(1.f / float(std::max(1, w)),
                                                         1.f / float(std::max(1, h)))));
    taa_u = new osg::Uniform("uTaa", 1.f);
    ss->addUniform(taa_u);
    ss->setAttributeAndModes(make_program(kFxaaVS, kTaaFS), osg::StateAttribute::ON);
    ss->setMode(GL_BLEND, osg::StateAttribute::OFF);
    ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, false));
    strip_ff(ss);
    out_ss = ss;
    geode->addDrawable(geom);
    geode->setCullingActive(false);
    cam->addChild(geode);
    cam->setCullingActive(false);
    no_shadow(cam);
    return cam;
}

osg::Texture2D* make_csm_tex(int n) {
    auto* t = new osg::Texture2D;
    t->setTextureSize(n, n);
    t->setInternalFormat(GL_DEPTH_COMPONENT24);
    t->setSourceFormat(GL_DEPTH_COMPONENT);
    t->setSourceType(GL_FLOAT);
    t->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    t->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    t->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_BORDER);
    t->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_BORDER);
    t->setBorderColor(osg::Vec4(1.f, 1.f, 1.f, 1.f));
    t->setResizeNonPowerOfTwoHint(false);
    return t;
}

osg::Camera* make_csm_cam(osg::Texture2D* tex, osg::Node* casters, int n, int order) {
    auto* cam = new osg::Camera;
    cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    cam->setRenderOrder(osg::Camera::PRE_RENDER, order);
    cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    cam->attach(osg::Camera::DEPTH_BUFFER, tex);
    cam->setClearMask(GL_DEPTH_BUFFER_BIT);
    cam->setClearColor(osg::Vec4(1.f, 1.f, 1.f, 1.f));
    cam->setViewport(0, 0, n, n);
    cam->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    cam->setCullingActive(false);
    if (casters)
        cam->addChild(casters);
    no_shadow(cam);
    return cam;
}

float cluster_radius_m(LightKind k) {
    switch (k) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Threshold:
    case LightKind::Reil:
        return 120.f;
    case LightKind::Edge:
    case LightKind::Centerline:
    case LightKind::End:
        return 90.f;
    case LightKind::Vasi:
        return 160.f;
    case LightKind::Beacon:
        return 220.f;
    case LightKind::Night:
        return 40.f;
    default:
        return 50.f;
    }
}

osg::Node* make_sky_dome() {
    // Nested identity camera + clip-space quad. A world-space dome was
    // culled (ECEF) or produced a zenith trapezoid (UV sphere).
    auto* cam = new osg::Camera;
    cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    cam->setProjectionMatrix(osg::Matrix::identity());
    cam->setViewMatrix(osg::Matrix::identity());
    cam->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    cam->setCullingMode(osg::CullSettings::NO_CULLING);
    cam->setClearMask(0);
    cam->setAllowEventFocus(false);
    cam->setRenderOrder(osg::Camera::NESTED_RENDER, -10);
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    verts->push_back(osg::Vec3(-1.f, -1.f, 0.f));
    verts->push_back(osg::Vec3(1.f, -1.f, 0.f));
    verts->push_back(osg::Vec3(1.f, 1.f, 0.f));
    verts->push_back(osg::Vec3(-1.f, 1.f, 0.f));
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    idx->push_back(0);
    idx->push_back(1);
    idx->push_back(2);
    idx->push_back(0);
    idx->push_back(2);
    idx->push_back(3);
    geom->setVertexArray(verts);
    geom->addPrimitiveSet(idx);
    geom->setCullingActive(false);
    auto* ss = geom->getOrCreateStateSet();
    ss->setAttributeAndModes(make_program(kSkyVS, kSkyFS), osg::StateAttribute::ON);
    ss->setMode(GL_BLEND, osg::StateAttribute::OFF);
    ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false));
    strip_ff(ss);
    geode->addDrawable(geom);
    geode->setCullingActive(false);
    cam->addChild(geode);
    cam->setCullingActive(false);
    return cam;
}

osg::Image* make_globe_image(int w, int h) {
    auto* img = new osg::Image;
    img->allocateImage(w, h, 1, GL_RGB, GL_UNSIGNED_BYTE);
    auto* p = img->data();
    const double dlat = 180.0 / h;
    const double dlon = 360.0 / w;
    for (int y = 0; y < h; ++y) {
        const double lat = 90.0 - 180.0 * (y + 0.5) / h;
        for (int x = 0; x < w; ++x) {
            const double lon = -180.0 + 360.0 * (x + 0.5) / w;
            float cover = 0.f;
            for (int sy = 0; sy < 2; ++sy) {
                for (int sx = 0; sx < 2; ++sx) {
                    cover += geo::land_coverage(lat + (sy - 0.5) * 0.45 * dlat,
                                                lon + (sx - 0.5) * 0.45 * dlon);
                }
            }
            cover *= 0.25f;
            const int i = (y * w + x) * 3;
            if (lat > 40.50 && lat < 40.82 && lon > -74.06 && lon < -73.70) {
                std::uint8_t r = 0, g = 0, b = 0;
                bool water = false;
                kjfk_subset_land_rgb(lat, lon, r, g, b, water);
                p[i] = r;
                p[i + 1] = g;
                p[i + 2] = b;
            } else {
                const float ice = (lat > 70.f || lat < -60.f) ? 1.f : 0.f;
                const float water_r = 0.07f, water_g = 0.16f, water_b = 0.38f;
                const float land_r = 0.20f + 0.04f * ice, land_g = 0.38f - 0.10f * ice,
                            land_b = 0.16f + 0.50f * ice;
                const float r = water_r + (land_r - water_r) * cover;
                const float g = water_g + (land_g - water_g) * cover;
                const float b = water_b + (land_b - water_b) * cover;
                p[i] = static_cast<unsigned char>(std::max(0, std::min(255, int(r * 255.f))));
                p[i + 1] = static_cast<unsigned char>(std::max(0, std::min(255, int(g * 255.f))));
                p[i + 2] = static_cast<unsigned char>(std::max(0, std::min(255, int(b * 255.f))));
            }
        }
    }
    return img;
}

osg::Node* make_procedural_globe(float aniso = 16.f) {
    auto* geode = new osg::Geode;
    constexpr int nlon = 160, nlat = 80;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* uvs = new osg::Vec2Array;
    verts->reserve((nlon + 1) * (nlat + 1));
    for (int j = 0; j <= nlat; ++j) {
        const double lat = 90.0 - 180.0 * j / nlat;
        for (int i = 0; i <= nlon; ++i) {
            const double lon = -180.0 + 360.0 * i / nlon;
            const geo::Ecef p = geo::lla_to_ecef(lat, lon, -80.0);
            verts->push_back(ecef_to_osg(p));
            geo::Ecef e, n, u;
            geo::ecef_to_enu_axes(lat, lon, e, n, u);
            norms->push_back(osg::Vec3(static_cast<float>(u.x), static_cast<float>(u.y),
                                       static_cast<float>(u.z)));
            uvs->push_back(
                osg::Vec2(static_cast<float>(i) / nlon, 1.f - static_cast<float>(j) / nlat));
        }
    }
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    for (int j = 0; j < nlat; ++j) {
        for (int i = 0; i < nlon; ++i) {
            const unsigned a = static_cast<unsigned>(j * (nlon + 1) + i);
            const unsigned b = a + 1;
            const unsigned c = a + static_cast<unsigned>(nlon + 1);
            const unsigned d = c + 1;
            idx->push_back(a);
            idx->push_back(c);
            idx->push_back(b);
            idx->push_back(b);
            idx->push_back(c);
            idx->push_back(d);
        }
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setTexCoordArray(0, uvs);
    geom->addPrimitiveSet(idx);

    auto* tex = new osg::Texture2D(make_globe_image(2048, 1024));
    tex->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    tex->setMaxAnisotropy(std::max(8.f, aniso));
    tex->setLODBias(-0.35f);
    attach_lit_tex(geom->getOrCreateStateSet(), tex);
    geode->addDrawable(geom);
    return geode;
}

osg::Image* image_from_rgb(const ImageRgb& im) {
    auto* img = new osg::Image;
    img->allocateImage(im.w, im.h, 1, GL_RGB, GL_UNSIGNED_BYTE);
    if (!im.rgb.empty())
        std::memcpy(img->data(), im.rgb.data(), im.rgb.size());
    return img;
}

void align_dxt_for_osg(dxt::CompressedImage& c) {
    const int aw = (c.w + 3) & ~3;
    const int ah = (c.h + 3) & ~3;
    if (aw == c.w && ah == c.h)
        return;
    // Block payload already covers the rounded-up size. Drop file mips —
    // their offsets assume the odd header size and will mis-bind.
    const std::size_t base = (c.fmt == dxt::BlockFmt::Bc7) ? dxt::bc7_size(c.w, c.h)
                                                           : dxt::bc1_size(c.w, c.h);
    if (c.blocks.size() >= base)
        c.blocks.resize(base);
    c.w = aw;
    c.h = ah;
    c.mips = 1;
}

osg::Image* image_from_compressed_dds(const std::string& path) {
    dxt::CompressedImage c;
    if (!dxt::read_dds(path, c) || c.blocks.empty())
        return nullptr;
    // OSG 3.6 does not treat BPTC as compressed — glTexImage2D(BPTC) is
    // GL_INVALID_ENUM. Transcode our mode-6 BC7 to DXT1 (block-by-block).
    dxt::make_osg_safe(c);
    align_dxt_for_osg(c);
    if (!prefer_rgb_textures())
        dxt::ensure_bc1_mips(c);
    // llvmpipe / softpipe have no S3TC. Decode so the ortho actually binds.
    if (prefer_rgb_textures()) {
        auto rgb = dxt::decode_bc1(c.blocks.data(), c.w, c.h);
        ImageRgb im;
        im.w = c.w;
        im.h = c.h;
        im.rgb = std::move(rgb);
        return image_from_rgb(im);
    }
    const GLenum fmt = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    auto* data = new unsigned char[c.blocks.size()];
    std::memcpy(data, c.blocks.data(), c.blocks.size());
    auto* img = new osg::Image;
    img->setImage(c.w, c.h, 1, fmt, fmt, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
    if (c.mips > 1) {
        osg::Image::MipmapDataType offs;
        std::size_t o = dxt::bc1_size(c.w, c.h);
        int w = c.w, h = c.h;
        for (int i = 1; i < c.mips; ++i) {
            offs.push_back(o);
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            o += dxt::bc1_size(w, h);
        }
        img->setMipmapLevels(offs);
    }
    return img;
}

osg::Image* image_from_dds_or_rgb(const CacheRegion& cache) {
    if (!cache.imagery_dds.empty() && std::filesystem::exists(cache.imagery_dds)) {
        if (auto* packed = image_from_compressed_dds(cache.imagery_dds))
            return packed;
        // Never osgDB::readImageFile — OSG decodes BC/DXT to RGBA (48 MB/tile).
    }
    if (!cache.imagery.rgb.empty())
        return image_from_rgb(cache.imagery);
    if (!cache.dem.empty() && cache.dem.w * cache.dem.h <= 256 * 256)
        return image_from_rgb(ImageRgb::hillshade(cache.dem));
    ImageRgb fb;
    fb.w = 8;
    fb.h = 8;
    fb.rgb.resize(8 * 8 * 3);
    for (int i = 0; i < 8 * 8; ++i) {
        fb.rgb[static_cast<std::size_t>(i) * 3 + 0] = 70;
        fb.rgb[static_cast<std::size_t>(i) * 3 + 1] = 88;
        fb.rgb[static_cast<std::size_t>(i) * 3 + 2] = 52;
    }
    return image_from_rgb(fb);
}

osg::Node* make_dem_patch(const CacheRegion& cache, float aniso = 16.f) {
    const DemGrid& dem = cache.dem;
    if (dem.empty())
        return nullptr;
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* uvs = new osg::Vec2Array;
    auto* wuvs = new osg::Vec3Array;
    const int w = dem.w, h = dem.h;
    const int nvert = w * h;
    std::vector<double> zs(static_cast<std::size_t>(nvert));
    std::vector<double> lats(static_cast<std::size_t>(nvert));
    std::vector<double> lons(static_cast<std::size_t>(nvert));
    std::vector<char> water(static_cast<std::size_t>(nvert), 0);
    verts->reserve(static_cast<std::size_t>(nvert));
    for (int y = 0; y < h; ++y) {
        const double lat = dem.north - (dem.north - dem.south) * y / std::max(1, h - 1);
        for (int x = 0; x < w; ++x) {
            const double lon = dem.west + (dem.east - dem.west) * x / std::max(1, w - 1);
            const int i = y * w + x;
            lats[static_cast<std::size_t>(i)] = lat;
            lons[static_cast<std::size_t>(i)] = lon;
            double z = dem.z_m[static_cast<std::size_t>(i)];
            const float cover = geo::land_coverage(lat, lon);
            const bool jfk_pad =
                lat > 40.626 && lat < 40.662 && lon > -73.828 && lon < -73.752;
            const bool dem_wet = z < 0.80;
            const bool dem_dry = z > 1.60;
            if (jfk_pad && z < 10.0 && !dem_wet) {
                // Synth land is 4 m / water 0 m. A mixed cell became the
                // 2500 ft Manhattan-shot "tile triangle" across the pad.
                z = 4.0;
            } else if (dem_dry && cover > 0.20f) {
                // 1 m 3DEP barrier / pad wins over the coarse GSHHS box.
            } else if (dem_wet && cover < 0.85f) {
                z = 0.0;
                water[static_cast<std::size_t>(i)] = 1;
            } else if (cover < 0.40f) {
                z = 0.0; // flatten bay so 96² tiles are not a faceted green grid
                water[static_cast<std::size_t>(i)] = 1;
            }
            zs[static_cast<std::size_t>(i)] = z;
        }
    }
    const double dlat_m =
        geo::kWgsA * geo::kDeg * (dem.north - dem.south) / static_cast<double>(std::max(1, h - 1));
    auto z_at = [&](int x, int y) {
        x = std::max(0, std::min(w - 1, x));
        y = std::max(0, std::min(h - 1, y));
        return zs[static_cast<std::size_t>(y * w + x)];
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            const double lat = lats[static_cast<std::size_t>(i)];
            const double lon = lons[static_cast<std::size_t>(i)];
            const double z = zs[static_cast<std::size_t>(i)];
            const geo::Ecef p = geo::lla_to_ecef(lat, lon, z);
            verts->push_back(ecef_to_osg(p));
            geo::Ecef e, n, u;
            geo::ecef_to_enu_axes(lat, lon, e, n, u);
            if (water[static_cast<std::size_t>(i)]) {
                // Flat water: both sides of a tile edge share geodetic-up.
                norms->push_back(osg::Vec3(static_cast<float>(u.x), static_cast<float>(u.y),
                                           static_cast<float>(u.z)));
            } else {
                const double dlon_m = geo::kWgsA * geo::kDeg * std::cos(geo::deg2rad(lat)) *
                                      (dem.east - dem.west) / static_cast<double>(std::max(1, w - 1));
                const int xm = std::max(0, x - 1), xp = std::min(w - 1, x + 1);
                const int ym = std::max(0, y - 1), yp = std::min(h - 1, y + 1);
                const double de = std::max(1e-3, static_cast<double>(xp - xm) * dlon_m);
                const double dn = std::max(1e-3, static_cast<double>(yp - ym) * dlat_m);
                const double dze = (z_at(xp, y) - z_at(xm, y)) / de;
                const double dzn = (z_at(x, ym) - z_at(x, yp)) / dn; // y grows south
                geo::Ecef N{u.x - dze * e.x - dzn * n.x, u.y - dze * e.y - dzn * n.y,
                            u.z - dze * e.z - dzn * n.z};
                const double mag = std::sqrt(N.x * N.x + N.y * N.y + N.z * N.z);
                if (mag > 1e-9) {
                    N.x /= mag;
                    N.y /= mag;
                    N.z /= mag;
                } else {
                    N = u;
                }
                norms->push_back(osg::Vec3(static_cast<float>(N.x), static_cast<float>(N.y),
                                           static_cast<float>(N.z)));
            }
            // Shared geographic edges sample the edge texel (not a half-texel
            // inset, which painted a different colour on each side of a join).
            uvs->push_back(osg::Vec2(static_cast<float>(x) / static_cast<float>(std::max(1, w - 1)),
                                     static_cast<float>(y) / static_cast<float>(std::max(1, h - 1))));
            wuvs->push_back(osg::Vec3(static_cast<float>(lon), static_cast<float>(lat),
                                      water[static_cast<std::size_t>(i)] ? 1.f : 0.f));
        }
    }
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    for (int y = 0; y < dem.h - 1; ++y) {
        for (int x = 0; x < dem.w - 1; ++x) {
            const unsigned a = static_cast<unsigned>(y * dem.w + x);
            const unsigned b = a + 1;
            const unsigned c = a + static_cast<unsigned>(dem.w);
            const unsigned d = c + 1;
            idx->push_back(a);
            idx->push_back(c);
            idx->push_back(b);
            idx->push_back(b);
            idx->push_back(c);
            idx->push_back(d);
        }
    }
    // Skirts: drop the boundary 40 m so adjacent-tile cracks do not show sky.
    const unsigned base = static_cast<unsigned>(w * h);
    auto drop = [&](int x, int y) {
        const unsigned i = static_cast<unsigned>(y * w + x);
        const double lat = lats[i];
        const double lon = lons[i];
        const double z = zs[i] - 40.0;
        const geo::Ecef e = geo::lla_to_ecef(lat, lon, z);
        verts->push_back(ecef_to_osg(e));
        norms->push_back((*norms)[i]);
        uvs->push_back((*uvs)[i]);
        wuvs->push_back((*wuvs)[i]);
        return static_cast<unsigned>(verts->size() - 1);
    };
    auto skirt_edge = [&](int x0, int y0, int x1, int y1) {
        const unsigned a = static_cast<unsigned>(y0 * w + x0);
        const unsigned b = static_cast<unsigned>(y1 * w + x1);
        const unsigned c = drop(x0, y0);
        const unsigned d = drop(x1, y1);
        idx->push_back(a);
        idx->push_back(b);
        idx->push_back(c);
        idx->push_back(b);
        idx->push_back(d);
        idx->push_back(c);
    };
    (void)base;
    for (int x = 0; x < w - 1; ++x) {
        skirt_edge(x, 0, x + 1, 0);
        skirt_edge(x, h - 1, x + 1, h - 1);
    }
    for (int y = 0; y < h - 1; ++y) {
        skirt_edge(0, y, 0, y + 1);
        skirt_edge(w - 1, y, w - 1, y + 1);
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setTexCoordArray(0, uvs);
    geom->setTexCoordArray(1, wuvs);
    geom->addPrimitiveSet(idx);

    osg::Image* img = image_from_dds_or_rgb(cache);
    auto* tex = new osg::Texture2D(img);
    configure_terrain_tex(tex, img);
    tex->setMaxAnisotropy(std::max(8.f, aniso));
    tex->setLODBias(-0.35f);
    attach_lit_tex(geom->getOrCreateStateSet(), tex);
    geode->addDrawable(geom);
    return geode;
}

// High-res land/water patch around an airport so 12k/oblique views stay
// sharp. Same land_coverage as the globe, so the edge is a color match
// (no hard horizon seam). Land is raised a few metres over the ellipsoid.
osg::Node* make_local_ground(double lat0, double lon0, double radius_km, float aniso) {
    const bool jfk = lat0 > 40.3 && lat0 < 41.0 && lon0 > -74.3 && lon0 < -73.4;
    const int n = jfk ? (radius_km <= 20.0 ? 240 : 160) : 80;
    const int tw = 384, th = 384;
    auto* img = new osg::Image;
    img->allocateImage(tw, th, 1, GL_RGB, GL_UNSIGNED_BYTE);
    auto* px = img->data();
    const double rlat = radius_km / 111.32;
    const double rlon = rlat / std::max(0.2, std::cos(geo::deg2rad(lat0)));
    for (int y = 0; y < th; ++y) {
        const double lat = lat0 + rlat * (1.0 - 2.0 * (y + 0.5) / th);
        for (int x = 0; x < tw; ++x) {
            const double lon = lon0 + rlon * (2.0 * (x + 0.5) / tw - 1.0);
            const int i = (y * tw + x) * 3;
            if (jfk) {
                std::uint8_t r = 0, g = 0, b = 0;
                bool water = false;
                kjfk_subset_land_rgb(lat, lon, r, g, b, water);
                px[i] = r;
                px[i + 1] = g;
                px[i + 2] = b;
            } else {
                float cover = 0.f;
                for (int sy = 0; sy < 2; ++sy)
                    for (int sx = 0; sx < 2; ++sx)
                        cover += geo::land_coverage(lat + (sy - 0.5) * rlat / th,
                                                    lon + (sx - 0.5) * rlon / tw);
                cover *= 0.25f;
                const float wr = 0.07f, wg = 0.16f, wb = 0.38f;
                const float lr = 0.22f, lg = 0.36f, lb = 0.16f;
                px[i] = static_cast<unsigned char>((wr + (lr - wr) * cover) * 255.f);
                px[i + 1] = static_cast<unsigned char>((wg + (lg - wg) * cover) * 255.f);
                px[i + 2] = static_cast<unsigned char>((wb + (lb - wb) * cover) * 255.f);
            }
        }
    }

    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* uvs = new osg::Vec2Array;
    auto* wuvs = new osg::Vec3Array;
    verts->reserve(static_cast<std::size_t>((n + 1) * (n + 1)));
    for (int j = 0; j <= n; ++j) {
        const double v = static_cast<double>(j) / n;
        const double lat = lat0 + rlat * (1.0 - 2.0 * v);
        for (int i = 0; i <= n; ++i) {
            const double u = static_cast<double>(i) / n;
            const double lon = lon0 + rlon * (2.0 * u - 1.0);
            const float cover = geo::land_coverage(lat, lon);
            const double edge = std::max(std::fabs(u - 0.5), std::fabs(v - 0.5)) * 2.0;
            const double fade = edge > 0.82 ? std::max(0.0, 1.0 - (edge - 0.82) / 0.18) : 1.0;
            const double z = cover * 12.0 * fade;
            const geo::Ecef p = geo::lla_to_ecef(lat, lon, z);
            verts->push_back(ecef_to_osg(p));
            geo::Ecef e, nn, up;
            geo::ecef_to_enu_axes(lat, lon, e, nn, up);
            norms->push_back(osg::Vec3(static_cast<float>(up.x), static_cast<float>(up.y),
                                       static_cast<float>(up.z)));
            uvs->push_back(osg::Vec2(static_cast<float>(u), static_cast<float>(1.0 - v)));
            wuvs->push_back(osg::Vec3(static_cast<float>(lon), static_cast<float>(lat),
                                      cover < 0.35f ? 1.f : 0.f));
        }
    }
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const unsigned a = static_cast<unsigned>(j * (n + 1) + i);
            const unsigned b = a + 1;
            const unsigned c = a + static_cast<unsigned>(n + 1);
            if (jfk) {
                // Water / coast only — packed tiles already paint Queens land.
                const float wa = (*wuvs)[a].z(), wb = (*wuvs)[b].z();
                const float wc = (*wuvs)[c].z(), wd = (*wuvs)[c + 1].z();
                if (wa + wb + wc + wd < 0.5f)
                    continue;
            }
            idx->push_back(a);
            idx->push_back(c);
            idx->push_back(b);
            idx->push_back(b);
            idx->push_back(c);
            idx->push_back(c + 1);
        }
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setTexCoordArray(0, uvs);
    geom->setTexCoordArray(1, wuvs);
    geom->addPrimitiveSet(idx);
    auto* tex = new osg::Texture2D(img);
    configure_terrain_tex(tex, img);
    tex->setMaxAnisotropy(std::max(8.f, aniso));
    tex->setLODBias(-0.4f);
    attach_lit_tex(geom->getOrCreateStateSet(), tex);
    geode->addDrawable(geom);
    return geode;
}

osg::Node* make_airport_geode(const AirportMesh& mesh) {
    if (mesh.verts.size() < 3)
        return nullptr;
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* cols = new osg::Vec4Array;
    verts->reserve(mesh.verts.size());
    for (const auto& v : mesh.verts) {
        verts->push_back(ecef_to_osg(v.p));
        norms->push_back(osg::Vec3(static_cast<float>(v.n.x), static_cast<float>(v.n.y),
                                   static_cast<float>(v.n.z)));
        cols->push_back(osg::Vec4(v.r, v.g, v.b, v.mat));
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setColorArray(cols, osg::Array::BIND_PER_VERTEX);
    geom->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, static_cast<int>(mesh.verts.size())));
    auto* ss = geom->getOrCreateStateSet();
    attach_lit_color(ss);
    ss->setAttributeAndModes(new osg::PolygonOffset(-24.f, -24.f), osg::StateAttribute::ON);
    ss->setMode(GL_POLYGON_OFFSET_FILL, osg::StateAttribute::ON);
    ss->setRenderBinDetails(2, "RenderBin");
    geode->addDrawable(geom);
    return geode;
}

osg::Node* make_landmark_geode(const AirportMesh& mesh) {
    if (mesh.verts.size() < 3)
        return nullptr;
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* cols = new osg::Vec4Array;
    auto* uvs = new osg::Vec2Array;
    verts->reserve(mesh.verts.size());
    for (const auto& v : mesh.verts) {
        verts->push_back(ecef_to_osg(v.p));
        norms->push_back(osg::Vec3(static_cast<float>(v.n.x), static_cast<float>(v.n.y),
                                   static_cast<float>(v.n.z)));
        cols->push_back(osg::Vec4(v.r, v.g, v.b, v.mat));
        uvs->push_back(osg::Vec2(v.u, v.v));
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setColorArray(cols, osg::Array::BIND_PER_VERTEX);
    geom->setTexCoordArray(0, uvs);
    geom->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, static_cast<int>(mesh.verts.size())));
    attach_landmark(geom->getOrCreateStateSet());
    geode->addDrawable(geom);
    return geode;
}

struct LightGpu {
    osg::ref_ptr<osg::Geometry> geom;
    osg::ref_ptr<osg::Vec4Array> colors;
    osg::ref_ptr<osg::Vec2Array> sizes;
    std::vector<int> switches;
    std::vector<LightKind> kinds;
    std::vector<int> seq;
    std::vector<osg::Vec4> base;
    std::vector<double> lat;
    std::vector<double> lon;
    std::vector<double> alt_m;
    std::vector<int> papi_box;
    std::vector<float> papi_gs;
    std::vector<float> papi_rot;
    std::vector<float> papi_ul;
    std::vector<float> papi_ur;
    std::vector<float> papi_hdg;
    std::vector<osg::Vec4> rev;
    std::vector<float> color_hdg;
};

LightGpu make_light_gpu(const Airport& apt) {
    LightGpu g;
    g.geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    g.colors = new osg::Vec4Array;
    g.sizes = new osg::Vec2Array;
    verts->reserve(apt.lights.size());
    for (const auto& L : apt.lights) {
        const geo::Ecef p = geo::lla_to_ecef(L.lat, L.lon, L.alt_m);
        verts->push_back(ecef_to_osg(p));
        const osg::Vec4 c(L.r, L.g, L.b, 1);
        g.colors->push_back(c);
        g.base.push_back(c);
        g.switches.push_back(L.switch_num);
        g.kinds.push_back(L.kind);
        g.seq.push_back(L.seq);
        g.lat.push_back(L.lat);
        g.lon.push_back(L.lon);
        g.alt_m.push_back(L.alt_m);
        g.papi_box.push_back(L.papi_box);
        g.papi_gs.push_back(L.papi_gs_deg);
        g.papi_rot.push_back(L.papi_rotate_deg);
        g.papi_ul.push_back(L.papi_usable_left_deg);
        g.papi_ur.push_back(L.papi_usable_right_deg);
        g.papi_hdg.push_back(L.papi_rwy_hdg_deg);
        g.rev.push_back(osg::Vec4(L.r2, L.g2, L.b2, L.r2 >= 0.f ? 1.f : -1.f));
        g.color_hdg.push_back(L.color_hdg_deg);
        g.sizes->push_back(osg::Vec2(lights::kBaseSizePx, 0.f));
    }
    g.geom->setVertexArray(verts);
    g.geom->setColorArray(g.colors, osg::Array::BIND_PER_VERTEX);
    g.geom->setTexCoordArray(1, g.sizes);
    g.geom->addPrimitiveSet(
        new osg::DrawArrays(GL_POINTS, 0, static_cast<int>(apt.lights.size())));
    g.geom->setUseDisplayList(false);
    g.geom->setUseVertexBufferObjects(true);
    auto* ss = g.geom->getOrCreateStateSet();
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE));
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false));
    attach_points(ss, lights::kBaseSizePx);
    ss->setRenderBinDetails(20, "DepthSortedBin");
    return g;
}

struct PrecipGpu {
    osg::ref_ptr<osg::Geometry> geom;
    osg::ref_ptr<osg::Vec3Array> verts;
    osg::ref_ptr<osg::Vec4Array> colors;
    std::vector<osg::Vec3> seed;
};

PrecipGpu make_precip_gpu() {
    PrecipGpu g;
    constexpr int n = 360;
    g.geom = new osg::Geometry;
    g.verts = new osg::Vec3Array;
    g.colors = new osg::Vec4Array;
    g.verts->resize(n);
    g.colors->resize(n);
    g.seed.resize(n);
    for (int i = 0; i < n; ++i) {
        g.seed[static_cast<std::size_t>(i)] =
            osg::Vec3(static_cast<float>((i * 37) % 80 - 40), static_cast<float>((i * 19) % 80 - 40),
                      static_cast<float>((i * 13) % 55));
        (*g.verts)[static_cast<unsigned>(i)] = osg::Vec3(0, 0, 0);
        (*g.colors)[static_cast<unsigned>(i)] = osg::Vec4(0, 0, 0, 0);
    }
    g.geom->setVertexArray(g.verts);
    g.geom->setColorArray(g.colors, osg::Array::BIND_PER_VERTEX);
    g.geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, n));
    g.geom->setUseDisplayList(false);
    g.geom->setUseVertexBufferObjects(true);
    auto* ss = g.geom->getOrCreateStateSet();
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    attach_points(ss, 2.2f);
    ss->setRenderBinDetails(25, "DepthSortedBin");
    return g;
}

float storm_radius_m(const xt4::StormCell& cell) {
    const float k = 0.5f + 0.05f * std::max(0, cell.cfg.precip_intensity);
    return 2778.f * k; // 1.5 nm stand-in; XT4 sized cells from the IG catalog
}

void update_missile_fx(PrecipGpu& g) {
    if (!g.verts || g.verts->empty())
        return;
    const auto& fx = missile_world().fx;
    const unsigned n = g.verts->size();
    for (unsigned i = 0; i < n; ++i) {
        if (i < fx.size()) {
            const auto& f = fx[i];
            const geo::Ecef e = geo::lla_to_ecef(f.lat, f.lon, f.alt_m);
            (*g.verts)[i] = osg::Vec3(static_cast<float>(e.x), static_cast<float>(e.y),
                                      static_cast<float>(e.z));
            (*g.colors)[i] = osg::Vec4(f.r, f.g, f.b, std::max(0.f, f.a * std::min(1.f, f.life)));
        } else {
            (*g.colors)[i] = osg::Vec4(0, 0, 0, 0);
        }
    }
    g.verts->dirty();
    g.colors->dirty();
}

void update_precip(PrecipGpu& g, const HostState& host, float dt) {
    if (!g.verts || g.verts->empty())
        return;
    int ptype = host.have_env ? host.env.precip_type : 0;
    int pint = host.have_env ? host.env.precip_intensity : 0;
    if (host.have_eyepoint) {
        for (const auto& cell : host.storms) {
            if (!cell.cfg.active || !cell.have_pose || cell.cfg.precip_type <= 0)
                continue;
            const double dlat = (host.eye.lat_deg - cell.pose.lat_deg) * 111320.0;
            const double dlon = (host.eye.lon_deg - cell.pose.lon_deg) * 111320.0 *
                                std::cos(geo::deg2rad(host.eye.lat_deg));
            const double dist = std::sqrt(dlat * dlat + dlon * dlon);
            if (dist < storm_radius_m(cell) * 1.2) {
                if (cell.cfg.precip_intensity > pint) {
                    pint = cell.cfg.precip_intensity;
                    ptype = cell.cfg.precip_type;
                }
            }
        }
    }
    const bool on = ptype > 0 && pint > 0 && host.have_eyepoint;
    const float k = on ? std::min(1.f, pint / 10.f) : 0.f;
    if (!on || k <= 0.f) {
        for (unsigned i = 0; i < g.colors->size(); ++i)
            (*g.colors)[i] = osg::Vec4(0, 0, 0, 0);
        g.colors->dirty();
        return;
    }
    const bool snow = ptype == 2;
    const bool hail = ptype == 3;
    const float fall = (snow ? 9.f : hail ? 40.f : 32.f) * dt;
    // Wind FROM dir (deg, 0=N): particles drift with the flow.
    const float wrad = static_cast<float>(geo::deg2rad(host.env.wind_dir_deg));
    const float wspd = host.env.wind_speed_mph * 0.15f;
    const float we = -std::sin(wrad) * wspd;
    const float wn = -std::cos(wrad) * wspd;
    const geo::Ecef eye =
        geo::lla_to_ecef(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes(host.eye.lat_deg, host.eye.lon_deg, e, n, u);
    const osg::Vec4 col = snow ? osg::Vec4(0.92f, 0.94f, 0.98f, 0.55f * k)
                               : hail ? osg::Vec4(0.86f, 0.88f, 0.92f, 0.45f * k)
                                      : osg::Vec4(0.70f, 0.76f, 0.84f, 0.35f * k);
    const float psz = snow ? 3.4f : hail ? 3.8f : 2.2f;
    auto* pss = g.geom->getOrCreateStateSet();
    if (auto* pt = dynamic_cast<osg::Point*>(pss->getAttribute(osg::StateAttribute::POINT)))
        pt->setSize(psz);
    if (auto* u = pss->getUniform("uPointSize"))
        u->set(psz);
    if (auto* u = pss->getUniform("uPointSizeFar"))
        u->set(psz);
    for (std::size_t i = 0; i < g.seed.size(); ++i) {
        osg::Vec3& s = g.seed[i];
        s.z() -= fall;
        s.x() += we * dt;
        s.y() += wn * dt;
        if (s.z() < -8.f)
            s.z() += 55.f;
        if (s.x() > 40.f)
            s.x() -= 80.f;
        if (s.x() < -40.f)
            s.x() += 80.f;
        if (s.y() > 40.f)
            s.y() -= 80.f;
        if (s.y() < -40.f)
            s.y() += 80.f;
        const geo::Ecef p{eye.x + e.x * s.x() + n.x * s.y() + u.x * s.z(),
                          eye.y + e.y * s.x() + n.y * s.y() + u.y * s.z(),
                          eye.z + e.z * s.x() + n.z * s.y() + u.z * s.z()};
        (*g.verts)[static_cast<unsigned>(i)] =
            osg::Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
        (*g.colors)[static_cast<unsigned>(i)] = col;
    }
    g.verts->dirty();
    g.colors->dirty();
    g.geom->dirtyBound();
}

struct MeshGpu {
    osg::ref_ptr<osg::Geometry> geom;
    osg::ref_ptr<osg::Vec3Array> verts;
    osg::ref_ptr<osg::Vec4Array> cols;
    osg::ref_ptr<osg::DrawArrays> draw;
};

MeshGpu make_mesh_gpu(int bin) {
    MeshGpu g;
    g.geom = new osg::Geometry;
    g.verts = new osg::Vec3Array;
    g.cols = new osg::Vec4Array;
    g.draw = new osg::DrawArrays(GL_TRIANGLES, 0, 0);
    g.geom->setVertexArray(g.verts);
    g.geom->setColorArray(g.cols, osg::Array::BIND_PER_VERTEX);
    g.geom->addPrimitiveSet(g.draw);
    g.geom->setUseDisplayList(false);
    g.geom->setUseVertexBufferObjects(true);
    auto* ss = g.geom->getOrCreateStateSet();
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false));
    attach_unlit_color(ss);
    ss->setRenderBinDetails(bin, "DepthSortedBin");
    return g;
}

void mesh_begin(MeshGpu& g) {
    g.verts->clear();
    g.cols->clear();
}

void mesh_end(MeshGpu& g) {
    g.draw->setCount(static_cast<GLint>(g.verts->size()));
    g.verts->dirty();
    g.cols->dirty();
    g.geom->dirtyBound();
}

struct TrafficGpu {
    TrafficWorld world;
    osg::ref_ptr<osg::Geometry> body;
    osg::ref_ptr<osg::Vec3Array> bverts;
    osg::ref_ptr<osg::Vec3Array> bnorms;
    osg::ref_ptr<osg::Vec4Array> bcols;
    osg::ref_ptr<osg::DrawArrays> bdraw;
    osg::ref_ptr<osg::Geometry> lamps;
    osg::ref_ptr<osg::Vec3Array> lverts;
    osg::ref_ptr<osg::Vec4Array> lcols;
    osg::ref_ptr<osg::DrawArrays> ldraw;
};

TrafficGpu make_traffic_gpu() {
    TrafficGpu g;
    g.body = new osg::Geometry;
    g.bverts = new osg::Vec3Array;
    g.bnorms = new osg::Vec3Array;
    g.bcols = new osg::Vec4Array;
    g.bdraw = new osg::DrawArrays(GL_TRIANGLES, 0, 0);
    g.body->setVertexArray(g.bverts);
    g.body->setNormalArray(g.bnorms, osg::Array::BIND_PER_VERTEX);
    g.body->setColorArray(g.bcols, osg::Array::BIND_PER_VERTEX);
    g.body->addPrimitiveSet(g.bdraw);
    g.body->setUseDisplayList(false);
    g.body->setUseVertexBufferObjects(true);
    attach_lit_color(g.body->getOrCreateStateSet());
    g.lamps = new osg::Geometry;
    g.lverts = new osg::Vec3Array;
    g.lcols = new osg::Vec4Array;
    g.ldraw = new osg::DrawArrays(GL_TRIANGLES, 0, 0);
    g.lamps->setVertexArray(g.lverts);
    g.lamps->setColorArray(g.lcols, osg::Array::BIND_PER_VERTEX);
    g.lamps->addPrimitiveSet(g.ldraw);
    g.lamps->setUseDisplayList(false);
    g.lamps->setUseVertexBufferObjects(true);
    auto* lss = g.lamps->getOrCreateStateSet();
    lss->setMode(GL_BLEND, osg::StateAttribute::ON);
    lss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE));
    lss->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false));
    attach_unlit_color(lss);
    lss->setRenderBinDetails(22, "DepthSortedBin");
    return g;
}

void upload_traffic(TrafficGpu& g, const TrafficMesh& mesh) {
    g.bverts->clear();
    g.bnorms->clear();
    g.bcols->clear();
    g.bverts->reserve(mesh.body.size());
    for (const auto& v : mesh.body) {
        g.bverts->push_back(ecef_to_osg(v.p));
        g.bnorms->push_back(osg::Vec3(static_cast<float>(v.n.x), static_cast<float>(v.n.y),
                                     static_cast<float>(v.n.z)));
        g.bcols->push_back(osg::Vec4(v.r, v.g, v.b, v.mat));
    }
    g.bdraw->setCount(static_cast<GLint>(g.bverts->size()));
    g.bverts->dirty();
    g.bnorms->dirty();
    g.bcols->dirty();
    g.body->dirtyBound();
    g.lverts->clear();
    g.lcols->clear();
    g.lverts->reserve(mesh.lamps.size());
    for (const auto& v : mesh.lamps) {
        g.lverts->push_back(ecef_to_osg(v.p));
        g.lcols->push_back(osg::Vec4(v.r, v.g, v.b, 1.f));
    }
    g.ldraw->setCount(static_cast<GLint>(g.lverts->size()));
    g.lverts->dirty();
    g.lcols->dirty();
    g.lamps->dirtyBound();
}

void update_traffic(TrafficGpu& g, const HostState& host, float dt) {
    const float tod = host.have_env ? host.env.tod_hours : 14.5f;
    double lat = 40.64, lon = -73.78, alt = 0;
    if (host.have_eyepoint) {
        lat = host.eye.lat_deg;
        lon = host.eye.lon_deg;
        alt = host.eye.alt_ft * geo::kFtToM;
    }
    step_traffic(g.world, dt, tod, lat, lon, alt);
    TrafficMesh mesh;
    build_traffic_mesh(g.world, tod, mesh);
    upload_traffic(g, mesh);
}

void add_tri(MeshGpu& g, const osg::Vec3& a, const osg::Vec3& b, const osg::Vec3& c,
             const osg::Vec4& col) {
    g.verts->push_back(a);
    g.verts->push_back(b);
    g.verts->push_back(c);
    g.cols->push_back(col);
    g.cols->push_back(col);
    g.cols->push_back(col);
}

osg::Vec3 enu_point(const geo::Ecef& o, const geo::Ecef& e, const geo::Ecef& n, const geo::Ecef& u,
                    double east, double north, double up) {
    return osg::Vec3(static_cast<float>(o.x + e.x * east + n.x * north + u.x * up),
                     static_cast<float>(o.y + e.y * east + n.y * north + u.y * up),
                     static_cast<float>(o.z + e.z * east + n.z * north + u.z * up));
}

void add_disc(MeshGpu& g, double lat, double lon, double alt_m, float radius_m, int nseg,
              const osg::Vec4& col) {
    const geo::Ecef o = geo::lla_to_ecef(lat, lon, alt_m);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes(lat, lon, e, n, u);
    const osg::Vec3 ctr = enu_point(o, e, n, u, 0, 0, 0);
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i < nseg; ++i) {
        const float a0 = 2.f * kPi * static_cast<float>(i) / nseg;
        const float a1 = 2.f * kPi * static_cast<float>(i + 1) / nseg;
        add_tri(g, ctr,
                enu_point(o, e, n, u, radius_m * std::cos(a0), radius_m * std::sin(a0), 0),
                enu_point(o, e, n, u, radius_m * std::cos(a1), radius_m * std::sin(a1), 0), col);
    }
}

void add_card(MeshGpu& g, double lat, double lon, double alt0_m, double alt1_m, float east,
              float north, float half_w, const osg::Vec4& col) {
    const geo::Ecef o = geo::lla_to_ecef(lat, lon, 0);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes(lat, lon, e, n, u);
    // Tangent to the ring so the card faces the eyepoint.
    float len = std::sqrt(east * east + north * north);
    if (len < 1.f)
        len = 1.f;
    const float te = -north / len * half_w;
    const float tn = east / len * half_w;
    const osg::Vec3 a = enu_point(o, e, n, u, east + te, north + tn, alt0_m);
    const osg::Vec3 b = enu_point(o, e, n, u, east - te, north - tn, alt0_m);
    const osg::Vec3 c = enu_point(o, e, n, u, east - te, north - tn, alt1_m);
    const osg::Vec3 d = enu_point(o, e, n, u, east + te, north + tn, alt1_m);
    add_tri(g, a, b, c, col);
    add_tri(g, a, c, d, col);
}

void layer_alts(const xt4::CloudLayer& L, int which, float& lo, float& hi) {
    lo = L.ceiling_ft;
    hi = L.top_ft;
    if (which == 2 && L.on() && hi <= lo) {
        lo = 25000.f;
        hi = 28000.f;
    }
}

bool eye_in_layer(const HostState& host, const xt4::CloudLayer& L, int which) {
    if (!L.on() || !host.have_eyepoint)
        return false;
    float lo = 0, hi = 0;
    layer_alts(L, which, lo, hi);
    return host.eye.alt_ft >= lo && host.eye.alt_ft <= hi && hi > lo;
}

void update_clouds(MeshGpu& g, const HostState& host, float drift_m) {
    mesh_begin(g);
    if (!host.have_cloud || !host.have_eyepoint) {
        mesh_end(g);
        return;
    }
    const double lat = host.eye.lat_deg;
    const double lon = host.eye.lon_deg;
    const osg::Vec4 cols[3] = {
        osg::Vec4(0.52f, 0.55f, 0.60f, 0.30f),
        osg::Vec4(0.68f, 0.71f, 0.76f, 0.22f),
        osg::Vec4(0.84f, 0.86f, 0.90f, 0.12f),
    };
    for (int li = 0; li < 3; ++li) {
        const auto& L = host.clouds[static_cast<std::size_t>(li)];
        if (!L.on())
            continue;
        float lo_ft = 0, hi_ft = 0;
        layer_alts(L, li, lo_ft, hi_ft);
        if (hi_ft <= lo_ft)
            continue;
        const float lo_m = lo_ft * static_cast<float>(geo::kFtToM);
        const float hi_m = hi_ft * static_cast<float>(geo::kFtToM);
        const float mid_m = 0.5f * (lo_m + hi_m);
        float rad = 9000.f;
        if (L.scud & 1)
            rad += 600.f;
        const float we =
            (L.wind_kt != 0.f ? L.wind_kt * 0.514f : host.env.wind_speed_mph * 0.447f);
        const float wrad = static_cast<float>(
            geo::deg2rad(L.wind_kt != 0.f ? L.wind_dir_deg : host.env.wind_dir_deg));
        const float ox = std::fmod(drift_m * -std::sin(wrad) * we * 0.04f, 4000.f);
        const float oy = std::fmod(drift_m * -std::cos(wrad) * we * 0.04f, 4000.f);
        const double clat = lat + oy / 111320.0;
        const double clon = lon + ox / (111320.0 * std::max(0.2, std::cos(geo::deg2rad(lat))));
        osg::Vec4 col = cols[li];
        if (eye_in_layer(host, L, li))
            col.a() *= 0.55f;
        add_disc(g, clat, clon, lo_m, rad, 14, col);
        add_disc(g, clat, clon, mid_m, rad * 0.92f, 12, col);
        add_disc(g, clat, clon, hi_m, rad * 0.85f, 12, col);
        constexpr int kCards = 8;
        for (int i = 0; i < kCards; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kCards;
            const float r = rad * 0.55f;
            add_card(g, clat, clon, lo_m, hi_m, r * std::cos(a), r * std::sin(a), 1400.f,
                     osg::Vec4(col.r(), col.g(), col.b(), col.a() * 0.7f));
        }
    }
    mesh_end(g);
}

void update_storms(MeshGpu& g, const HostState& host, float flash) {
    mesh_begin(g);
    for (const auto& cell : host.storms) {
        if (!cell.cfg.active || !cell.have_pose)
            continue;
        const double lat = cell.pose.lat_deg;
        const double lon = cell.pose.lon_deg;
        const float rad = storm_radius_m(cell);
        const float base_m = static_cast<float>(std::max(200.0, cell.pose.alt_ft * geo::kFtToM - 800.0));
        const float top_m = base_m + 7000.f;
        osg::Vec4 col(0.16f + 0.45f * flash, 0.17f + 0.45f * flash, 0.20f + 0.50f * flash,
                      0.42f + 0.25f * flash);
        add_disc(g, lat, lon, base_m, rad, 12, col);
        add_disc(g, lat, lon, 0.4f * base_m + 0.6f * top_m, rad * 0.75f, 10, col);
        add_disc(g, lat, lon, top_m, rad * 0.45f, 10, col);
        for (int i = 0; i < 6; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 6.f;
            add_card(g, lat, lon, base_m, top_m, rad * 0.4f * std::cos(a),
                     rad * 0.4f * std::sin(a), rad * 0.35f, col);
        }
    }
    mesh_end(g);
}

void apply_light_steps(LightGpu& g, const HostState& host) {
    if (!g.colors || g.colors->empty())
        return;
    const float tod = host.have_env ? effective_tod_hours(host.env.tod_hours, host.env.tod_select)
                                    : 14.5f;
    const float night = lights::night_weight(tod);
    const float hat = host.have_hat ? host.hat_ft : std::max(0.f, static_cast<float>(host.eye.alt_ft));
    const float psz_near = lights::point_size_px(tod, hat, 0.f);
    const float psz_far = lights::base_size_px(tod);
    if (g.geom) {
        auto* ss = g.geom->getOrCreateStateSet();
        if (auto* pt = dynamic_cast<osg::Point*>(ss->getAttribute(osg::StateAttribute::POINT)))
            pt->setSize(psz_near);
        if (auto* u = ss->getUniform("uPointSize"))
            u->set(psz_near);
        if (auto* u = ss->getUniform("uPointSizeFar"))
            u->set(psz_far);
        if (auto* u = ss->getUniform("uSizeNearM"))
            u->set(lights::kSizeNearM);
    }
    const double t_s = host.last_rx_ns > 0 ? static_cast<double>(host.last_rx_ns) * 1e-9 : 0.0;
    geo::Ecef eye{};
    const bool have_eye = host.have_eyepoint;
    if (have_eye)
        eye = geo::lla_to_ecef(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM);
    for (std::size_t i = 0; i < g.colors->size(); ++i) {
        int step = 3;
        if (i < g.switches.size()) {
            const int sw = g.switches[i];
            if (sw >= 0 && sw < xt4::kMaxLightSwitches)
                step = host.light_steps[static_cast<std::size_t>(sw)];
        }
        const LightKind kind = i < g.kinds.size() ? g.kinds[i] : LightKind::Edge;
        float k = lights::intensity(kind, step) * lights::tod_visibility(kind, tod);
        float range = 0.f;
        if (have_eye && i < g.lat.size()) {
            const geo::Ecef p = geo::lla_to_ecef(g.lat[i], g.lon[i], g.alt_m[i]);
            const float dx = static_cast<float>(p.x - eye.x);
            const float dy = static_cast<float>(p.y - eye.y);
            const float dz = static_cast<float>(p.z - eye.z);
            range = std::sqrt(dx * dx + dy * dy + dz * dz);
            k *= lights::range_visibility(kind, range);
        }
        k *= 1.f + 0.85f * night; // night cores must outshine the navy sky
        float sz = lights::point_size_px(tod, hat, range) * lights::type_size_boost(kind);
        sz = lights::day_als_size_px(kind, night, sz);
        k *= lights::day_als_intensity(kind, night);
        if (kind == LightKind::Vasi) {
            sz = std::min(8.f, std::max(5.8f, sz));
            k = std::max(k, 1.15f); // 2W2R must bloom a little in daylight
        }
        if (night > 0.20f && (kind == LightKind::Edge || kind == LightKind::Centerline ||
                              kind == LightKind::Threshold)) {
            k *= 1.f + 0.28f * night; // HIRL / CL / TDZ over the asphalt sheen
        }
        // HAT boost is 12× at 0 ft. Uncapped, 0.15 nm night ALS became
        // 66 px spheres. Keep every lamp at the 8 px disc budget.
        sz = std::min(8.f, sz);
        if (g.sizes && i < g.sizes->size())
            (*g.sizes)[static_cast<unsigned>(i)] = osg::Vec2(sz, 0.f);
        if (kind == LightKind::Beacon && !lights::beacon_on(t_s))
            k = 0.f;
        if ((kind == LightKind::Strobe || kind == LightKind::Reil) && !lights::strobe_on(t_s))
            k = 0.f;
        const int seq = i < g.seq.size() ? g.seq[i] : -1;
        if (kind == LightKind::Flasher && seq >= 0 && !lights::sfl_on(t_s, seq))
            k = 0.f;
        osg::Vec4 b = g.base[i];
        {
            float rr = b.r(), gg = b.g(), bb = b.b();
            lights::day_als_rgb(kind, night, rr, gg, bb);
            b = osg::Vec4(rr, gg, bb, b.a());
        }
        if (have_eye && i < g.rev.size() && g.rev[i].a() >= 0.f) {
            double brg = 0;
            geo::inverse_wgs84(host.eye.lat_deg, host.eye.lon_deg, g.lat[i], g.lon[i], &brg);
            const double rel = geo::wrap180(brg - g.color_hdg[i]);
            if (std::fabs(rel) > 90.0)
                b = osg::Vec4(g.rev[i].r(), g.rev[i].g(), g.rev[i].b(), 1.f);
        }
        if (kind == LightKind::Vasi && i < g.papi_box.size() && g.papi_box[i] >= 0 &&
            host.have_eyepoint) {
            if (i < g.papi_hdg.size() &&
                !lights::papi_in_coverage(host.eye.lat_deg, host.eye.lon_deg, g.lat[i], g.lon[i],
                                          g.papi_hdg[i], g.papi_rot[i], g.papi_ul[i],
                                          g.papi_ur[i])) {
                k = 0.f;
            }
            const float ang = lights::papi_eye_angle_deg(
                host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM, g.lat[i],
                g.lon[i], g.alt_m[i]);
            const bool white = lights::papi_box_white(ang, g.papi_box[i], g.papi_gs[i]);
            b = white ? osg::Vec4(1.f, 1.f, 0.92f, 1.f) : osg::Vec4(1.f, 0.12f, 0.08f, 1.f);
        }
        (*g.colors)[static_cast<unsigned>(i)] = osg::Vec4(b.r() * k, b.g() * k, b.b() * k, k);
    }
    g.colors->dirty();
    if (g.sizes)
        g.sizes->dirty();
    g.geom->dirtyBound();
}

CacheRegion load_or_synth_cache(const std::string& dir, int max_mesh_n) {
    if (!dir.empty()) {
        try {
            auto r = load_cache_mesh(dir, max_mesh_n);
            S376_LOG_INFO("render", "loaded DEM cache %s  %dx%d (subsampled)", dir.c_str(), r.dem.w,
                          r.dem.h);
            return r;
        } catch (const std::exception& ex) {
            S376_LOG_WARN("render", "cache %s: %s — using synth KSEA DEM", dir.c_str(), ex.what());
        }
    }
    auto r = generate_ksea_cache();
    S376_LOG_INFO("render", "synth KSEA DEM %dx%d (run s376-cache for a packed region)", r.dem.w,
                  r.dem.h);
    return r;
}

void apply_view_camera(osg::Camera* cam, const ChannelConfig& ch, const HostState& host) {
    if (!cam || !host.have_eyepoint)
        return;
    const auto v = geo::make_channel_view(
        host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM,
        host.eye.true_heading_deg(), host.eye.pitch_deg, host.eye.roll_deg, ch.heading_offset_deg,
        ch.pitch_offset_deg, ch.roll_offset_deg, ch.hfov_deg, ch.vfov_deg, ch.width, ch.height,
        ch.eye_fwd_m, ch.eye_right_m, ch.eye_up_m);
    const osg::Vec3d epos = ecef_to_osg(v.eye);
    const osg::Vec3d center =
        epos + osg::Vec3d(v.forward.x, v.forward.y, v.forward.z) * 100.0;
    const osg::Vec3d upv(v.up.x, v.up.y, v.up.z);
    cam->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    cam->setProjectionMatrixAsPerspective(v.vfov_deg, v.aspect, v.near_m, v.far_m);
    cam->setViewMatrixAsLookAt(epos, center, upv);
    // flip_vertical is a projector framebuffer mirror, never a camera
    // orientation and never applied to FBO / wrap-strip shots.
}

// Final projector-output Y mirror. Used only on a single live channel
// window (the hangar Left PC). Never on FBO shots or the wrap strip.
struct OutputMirrorCallback : public osg::Camera::DrawCallback {
    mutable std::vector<unsigned char> pix;
    mutable std::vector<unsigned char> row;
    void operator()(osg::RenderInfo& ri) const override {
        osg::Camera* cam = ri.getCurrentCamera();
        if (!cam || !cam->getViewport())
            return;
        const osg::Viewport* vp = cam->getViewport();
        const int w = static_cast<int>(vp->width());
        const int h = static_cast<int>(vp->height());
        if (w < 2 || h < 2)
            return;
        const int x = static_cast<int>(vp->x());
        const int y = static_cast<int>(vp->y());
        pix.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
        row.resize(static_cast<std::size_t>(w) * 4u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
        for (int r = 0; r < h / 2; ++r) {
            unsigned char* a = pix.data() + static_cast<std::size_t>(r) * w * 4;
            unsigned char* b = pix.data() + static_cast<std::size_t>(h - 1 - r) * w * 4;
            std::memcpy(row.data(), a, row.size());
            std::memcpy(a, b, row.size());
            std::memcpy(b, row.data(), row.size());
        }
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, w, 0, h, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glRasterPos2i(0, 0);
        glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
};

// LDR RGBA8 FBO readback. PACK_ALIGNMENT 1 so a non-multiple-of-4 width
// cannot shift rows into stripe noise. Image is GL bottom-up.
struct PackReadback : public osg::Camera::DrawCallback {
    osg::ref_ptr<osg::Image> img;
    mutable bool ok = false;
    PackReadback(int w, int h) {
        img = new osg::Image;
        img->allocateImage(std::max(1, w), std::max(1, h), 1, GL_RGBA, GL_UNSIGNED_BYTE);
        img->setPacking(1);
        img->setOrigin(osg::Image::BOTTOM_LEFT);
    }
    void operator()(osg::RenderInfo&) const override {
        if (!img)
            return;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        img->readPixels(0, 0, img->s(), img->t(), GL_RGBA, GL_UNSIGNED_BYTE);
        ok = img->data() != nullptr;
    }
};

RgbImage rgb_from_fbo(const osg::Image& img) {
    RgbImage out;
    out.w = img.s();
    out.h = img.t();
    if (out.w < 1 || out.h < 1 || !img.data())
        return out;
    const int n = out.w * out.h;
    out.rgb.resize(static_cast<std::size_t>(n * 3));
    if (img.getPixelFormat() == GL_RGBA) {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(n * 4));
        std::memcpy(rgba.data(), img.data(), static_cast<std::size_t>(n * 4));
        flip_vert(rgba.data(), out.w, out.h, 4);
        rgba_to_rgb(rgba.data(), n, out.rgb.data());
    } else if (img.getPixelFormat() == GL_RGB) {
        std::memcpy(out.rgb.data(), img.data(), static_cast<std::size_t>(n * 3));
        flip_vert(out.rgb.data(), out.w, out.h, 3);
    }
    return out;
}

const char* kBlitVS = R"(
#version 120
varying vec2 vUV;
void main() {
    vUV = gl_MultiTexCoord0.xy;
    gl_Position = ftransform();
}
)";

// Integer pixel coords match make_gradient_rgb after the vertical flip.
const char* kGradFS = R"(
#version 120
uniform float uW;
uniform float uH;
void main() {
    float x = floor(gl_FragCoord.x);
    float y = floor(gl_FragCoord.y);
    float img_y = uH - 1.0 - y;
    float den_x = max(uW - 1.0, 1.0);
    float den_y = max(uH - 1.0, 1.0);
    gl_FragColor = vec4((x * 255.0 / den_x) / 255.0,
                        (img_y * 255.0 / den_y) / 255.0,
                        128.0 / 255.0, 1.0);
}
)";

osg::Geode* make_gradient_geode(int w, int h) {
    osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
    v->push_back(osg::Vec3(0, 0, 0));
    v->push_back(osg::Vec3(static_cast<float>(w), 0, 0));
    v->push_back(osg::Vec3(static_cast<float>(w), static_cast<float>(h), 0));
    v->push_back(osg::Vec3(0, static_cast<float>(h), 0));
    g->setVertexArray(v);
    osg::ref_ptr<osg::Vec2Array> t = new osg::Vec2Array;
    t->push_back(osg::Vec2(0, 0));
    t->push_back(osg::Vec2(1, 0));
    t->push_back(osg::Vec2(1, 1));
    t->push_back(osg::Vec2(0, 1));
    g->setTexCoordArray(0, t);
    g->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, 4));
    osg::ref_ptr<osg::Program> prog = new osg::Program;
    prog->addShader(new osg::Shader(osg::Shader::VERTEX, kBlitVS));
    prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, kGradFS));
    auto* ss = g->getOrCreateStateSet();
    ss->setAttributeAndModes(prog, osg::StateAttribute::ON);
    ss->addUniform(new osg::Uniform("uW", static_cast<float>(w)));
    ss->addUniform(new osg::Uniform("uH", static_cast<float>(h)));
    ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    auto* geode = new osg::Geode;
    geode->addDrawable(g);
    return geode;
}

osg::ref_ptr<osg::GraphicsContext> make_gl_context(int x, int y, int w, int h,
                                                   bool offscreen = false) {
    auto try_one = [&](bool pbuffer) -> osg::ref_ptr<osg::GraphicsContext> {
        osg::ref_ptr<osg::GraphicsContext::Traits> tr =
            new osg::GraphicsContext::Traits(osg::DisplaySettings::instance());
        tr->readDISPLAY();
        tr->setUndefinedScreenDetailsToDefaultScreen();
        tr->x = x;
        tr->y = y;
        // If a pbuffer is unavailable, park the fallback window off the
        // desktop so hangar projectors do not flash a review frame.
        if (offscreen && !pbuffer) {
            tr->x = -32000;
            tr->y = -32000;
        }
        tr->width = std::max(8, w);
        tr->height = std::max(8, h);
        tr->windowDecoration = false; // projectors / screenshots: no chrome, exact WxH
        tr->doubleBuffer = !pbuffer;
        tr->samples = 0;
        tr->sampleBuffers = 0;
        tr->vsync = false;
        tr->pbuffer = pbuffer;
        // 3.3 + COMPAT mask: Mesa/CI get a legal profile (unset version can
        // be CORE and then FF leftover is `invalid enumerant`). NVIDIA 595
        // with a mask stays compat; 3.3 without a mask is Core on that driver.
        if (prefer_rgb_textures() || !have_nvidia_device()) {
            tr->glContextVersion = "3.3";
            tr->glContextProfileMask = kGlCompatProfileBit;
        }
        osg::ref_ptr<osg::GraphicsContext> gc =
            osg::GraphicsContext::createGraphicsContext(tr.get());
        if (!gc || !gc->valid())
            return nullptr;
        if (!gc->isRealized())
            gc->realize();
        S376_LOG_INFO("render", "GL %s %s  %dx%d  display=%s", pbuffer ? "pbuffer" : "window",
                      gc->isRealized() ? "ok" : "unrealized", tr->width, tr->height,
                      tr->displayName().c_str());
        return gc;
    };
    if (offscreen) {
        if (auto gc = try_one(true))
            return gc;
        return try_one(false);
    }
    if (auto gc = try_one(false))
        return gc;
    return try_one(true);
}

struct KeepShotPose : public osg::View::Slave::UpdateSlaveCallback {
    ChannelConfig ch;
    const HostState* host = nullptr;
    int w = 0, h = 0;
    KeepShotPose(const ChannelConfig& c, const HostState* h, int a, int b)
        : ch(c), host(h), w(a), h(b) {}
    void updateSlave(osg::View&, osg::View::Slave& s) override {
        if (!s._camera.valid())
            return;
        if (host)
            apply_view_camera(s._camera.get(), ch, *host);
        s._camera->setViewport(0, 0, w, h);
    }
};

osg::ref_ptr<osg::Camera> make_shot_fbo(int w, int h, const osg::Vec4& clear,
                                        PackReadback* cb) {
    osg::ref_ptr<osg::Camera> rtt = new osg::Camera;
    rtt->setName("s376-shot-fbo");
    rtt->setViewport(0, 0, w, h);
    rtt->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    rtt->setClearColor(clear);
    rtt->setRenderOrder(osg::Camera::PRE_RENDER, 100);
    rtt->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    rtt->setImplicitBufferAttachmentRenderMask(0);
    rtt->setImplicitBufferAttachmentResolveMask(0);
    osg::ref_ptr<osg::Texture2D> color = new osg::Texture2D;
    color->setTextureSize(w, h);
    color->setInternalFormat(GL_RGBA8);
    color->setSourceFormat(GL_RGBA);
    color->setSourceType(GL_UNSIGNED_BYTE);
    color->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
    color->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
    color->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    color->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    color->setResizeNonPowerOfTwoHint(false);
    // samples=0: resolved LDR, not the MSAA window buffer.
    rtt->attach(osg::Camera::COLOR_BUFFER0, color.get(), 0, 0, false, 0, 0);
    rtt->attach(osg::Camera::DEPTH_BUFFER, GL_DEPTH_COMPONENT24);
    rtt->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    rtt->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    rtt->setInheritanceMask(osg::CullSettings::ALL_VARIABLES &
                            ~osg::CullSettings::COMPUTE_NEAR_FAR_MODE);
    struct FboPreClear : public osg::Camera::DrawCallback {
        int w = 0, h = 0;
        osg::Vec4 clear;
        FboPreClear(int ww, int hh, const osg::Vec4& c) : w(ww), h(hh), clear(c) {}
        void operator()(osg::RenderInfo&) const override {
            glViewport(0, 0, w, h);
            glScissor(0, 0, w, h);
            glEnable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            glClearColor(clear.r(), clear.g(), clear.b(), clear.a());
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
    };
    rtt->setPreDrawCallback(new FboPreClear(w, h, clear));
    rtt->setPostDrawCallback(cb);
    return rtt;
}

osg::Camera* make_instructor_hud(int w, int h, osgText::Text*& version, osgText::Text*& toast,
                                 osgText::Text*& era, osgText::Text*& missiles) {
    auto* cam = new osg::Camera;
    cam->setProjectionMatrix(osg::Matrix::ortho2D(0, w, 0, h));
    cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    cam->setViewMatrix(osg::Matrix::identity());
    cam->setClearMask(GL_DEPTH_BUFFER_BIT);
    cam->setRenderOrder(osg::Camera::POST_RENDER);
    cam->setAllowEventFocus(false);
    auto* geode = new osg::Geode;
    auto mk = [&](float x, float y, float sz) {
        auto* t = new osgText::Text;
        t->setPosition(osg::Vec3(x, y, 0));
        t->setCharacterSize(sz);
        t->setColor(osg::Vec4(0.90f, 0.92f, 0.94f, 0.92f));
        t->setAxisAlignment(osgText::Text::SCREEN);
        t->setBackdropType(osgText::Text::OUTLINE);
        t->setBackdropColor(osg::Vec4(0, 0, 0, 0.65f));
        geode->addDrawable(t);
        return t;
    };
    version = mk(12.f, 14.f, 16.f);
    version->setText(std::string("s376-visual ") + s376::version());
    toast = mk(12.f, 112.f, 18.f);
    toast->setText("");
    era = mk(12.f, 46.f, 16.f);
    era->setText("");
    missiles = mk(12.f, 78.f, 16.f);
    missiles->setText("");
    cam->addChild(geode);
    return cam;
}

class OsgRenderer final : public Renderer {
  public:
    bool on_hud_click(float x, float y);
    bool begin(const AppConfig& cfg, const std::vector<ChannelConfig>& channels) override {
        cfg_ = cfg;
        channels_ = channels;
        if (prefer_rgb_textures()) {
            cfg_.render.ssao = false;
            cfg_.render.msaa = 0;
            if (cfg_.render.aniso < 16)
                cfg_.render.aniso = 16;
        }
        if (channels_.empty()) {
            S376_LOG_ERROR("render", "no channels to open");
            return false;
        }

        viewer_ = std::make_unique<osgViewer::CompositeViewer>();
        viewer_->setThreadingModel(osgViewer::ViewerBase::SingleThreaded);
        viewer_->setKeyEventSetsDone(osgGA::GUIEventAdapter::KEY_Escape);
#ifdef __linux__
        const bool linux_host = true;
#else
        const bool linux_host = false;
#endif
        const GlDisplayPlan plan = plan_gl_display(linux_host, cfg.render.gl_debug);
        gl_check_errors_ = plan.check_errors;
        if (plan.check_errors)
            osg::setNotifyLevel(osg::INFO);
        // Linux: leave the version unset so NVIDIA hands back a 4.6
        // compatibility (legacy) context. setGLContextVersion("3.3") with
        // no profile mask is CORE on NVIDIA 595, and apt OSG's FF path
        // then floods "invalid enumerant". Elsewhere request 3.3 +
        // COMPATIBILITY_PROFILE_BIT. Scene StateSets use shaders either way.
        if (osg::ref_ptr<osg::DisplaySettings> ds = osg::DisplaySettings::instance()) {
            if (prefer_rgb_textures()) {
                ds->setGLContextVersion("3.3");
                ds->setGLContextProfileMask(kGlCompatProfileBit);
            } else if (!plan.version.empty()) {
                ds->setGLContextVersion(plan.version);
                if (plan.profile_mask != 0)
                    ds->setGLContextProfileMask(plan.profile_mask);
            }
            ds->setNumMultiSamples(std::max(0, std::min(cfg.render.msaa, 8)));
        }

        osg::ref_ptr<osg::Group> root = new osg::Group;
        casters_ = new osg::Group;
        root->addChild(casters_);
        if (cfg.render.atmosphere) {
            sky_xf_ = make_sky_dome();
            if (sky_xf_) {
                no_shadow(sky_xf_.get());
                root->addChild(sky_xf_.get());
            }
        }
#ifdef S376_HAS_OSGEARTH
        osg::ref_ptr<osg::Node> earth = osgDB::readRefNodeFile(cfg.render.earth_file);
        if (earth) {
            root->addChild(earth);
            used_earth_ = true;
            S376_LOG_INFO("render", "loaded osgEarth map %s", cfg.render.earth_file.c_str());
        } else {
            S376_LOG_WARN("render", "could not load %s; using procedural globe",
                          cfg.render.earth_file.c_str());
        }
#endif
        if (!cfg.render.cache_dir.empty())
            (void)geo::load_shoreline_bin(cfg.render.cache_dir + "/shoreline.bin");
        osg::ref_ptr<osg::Node> globe;
        if (!used_earth_)
            globe = make_procedural_globe(static_cast<float>(std::max(16, cfg.render.aniso)));
        if (!used_earth_) {
            const std::string dir = cfg.render.cache_dir;
            tiles_root_ = new osg::Group;
            casters_->addChild(tiles_root_);
            tile_index_ = scan_cache_tiles(dir);
            tiles_ready_ = tile_index_.empty();
            ram_budget_ = static_cast<std::uint64_t>(std::max(256, cfg.render.ram_mb)) * 1024ull *
                          1024ull;
            vram_budget_ = static_cast<std::uint64_t>(std::max(256, cfg.render.vram_mb)) * 1024ull *
                           1024ull;
            mesh_n_ = std::max(16, std::min(192, cfg.render.mesh_n));
            if (tile_index_.empty()) {
                if (dir.empty())
                    S376_LOG_WARN("render",
                                  "cache_dir empty — synth KSEA DEM (set --cache-dir or cache/"
                                  "<ICAO> for --at)");
                const CacheRegion cache = load_or_synth_cache(dir, mesh_n_);
                if (auto* patch = make_dem_patch(cache, static_cast<float>(cfg.render.aniso))) {
                    tiles_root_->addChild(patch);
                    S376_LOG_INFO("render", "draped %s DEM  %.3f,%.3f .. %.3f,%.3f  mesh %dx%d",
                                  cache.name.empty() ? "region" : cache.name.c_str(), cache.dem.west,
                                  cache.dem.south, cache.dem.east, cache.dem.north, cache.dem.w,
                                  cache.dem.h);
                }
            } else {
                S376_LOG_INFO("render",
                              "cache %s  %zu tiles  ram budget %d MiB  vram %d MiB  mesh %d",
                              dir.c_str(), tile_index_.size(), cfg.render.ram_mb, cfg.render.vram_mb,
                              mesh_n_);
                page_tiles(true); // coarse lod first; lod0 pages in on frame()
            }
        }
        if (globe) {
            no_shadow(globe.get());
            if (auto* ss = globe->getOrCreateStateSet())
                ss->setRenderBinDetails(-2, "RenderBin");
            casters_->addChild(globe);
            if (!tile_index_.empty())
                S376_LOG_INFO("render",
                              "globe under packed tiles — lod1/lod2 page out to %.0f nm",
                              kFarFieldNm);
        }
        if (!used_earth_) {
            const float an = static_cast<float>(cfg.render.aniso);
            // Always drape a KJFK metro disc. Packed lod0 is still paging the
            // 04R/13L 1 m strip; without this disc the bay is globe hexes.
            if (auto* g = make_local_ground(40.639925, -73.778694, 18.0, an)) {
                if (auto* ss = g->getOrCreateStateSet())
                    ss->setRenderBinDetails(-1, "RenderBin");
                casters_->addChild(g);
                S376_LOG_INFO("render", "KJFK local ground  18 km (under missing lod0)");
            }
            if (tile_index_.empty()) {
                for (const auto& apt : all_public_airports()) {
                    if (apt.icao == "KJFK")
                        continue;
                    if (auto* g = make_local_ground(apt.arp_lat, apt.arp_lon, 40.0, an)) {
                        casters_->addChild(g);
                        S376_LOG_INFO("render", "%s local ground  40 km", apt.icao.c_str());
                    }
                }
            }
        }
        {
            Airport merged;
            for (Airport apt : all_public_airports()) {
                const int osm_n = try_load_osm(apt, cfg.render.cache_dir);
                if (osm_n > 0)
                    S376_LOG_INFO("render",
                                  "%s OSM  %d ways  taxi=%zu apron=%zu bldg=%zu road=%zu (osm.bin)",
                                  apt.icao.c_str(), osm_n, apt.taxiways.size(), apt.aprons.size(),
                                  apt.buildings.size(), apt.roads.size());
                else if (!cfg.render.cache_dir.empty() &&
                         std::filesystem::exists(cfg.render.cache_dir + "/osm.json") &&
                         apt.icao.size() >= 3 &&
                         cfg.render.cache_dir.find(apt.icao) != std::string::npos)
                    S376_LOG_WARN("render",
                                  "%s has osm.json (%.1f MiB) — not loaded. "
                                  "s376-cache bake-osm %s",
                                  apt.icao.c_str(),
                                  std::filesystem::file_size(cfg.render.cache_dir + "/osm.json") /
                                      (1024.0 * 1024.0),
                                  cfg.render.cache_dir.c_str());
                generate_lights(apt);
                apply_skyline_era(apt.buildings, active_skyline_era());
                if (auto* mesh = make_airport_geode(build_airport_mesh(
                        apt, cfg.render.buildings, cfg.render.markings))) {
                    casters_->addChild(mesh);
                    S376_LOG_INFO("render", "%s mesh  %zu runways  %zu lights  %zu bldgs  %s",
                                  apt.icao.c_str(), apt.runways.size(), apt.lights.size(),
                                  apt.buildings.size(), apt.attribution.c_str());
                }
                merged.lights.insert(merged.lights.end(), apt.lights.begin(), apt.lights.end());
            }
            lights_gpu_ = make_light_gpu(merged);
            if (lights_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(lights_gpu_.geom);
                no_shadow(geode);
                root->addChild(geode);
            }
            precip_gpu_ = make_precip_gpu();
            if (precip_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(precip_gpu_.geom);
                root->addChild(geode);
            }
            missile_gpu_ = make_precip_gpu();
            if (missile_gpu_.geom) {
                attach_points(missile_gpu_.geom->getOrCreateStateSet(), 6.5f);
                auto* geode = new osg::Geode;
                geode->addDrawable(missile_gpu_.geom);
                root->addChild(geode);
            }
            cloud_gpu_ = make_mesh_gpu(12);
            storm_gpu_ = make_mesh_gpu(13);
            if (cloud_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(cloud_gpu_.geom);
                root->addChild(geode);
            }
            if (storm_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(storm_gpu_.geom);
                root->addChild(geode);
            }
            traffic_gpu_ = make_traffic_gpu();
            {
                std::vector<Airport> apts = all_public_airports();
                for (auto& apt : apts)
                    try_load_osm(apt, cfg.render.cache_dir);
                scene_apts_ = apts;
                rebuild_session_solids(scene_apts_);
                traffic_gpu_.world = build_traffic(apts, kMaxTrafficCars);
                S376_LOG_INFO("render", "road traffic  %zu roads  %zu cars  (980 Ti batch)",
                              traffic_gpu_.world.roads.size(), traffic_gpu_.world.cars.size());
            }
            if (traffic_gpu_.body) {
                auto* geode = new osg::Geode;
                geode->addDrawable(traffic_gpu_.body);
                if (traffic_gpu_.lamps)
                    geode->addDrawable(traffic_gpu_.lamps);
                root->addChild(geode);
            }
        }
        attach_env_uniforms(root->getOrCreateStateSet(), env_u_);
        if (env_u_.bloom)
            env_u_.bloom->set(cfg.render.bloom ? 1.f : 0.f);
        if (env_u_.filmic)
            env_u_.filmic->set(cfg.render.filmic ? 1.f : 0.f);
        if (env_u_.atmosphere)
            env_u_.atmosphere->set(cfg.render.atmosphere ? 1.f : 0.f);
        if (env_u_.shadows)
            env_u_.shadows->set(cfg.render.shadows ? 1.f : 0.f);
        {
            int nb = 0;
            const auto* budgets = quality_gpu_budgets(nb);
            S376_LOG_INFO("render",
                          "quality budget  %.2f ms / 16.67  preset=%s  shadows=%s  (980 Ti ALU)",
                          quality_gpu_budget_total_ms(cfg.render.shadows), cfg.render.quality.c_str(),
                          cfg.render.shadows ? "on" : "off");
            for (int i = 0; i < nb; ++i) {
                if (std::strcmp(budgets[i].name, "shadows") == 0 && !cfg.render.shadows)
                    continue;
                S376_LOG_INFO("render", "  gpu  %-12s  %s  %.2f ms", budgets[i].name,
                              budgets[i].toggle, budgets[i].gpu_ms);
            }
        }
        if (cfg.render.shadows && casters_) {
            const int csm = cfg.render.quality == "rtx" ? 1024 : 512;
            shadow_tex_ = make_csm_tex(csm);
            shadow_tex_far_ = make_csm_tex(csm);
            shadow_cam_ = make_csm_cam(shadow_tex_.get(), casters_, csm, -20);
            shadow_cam_far_ = make_csm_cam(shadow_tex_far_.get(), casters_, csm, -21);
            root->addChild(shadow_cam_);
            root->addChild(shadow_cam_far_);
            root->getOrCreateStateSet()->setTextureAttributeAndModes(2, shadow_tex_.get(),
                                                                     osg::StateAttribute::ON);
            root->getOrCreateStateSet()->setTextureAttributeAndModes(3, shadow_tex_far_.get(),
                                                                     osg::StateAttribute::ON);
            S376_LOG_INFO("render", "shadows  2-cascade %d+%d CSM (sun ±400 / ±2200 m)", csm, csm);
        }
        if (sky_xf_) {
            struct SkyViewCallback : public osg::NodeCallback {
                ShaderEnv* env = nullptr;
                void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
                    if (env && nv && nv->getVisitorType() == osg::NodeVisitor::CULL_VISITOR) {
                        if (auto* cv = dynamic_cast<osgUtil::CullVisitor*>(nv)) {
                            if (osg::Camera* cam = cv->getCurrentCamera()) {
                                osg::Vec3d eye, ctr, up;
                                cam->getViewMatrixAsLookAt(eye, ctr, up);
                                osg::Vec3d fd = ctr - eye;
                                if (fd.length2() > 1e-12)
                                    fd.normalize();
                                if (up.length2() > 1e-12)
                                    up.normalize();
                                const osg::Vec3 f(static_cast<float>(fd.x()),
                                                  static_cast<float>(fd.y()),
                                                  static_cast<float>(fd.z()));
                                const osg::Vec3 u(static_cast<float>(up.x()),
                                                  static_cast<float>(up.y()),
                                                  static_cast<float>(up.z()));
                                osg::Vec3 r = f ^ u;
                                if (r.length2() > 1e-12f)
                                    r.normalize();
                                else
                                    r = osg::Vec3(1.f, 0.f, 0.f);
                                if (env->fwd)
                                    env->fwd->set(f);
                                if (env->cam_up)
                                    env->cam_up->set(u);
                                if (env->right)
                                    env->right->set(r);
                            }
                        }
                    }
                    traverse(node, nv);
                }
            };
            auto* cb = new SkyViewCallback;
            cb->env = &env_u_;
            sky_xf_->setCullCallback(cb);
        }
        if (env_u_.exposure)
            env_u_.exposure->set(cfg.render.hdr ? std::max(0.2f, cfg.render.exposure) : 1.f);
        if (env_u_.detail)
            env_u_.detail->set(cfg.render.detail_textures ? 1.f : 0.f);
        if (env_u_.ssao)
            env_u_.ssao->set(cfg.render.ssao ? 1.f : 0.f);
        if (env_u_.water)
            env_u_.water->set(cfg.render.water ? 1.f : 0.f);
        skyline_root_ = new osg::Group;
        root->addChild(skyline_root_);
        rebuild_skyline();
        root_ = root;

        int nscr = 1, sw = 1920, sh = 1080;
        if (auto* wsi = osg::GraphicsContext::getWindowingSystemInterface()) {
            nscr = static_cast<int>(std::max(1u, wsi->getNumScreens()));
            osg::GraphicsContext::ScreenSettings ss;
            wsi->getScreenSettings(osg::GraphicsContext::ScreenIdentifier(0), ss);
            if (ss.width > 0)
                sw = ss.width;
            if (ss.height > 0)
                sh = ss.height;
        }
        const auto places = layout_channels(channels_, nscr, sw, sh);

        for (std::size_t i = 0; i < channels_.size(); ++i) {
            const auto& ch = channels_[i];
            WindowPlacement place = places[i];
            for (const auto& p : places) {
                if (p.name == ch.name)
                    place = p;
            }

            osg::ref_ptr<osgViewer::View> view = new osgViewer::View;
            view->setSceneData(root);
            const int win_w = std::max(8, ch.width);
            const int win_h = std::max(8, ch.height);
            // Undecorated exact-size window so the sky quad and the
            // channel viewport are the same pixels (no chrome band).
            osg::ref_ptr<osg::GraphicsContext> gc =
                make_gl_context(place.x, place.y, win_w, win_h, cfg.render.offscreen);
            if (gc) {
                if (auto* cam = view->getCamera()) {
                    cam->setGraphicsContext(gc);
                    cam->setViewport(new osg::Viewport(0, 0, win_w, win_h));
                }
            } else if (!cfg.render.offscreen && place.fullscreen) {
                view->setUpViewOnSingleScreen(static_cast<unsigned>(place.screen));
            } else if (!cfg.render.offscreen) {
                view->setUpViewInWindow(place.x, place.y, win_w, win_h,
                                        static_cast<unsigned>(place.screen));
            }
            if (auto* cam = view->getCamera()) {
                osg::GraphicsContext* have = cam->getGraphicsContext();
                if (!have || !have->valid()) {
                    osg::ref_ptr<osg::GraphicsContext> gc2 =
                        make_gl_context(place.x, place.y, win_w, win_h, cfg.render.offscreen);
                    if (gc2) {
                        cam->setGraphicsContext(gc2);
                        cam->setViewport(new osg::Viewport(0, 0, win_w, win_h));
                    }
                }
                cam->setClearColor(osg::Vec4(0.55f, 0.72f, 0.95f, 1));
                cam->setNearFarRatio(0.0001);
                cam->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
                cam->setViewport(0, 0, win_w, win_h);
                if (auto* gc = cam->getGraphicsContext()) {
                    if (const osg::GraphicsContext::Traits* tr = gc->getTraits()) {
                        if (tr->width >= 8 && tr->height >= 8 &&
                            (tr->width != win_w || tr->height != win_h)) {
                            S376_LOG_WARN("render",
                                          "drawable %dx%d != channel %dx%d — viewport follows "
                                          "drawable (sky band); shots use the channel FBO",
                                          tr->width, tr->height, win_w, win_h);
                            cam->setViewport(0, 0, tr->width, tr->height);
                        }
                    }
                }
                if (channels_.size() == 1 && ch.flip_vertical)
                    cam->setPostDrawCallback(new OutputMirrorCallback);
                if (auto* vp = cam->getViewport()) {
                    place.w = static_cast<int>(vp->width());
                    place.h = static_cast<int>(vp->height());
                }
            }
            viewer_->addView(view);
            views_.push_back(view);
            {
                osgText::Text *ver = nullptr, *toast = nullptr, *era = nullptr,
                              *missiles = nullptr;
                osg::ref_ptr<osg::Camera> hud =
                    make_instructor_hud(place.w, place.h, ver, toast, era, missiles);
                no_shadow(hud.get());
                if (auto* cam = view->getCamera())
                    cam->addChild(hud);
                if ((cfg.render.taa || cfg.render.fxaa) && !prefer_rgb_textures()) {
                    if (auto* cam = view->getCamera()) {
                        auto make_color = [&]() {
                            auto* t = new osg::Texture2D;
                            t->setTextureSize(win_w, win_h);
                            t->setInternalFormat(GL_RGBA8);
                            t->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
                            t->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
                            t->setResizeNonPowerOfTwoHint(false);
                            return t;
                        };
                        auto* color = make_color();
                        auto* depth = new osg::Texture2D;
                        depth->setTextureSize(win_w, win_h);
                        depth->setInternalFormat(GL_DEPTH_COMPONENT);
                        depth->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
                        depth->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
                        depth->setResizeNonPowerOfTwoHint(false);
                        cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
                        cam->attach(osg::Camera::COLOR_BUFFER, color);
                        cam->attach(osg::Camera::DEPTH_BUFFER, depth);
                        osg::ref_ptr<osg::Camera> post;
                        if (cfg.render.taa) {
                            taa_a_ = color;
                            taa_b_ = make_color();
                            taa_scene_cam_ = cam;
                            osg::Uniform* tu = nullptr;
                            osg::StateSet* tss = nullptr;
                            post = make_taa_cam(color, taa_b_.get(), win_w, win_h, tu, tss);
                            taa_u_ = tu;
                            taa_ss_ = tss;
                            S376_LOG_INFO("render", "taa  window %dx%d (history clamp)", win_w,
                                          win_h);
                        } else {
                            post = make_fxaa_cam(color, win_w, win_h);
                            S376_LOG_INFO("render", "fxaa  window %dx%d", win_w, win_h);
                        }
                        post->setGraphicsContext(cam->getGraphicsContext());
                        if (view->addSlave(post.get(), false)) {
                            const unsigned idx = view->findSlaveIndexForCamera(post.get());
                            auto& sl = view->getSlave(idx);
                            if (sl._camera) {
                                sl._camera->setGraphicsContext(cam->getGraphicsContext());
                                sl._camera->setViewport(0, 0, win_w, win_h);
                            }
                        }
                    }
                }
                hud_version_ = ver;
                hud_toast_ = toast;
                hud_era_ = era;
                hud_missiles_ = missiles;
                struct HudHandler : public osgGA::GUIEventHandler {
                    OsgRenderer* ig = nullptr;
                    explicit HudHandler(OsgRenderer* p) : ig(p) {}
                    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override {
                        if (!ig)
                            return false;
                        if (ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN ||
                            ea.getEventType() == osgGA::GUIEventAdapter::KEYUP) {
                            const int want = ig->cfg_.egg.ptt_key.empty()
                                                 ? 'm'
                                                 : static_cast<int>(
                                                       std::tolower(static_cast<unsigned char>(
                                                           ig->cfg_.egg.ptt_key[0])));
                            if (ea.getKey() == want || ea.getKey() == std::toupper(want))
                                ig->ptt_key_down_ =
                                    ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN;
                        }
                        if (ea.getEventType() != osgGA::GUIEventAdapter::PUSH)
                            return false;
                        if (ea.getButton() != osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
                            return false;
                        return ig->on_hud_click(ea.getX(), ea.getY());
                    }
                };
                view->addEventHandler(new HudHandler(this));
            }

            S376_LOG_INFO("render",
                          "channel %s  %dx%d at %d,%d screen %d  fov %.1fx%.1f  hdg-off %.3f%s%s",
                          ch.name.c_str(), place.w, place.h, place.x, place.y, place.screen,
                          ch.hfov_deg, ch.vfov_deg, ch.heading_offset_deg,
                          ch.flip_vertical ? "  flip" : "",
                          channels_.size() == 1 ? " (single-window)" : "");
        }

        viewer_->realize();
        configure_gl_state(*viewer_, gl_check_errors_);
        if (!views_.empty()) {
            if (auto* cam = views_.front()->getCamera()) {
                if (auto* gc = cam->getGraphicsContext()) {
                    gc->makeCurrent();
                    const char* vr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
                    const char* rr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
                    S376_LOG_INFO("render", "GL_VERSION %s  GL_RENDERER %s  gc=%s",
                                  vr ? vr : "?", rr ? rr : "?",
                                  gc->isRealized() ? "realized" : "unrealized");
                } else {
                    S376_LOG_WARN("render", "master camera has no GraphicsContext");
                }
            }
        }
        S376_LOG_INFO("render",
                      "opened %zu channel window(s)  screens=%d  %dx%d  gl %s%s%s",
                      views_.size(), nscr, sw, sh,
                      plan.version.empty() ? "legacy-compat" : plan.version.c_str(),
                      plan.profile_mask ? " +compat-mask" : "",
                      plan.check_errors ? "  gl-debug" : "");
        return true;
    }

    void frame(const HostState& host) override {
        if (!viewer_)
            return;
        if (!stripped_ff_) {
            configure_gl_state(*viewer_, gl_check_errors_);
            stripped_ff_ = true;
        }
        last_host_ = host;
        {
            const auto live = control_hub().live_channels();
            if (!live.empty()) {
                for (auto& ch : channels_) {
                    for (const auto& n : live) {
                        if (n.name == ch.name)
                            ch = n;
                    }
                }
            }
        }
        if (host.have_eyepoint)
            tick_session_view(host.eye.lat_deg, host.eye.lon_deg);
        if (active_skyline_era() != drawn_era_)
            rebuild_skyline();
        refresh_hud();
        page_tiles(false);
        fade_tiles();
        flip_taa();
        for (std::size_t i = 0; i < views_.size() && i < channels_.size(); ++i)
            apply_view_camera(views_[i]->getCamera(), channels_[i], host);
        apply_env(host);
        apply_light_steps(lights_gpu_, host);
        const float dt = host.script_dt >= 0.f ? host.script_dt : (1.f / 60.f);
        cloud_drift_m_ += 8.f * dt;
        update_precip(precip_gpu_, host, dt);
        update_missile_fx(missile_gpu_);
        update_clouds(cloud_gpu_, host, cloud_drift_m_);
        update_storms(storm_gpu_, host, storm_flash_);
        update_traffic(traffic_gpu_, host, dt);
        if (host.script_dt >= 0.f && channels_.size() == 1) {
            const ChannelConfig& ch = channels_.front();
            ensure_shot_fbo(std::max(8, ch.width), std::max(8, ch.height), host);
        }
        const auto t_cpu0 = std::chrono::steady_clock::now();
        begin_gpu_query();
        viewer_->frame();
        end_gpu_query(false);
        last_cpu_ms_ = static_cast<float>(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_cpu0)
                .count());
        if (cfg_.render.auto_drop && last_gpu_ms_ > cfg_.render.gpu_budget_ms) {
            if (apply_quality_autodrop(cfg_.render, last_gpu_ms_, cfg_.render.gpu_budget_ms,
                                       &autodrop_log_)) {
                apply_runtime_toggles();
                S376_LOG_INFO("render", "gtx980ti autodrop  gpu=%.2f ms  budget=%.2f  now=%s",
                              last_gpu_ms_, cfg_.render.gpu_budget_ms, autodrop_log_.c_str());
            }
        }
    }

    float last_gpu_ms() const override { return last_gpu_ms_; }
    float last_cpu_ms() const override { return last_cpu_ms_; }
    std::string autodrop_log() const override { return autodrop_log_; }
    float measure_gpu_ms() override {
        end_gpu_query(true);
        return last_gpu_ms_;
    }
    void set_render_toggles(const RenderConfig& r) override {
        cfg_.render.atmosphere = r.atmosphere;
        cfg_.render.water = r.water;
        cfg_.render.detail_textures = r.detail_textures;
        cfg_.render.ssao = r.ssao;
        cfg_.render.bloom = r.bloom;
        cfg_.render.filmic = r.filmic;
        cfg_.render.shadows = r.shadows;
        cfg_.render.fxaa = r.fxaa;
        cfg_.render.taa = r.taa;
        cfg_.render.clustered = r.clustered;
        cfg_.render.lod_fade = r.lod_fade;
        cfg_.render.auto_drop = r.auto_drop;
        cfg_.render.gpu_budget_ms = r.gpu_budget_ms;
        apply_runtime_toggles();
    }

    bool ptt_key_down() const override { return ptt_key_down_; }
    bool done() const override { return viewer_ && viewer_->done(); }
    void request_stop() override {
        if (viewer_)
            viewer_->setDone(true);
    }
    bool pager_idle() const override {
        if (!tiles_ready_)
            return false;
        if (views_.empty())
            return true;
        for (const auto& view : views_) {
            osgDB::DatabasePager* pager = view->getDatabasePager();
            if (!pager)
                continue;
            if (pager->getFileRequestListSize() != 0 || pager->getDataToCompileListSize() != 0 ||
                pager->getDataToMergeListSize() != 0)
                return false;
        }
        return true;
    }

    bool capture_one(std::size_t i, RgbImage& im) {
        if (i >= channels_.size() || views_.empty())
            return false;
        const ChannelConfig& ch = channels_[i];
        const int w = std::max(8, ch.width);
        const int h = std::max(8, ch.height);
        auto score = [](const RgbImage& img) -> int {
            if (img.w < 2 || img.h < 2 || img.rgb.size() < 12)
                return 0;
            const int idx[] = {0,
                               (img.w - 1) * 3,
                               ((img.h / 2) * img.w + img.w / 2) * 3,
                               ((img.h - 1) * img.w) * 3,
                               ((img.h - 1) * img.w + img.w - 1) * 3};
            int uniq = 0;
            for (int a = 0; a < 5; ++a) {
                bool seen = false;
                for (int b = 0; b < a; ++b) {
                    if (img.rgb[static_cast<std::size_t>(idx[a])] ==
                            img.rgb[static_cast<std::size_t>(idx[b])] &&
                        img.rgb[static_cast<std::size_t>(idx[a] + 1)] ==
                            img.rgb[static_cast<std::size_t>(idx[b] + 1)])
                        seen = true;
                }
                if (!seen)
                    ++uniq;
            }
            return uniq;
        };
        RgbImage fbo, win;
        if (i == 0 && shot_cb_ && shot_cb_->ok && shot_cb_->img && shot_w_ == w && shot_h_ == h)
            fbo = rgb_from_fbo(*shot_cb_->img);
        const std::size_t vi = i < views_.size() ? i : 0;
        if (auto* cam = views_[vi]->getCamera()) {
            const osg::Viewport* vp = cam->getViewport();
            const int rw = vp ? static_cast<int>(vp->width()) : 0;
            const int rh = vp ? static_cast<int>(vp->height()) : 0;
            if (rw >= 8 && rh >= 8) {
                if (auto* gc = cam->getGraphicsContext()) {
                    gc->makeCurrent();
                    osg::ref_ptr<osg::Image> grab = new osg::Image;
                    grab->allocateImage(rw, rh, 1, GL_RGBA, GL_UNSIGNED_BYTE);
                    grab->setPacking(1);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadBuffer(GL_BACK);
                    grab->readPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE);
                    win = rgb_from_fbo(*grab);
                }
            }
        }
        const int fs = score(fbo), ws = score(win);
        S376_LOG_INFO("render",
                      "readback %s  FBO score=%d px0=%d,%d,%d  window score=%d px0=%d,%d,%d  win=%dx%d",
                      ch.name.c_str(), fs, fbo.rgb.size() >= 3 ? fbo.rgb[0] : -1,
                      fbo.rgb.size() >= 3 ? fbo.rgb[1] : -1, fbo.rgb.size() >= 3 ? fbo.rgb[2] : -1,
                      ws, win.rgb.size() >= 3 ? win.rgb[0] : -1,
                      win.rgb.size() >= 3 ? win.rgb[1] : -1, win.rgb.size() >= 3 ? win.rgb[2] : -1,
                      win.w, win.h);
        // Prefer a window read only when it is the channel size. A 1080p
        // drawable with a 1200-row read is the 10% sky band on Mint.
        if (ws >= 2 && win.w == w && win.h == h)
            im = std::move(win);
        else if (fs >= 2 && fbo.w == w && fbo.h == h)
            im = std::move(fbo);
        else if (ws >= 2 && win.w >= 8 && win.h >= 8)
            im = std::move(win);
        if (im.w == w && im.h == h && im.rgb.size() >= static_cast<std::size_t>(w * h * 3))
            return true;
        osg::Vec4 clear(0.35f, 0.50f, 0.72f, 1);
        if (auto* cam = views_.front()->getCamera())
            clear = cam->getClearColor();
        return render_fbo(w, h, clear, root_.get(), false, &ch, &last_host_, im);
    }

    bool capture_rgb(RgbImage& im) override {
        if (channels_.empty() || !viewer_ || views_.empty())
            return false;
        if (channels_.size() == 1)
            return capture_one(0, im);
        std::vector<RgbImage> parts;
        parts.reserve(channels_.size());
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            RgbImage part;
            if (!capture_one(i, part))
                return false;
            parts.push_back(std::move(part));
        }
        im = stitch_horizontal(parts);
        return im.w > 0 && im.h > 0 &&
               im.rgb.size() >= static_cast<std::size_t>(im.w * im.h * 3);
    }

    bool capture(const std::string& path) override {
        if (path.empty())
            return false;
        RgbImage im;
        if (!capture_rgb(im))
            return false;
        std::string ppm, png;
        if (!write_shot_images(path, im, &ppm, &png))
            return false;
        if (channels_.size() > 1) {
            S376_LOG_INFO("render", "shot strip %dx%d  %zu channels  ppm=%s png=%s", im.w, im.h,
                          channels_.size(), ppm.c_str(), png.c_str());
        } else {
            S376_LOG_INFO("render", "shot FBO %dx%d channel %s  ppm=%s png=%s", im.w, im.h,
                          channels_.front().name.c_str(), ppm.c_str(), png.c_str());
        }
        return true;
    }

    bool capture_self_test(const std::string& dir, std::string& err) override {
        if (!self_test_shot_files(dir, err))
            return false;
        if (!viewer_ || views_.empty())
            return true;
        const std::string stem = dir.empty() ? "s376-gl-self" : (dir + "/s376-gl-self");
        const int w = 64;
        const int h = 48;
        const osg::Vec4 solid_clear(40.f / 255.f, 80.f / 255.f, 160.f / 255.f, 1);
        RgbImage solid;
        if (!render_fbo(w, h, solid_clear, nullptr, true, nullptr, nullptr, solid)) {
            err = "GL FBO solid readback failed";
            return false;
        }
        if (!pixels_near_solid(solid, 40, 80, 160, 3)) {
            err = "GL FBO solid pixels mismatch (format/type readback)";
            write_shot_images(stem + "-solid-bad.ppm", solid);
            return false;
        }
        osg::ref_ptr<osg::Geode> grad_node = make_gradient_geode(w, h);
        RgbImage grad;
        if (!render_fbo(w, h, osg::Vec4(0, 0, 0, 1), grad_node.get(), true, nullptr, nullptr, grad)) {
            err = "GL FBO gradient readback failed";
            return false;
        }
        if (!pixels_near_gradient(grad, 3)) {
            err = "GL FBO gradient pixels mismatch (format/type readback)";
            write_shot_images(stem + "-grad-bad.ppm", grad);
            return false;
        }
        if (!write_shot_images(stem + "-solid.ppm", solid) ||
            !write_shot_images(stem + "-grad.ppm", grad)) {
            err = "GL self-test write failed";
            return false;
        }
        err.clear();
        S376_LOG_INFO("render", "GL FBO self-test ok  %dx%d solid+gradient", w, h);
        return true;
    }
    void end() override {
        if (viewer_)
            viewer_->setDone(true);
        detach_shot_fbo();
        views_.clear();
        viewer_.reset();
        root_ = nullptr;
    }
    std::string name() const override { return used_earth_ ? "osgearth" : "osg"; }
    std::size_t view_count() const override { return views_.size(); }

  private:
    bool tile_resident(const std::string& path) const {
        for (const auto& r : resident_) {
            if (r.path == path)
                return true;
        }
        return false;
    }

    void detach_shot_fbo() {
        if (shot_cam_ && !views_.empty()) {
            osgViewer::View* view = views_.front();
            const unsigned idx = view->findSlaveIndexForCamera(shot_cam_.get());
            if (idx < view->getNumSlaves())
                view->removeSlave(idx);
        }
        shot_cam_ = nullptr;
        shot_cb_ = nullptr;
        shot_w_ = 0;
        shot_h_ = 0;
    }

    void ensure_shot_fbo(int w, int h, const HostState& host) {
        if (shot_cam_ && shot_cb_ && shot_w_ == w && shot_h_ == h) {
            apply_view_camera(shot_cam_.get(), channels_.front(), host);
            return;
        }
        detach_shot_fbo();
        if (views_.empty() || channels_.empty() || !root_)
            return;
        osgViewer::View* view = views_.front();
        osg::Camera* master = view->getCamera();
        if (!master || !master->getGraphicsContext())
            return;
        osg::Vec4 clear(0.35f, 0.50f, 0.72f, 1);
        if (master->getClearColor()[3] > 0)
            clear = master->getClearColor();
        shot_cb_ = new PackReadback(w, h);
        shot_cam_ = make_shot_fbo(w, h, clear, shot_cb_.get());
        shot_cam_->setGraphicsContext(master->getGraphicsContext());
        apply_view_camera(shot_cam_.get(), channels_.front(), host);
        if (!view->addSlave(shot_cam_.get(), true)) {
            detach_shot_fbo();
            return;
        }
        const unsigned idx = view->findSlaveIndexForCamera(shot_cam_.get());
        auto& sl = view->getSlave(idx);
        if (sl._camera) {
            sl._camera->setGraphicsContext(master->getGraphicsContext());
            sl._camera->setViewport(0, 0, w, h);
            sl._camera->setInheritanceMask(osg::CullSettings::ALL_VARIABLES &
                                           ~osg::CullSettings::COMPUTE_NEAR_FAR_MODE);
            sl._updateSlaveCallback = new KeepShotPose(channels_.front(), &last_host_, w, h);
        }
        shot_w_ = w;
        shot_h_ = h;
    }

    bool load_one_tile(const CacheTileRef& t) {
        if (!tiles_root_ || tile_resident(t.path))
            return true;
        if (bad_tiles_.count(t.path))
            return false;
        if (!dem_file_complete(t.path + "/elev.s376dem")) {
            bad_tiles_.insert(t.path);
            S376_LOG_WARN("render", "tile %s incomplete — skip (no retry)", t.path.c_str());
            return false;
        }
        try {
            auto cache = load_cache_mesh(t.path, mesh_n_);
            osg::ref_ptr<osg::Node> node = make_dem_patch(cache, static_cast<float>(cfg_.render.aniso));
            if (!node)
                return false;
            if (auto* ss = node->getOrCreateStateSet()) {
                // lod0 wins the overlap; keep lod1/lod2 for the 40 nm far field.
                ss->setRenderBinDetails(t.lod == 0 ? 3 : 1, "RenderBin");
                if (t.lod == 0)
                    ss->setAttributeAndModes(new osg::PolygonOffset(-4.f, -4.f),
                                             osg::StateAttribute::ON);
            }
            // Coarse tiles must not cast the 2500 ft "tile triangle" shadow.
            if (t.lod != 0)
                no_shadow(node.get());
            tiles_root_->addChild(node);
            ResidentTile r;
            r.path = t.path;
            r.node = node;
            r.cpu = estimate_tile_cpu_bytes(t, mesh_n_);
            r.vram = t.dds_bytes;
            r.lod = t.lod;
            r.west = t.west;
            r.south = t.south;
            r.east = t.east;
            r.north = t.north;
            r.fade = (!cfg_.render.lod_fade || last_host_.script_dt >= 0.f || t.lod != 0) ? 1.f
                                                                                         : 0.f;
            r.lod_fade_u = new osg::Uniform("uLodFade", r.fade);
            if (auto* ss = node->getOrCreateStateSet())
                ss->addUniform(r.lod_fade_u);
            resident_cpu_ += r.cpu;
            resident_vram_ += r.vram;
            resident_.push_back(std::move(r));
            S376_LOG_INFO("render", "page in lod%d %s  mesh %dx%d  cpu+vram %llu+%llu KiB", t.lod,
                          t.path.c_str(), cache.dem.w, cache.dem.h,
                          static_cast<unsigned long long>(resident_.back().cpu / 1024),
                          static_cast<unsigned long long>(t.dds_bytes / 1024));
            return true;
        } catch (const std::exception& ex) {
            bad_tiles_.insert(t.path);
            S376_LOG_WARN("render", "tile %s: %s — marked bad", t.path.c_str(), ex.what());
            return false;
        }
    }

    void evict_tile(std::size_t i) {
        if (i >= resident_.size() || !tiles_root_)
            return;
        tiles_root_->removeChild(resident_[i].node);
        resident_cpu_ -= resident_[i].cpu;
        resident_vram_ -= resident_[i].vram;
        resident_.erase(resident_.begin() + static_cast<std::ptrdiff_t>(i));
    }

    void page_tiles(bool coarse_only) {
        if (!tiles_root_ || tile_index_.empty())
            return;
        double elat = 40.64, elon = -73.78;
        double tlat = elat, tlon = elon;
        double hdg_true = 0;
        if (last_host_.have_eyepoint) {
            elat = last_host_.eye.lat_deg;
            elon = last_host_.eye.lon_deg;
            tlat = elat;
            tlon = elon;
            hdg_true = last_host_.eye.true_heading_deg();
            const double alt_m = last_host_.eye.alt_ft * geo::kFtToM;
            const double look_m =
                std::max(400.0, std::min(kFarFieldKm * 1000.0,
                                         alt_m / std::tan(3.0 * geo::kDeg)));
            offset_m(elat, elon, hdg_true, look_m, tlat, tlon);
        }
        std::vector<TilePageWant> want;
        score_tiles_for_view(tile_index_, elat, elon, tlat, tlon, hdg_true,
                             last_host_.have_eyepoint, coarse_only, want);
        int loaded = 0;
        const int kMaxNewPerFrame = coarse_only ? 32 : 16;

        auto evict_farthest_lod0 = [&](double min_km) -> bool {
            int far = -1;
            double far_s = -1;
            for (std::size_t i = 0; i < resident_.size(); ++i) {
                if (resident_[i].lod != 0)
                    continue;
                const double dlat = (0.5 * (resident_[i].south + resident_[i].north) - elat) * 111.32;
                const double dlon = (0.5 * (resident_[i].west + resident_[i].east) - elon) * 111.32 *
                                    std::max(0.2, std::cos(geo::deg2rad(elat)));
                const double km = std::sqrt(dlat * dlat + dlon * dlon);
                if (km > far_s) {
                    far_s = km;
                    far = static_cast<int>(i);
                }
            }
            if (far >= 0 && far_s > min_km) {
                evict_tile(static_cast<std::size_t>(far));
                return true;
            }
            return false;
        };

        auto try_load = [&](const TilePageWant& s) -> bool {
            if (loaded >= kMaxNewPerFrame || !s.t)
                return false;
            if (tile_resident(s.t->path) || bad_tiles_.count(s.t->path))
                return false;
            const auto cpu = estimate_tile_cpu_bytes(*s.t, mesh_n_);
            auto over = [&]() {
                return resident_cpu_ + cpu > ram_budget_ ||
                       resident_vram_ + s.t->dds_bytes > vram_budget_;
            };
            if (over()) {
                // Reserved lod1/lod2 win over leftover lod0. Never evict
                // coarse inside 40 nm to make room for another 1 m tile.
                if (s.reserve_coarse) {
                    while (over() && evict_farthest_lod0(2.0)) {
                    }
                } else if (s.t->lod == 0) {
                    evict_farthest_lod0(s.km + 2.0);
                }
                if (over())
                    return false;
            }
            if (load_one_tile(*s.t)) {
                ++loaded;
                return true;
            }
            return false;
        };

        for (const auto& s : want) {
            if (s.reserve_coarse)
                try_load(s);
        }
        for (const auto& s : want) {
            if (!s.reserve_coarse)
                try_load(s);
        }
        // Only drop a coarse tile when lod0 fully covers it. Overlap-only
        // eviction deleted the 50 km lod2 the moment the airport 1 m
        // tiles loaded — that is Joey's empty far field.
        if (!coarse_only) {
            for (int i = static_cast<int>(resident_.size()) - 1; i >= 0; --i) {
                if (resident_[static_cast<std::size_t>(i)].lod < 1)
                    continue;
                const auto& c = resident_[static_cast<std::size_t>(i)];
                double cover_w = 0, cover_s = 0, cover_e = 0, cover_n = 0;
                bool any = false;
                for (const auto& r : resident_) {
                    if (r.lod != 0)
                        continue;
                    if (r.west >= c.east || r.east <= c.west || r.south >= c.north ||
                        r.north <= c.south)
                        continue;
                    if (!any) {
                        cover_w = r.west;
                        cover_s = r.south;
                        cover_e = r.east;
                        cover_n = r.north;
                        any = true;
                    } else {
                        cover_w = std::min(cover_w, r.west);
                        cover_s = std::min(cover_s, r.south);
                        cover_e = std::max(cover_e, r.east);
                        cover_n = std::max(cover_n, r.north);
                    }
                }
                const double pad = 0.002;
                if (any && cover_w <= c.west + pad && cover_s <= c.south + pad &&
                    cover_e >= c.east - pad && cover_n >= c.north - pad)
                    evict_tile(static_cast<std::size_t>(i));
            }
            int need = 0, have = 0, coarse_need = 0, coarse_have = 0;
            for (const auto& s : want) {
                if (s.reserve_coarse) {
                    ++coarse_need;
                    if (tile_resident(s.t->path))
                        ++coarse_have;
                }
                if (s.t->lod != 0 || s.score > 8.0)
                    continue;
                ++need;
                if (tile_resident(s.t->path))
                    ++have;
                if (need >= 16)
                    break;
            }
            const bool near_ok = (need == 0) || (have >= std::min(need, 8));
            const bool far_ok = (coarse_need == 0) || (coarse_have >= coarse_need);
            tiles_ready_ = near_ok && far_ok;
        }
    }

    bool render_fbo(int w, int h, const osg::Vec4& clear, osg::Node* scene, bool ortho,
                    const ChannelConfig* ch, const HostState* host, RgbImage& out) {
        if (!viewer_ || views_.empty())
            return false;
        osgViewer::View* view = views_.front();
        osg::Camera* master = view->getCamera();
        if (!master || !master->getGraphicsContext())
            return false;
        osg::ref_ptr<osg::Viewport> saved_vp =
            master->getViewport() ? new osg::Viewport(*master->getViewport()) : nullptr;
        master->setViewport(0, 0, w, h);
        osg::ref_ptr<PackReadback> cb = new PackReadback(w, h);
        osg::ref_ptr<osg::Camera> rtt = make_shot_fbo(w, h, clear, cb.get());
        rtt->setGraphicsContext(master->getGraphicsContext());
        if (ortho) {
            rtt->setProjectionMatrixAsOrtho2D(0, w, 0, h);
            rtt->setViewMatrix(osg::Matrix::identity());
        } else if (ch && host) {
            apply_view_camera(rtt.get(), *ch, *host);
        }
        if (scene && scene != view->getSceneData())
            rtt->addChild(scene);
        if (!view->addSlave(rtt.get(), scene == view->getSceneData() || scene == nullptr))
            return false;
        const unsigned idx = view->findSlaveIndexForCamera(rtt.get());
        auto& sl = view->getSlave(idx);
        if (sl._camera) {
            sl._camera->setGraphicsContext(master->getGraphicsContext());
            sl._camera->setViewport(0, 0, w, h);
            sl._camera->setInheritanceMask(osg::CullSettings::ALL_VARIABLES &
                                           ~osg::CullSettings::COMPUTE_NEAR_FAR_MODE);
            sl._updateSlaveCallback =
                new KeepShotPose(ch ? *ch : ChannelConfig{}, host, w, h);
        }
        viewer_->frame();
        view->removeSlave(idx);
        if (saved_vp)
            master->setViewport(saved_vp);
        if (!cb->ok || !cb->img)
            return false;
        out = rgb_from_fbo(*cb->img);
        return out.w == w && out.h == h &&
               out.rgb.size() >= static_cast<std::size_t>(w * h * 3);
    }

    void apply_env(const HostState& host) {
        float vis_ft = host.have_env ? host.env.visible_range_ft : 528000.f;
        if (host.have_fog && host.fog.select >= 0 && host.fog.rvr_ft > 0)
            vis_ft = std::min(vis_ft, host.fog.rvr_ft);
        vis_ft = std::max(200.f, std::min(vis_ft, xt4::kMaxVisibilityFt));
        bool in_cloud = false;
        if (host.have_cloud && host.have_eyepoint) {
            for (int i = 0; i < 3; ++i) {
                const auto& L = host.clouds[static_cast<std::size_t>(i)];
                if (!L.on())
                    continue;
                float lo = 0, hi = 0;
                layer_alts(L, i, lo, hi);
                if (host.eye.alt_ft > hi && L.vis_above_ft > 0)
                    vis_ft = std::min(vis_ft, L.vis_above_ft);
                if (host.eye.alt_ft >= lo && host.eye.alt_ft <= hi && hi > lo)
                    in_cloud = true;
            }
        }
        vis_ft = std::max(200.f, std::min(vis_ft, xt4::kMaxVisibilityFt));
        const float vis_m = vis_ft * static_cast<float>(geo::kFtToM);
        float dens = 1.7f / vis_m;
        if (host.have_env && host.env.precip_type > 0)
            dens *= 1.f + 0.12f * std::min(10, host.env.precip_intensity);
        // Fog height is AGL. Use last HAT sample so we do not treat MSL as AGL.
        if (host.have_fog && host.fog.select >= 0 && host.have_hat &&
            host.hat_ft < host.fog.height_ft + 200.f)
            dens *= 1.35f;
        if (in_cloud)
            dens *= 2.4f;
        if (env_u_.fog_density)
            env_u_.fog_density->set(dens);
        float alt_k = 0.f;
        if (host.have_eyepoint)
            alt_k = std::min(1.f, static_cast<float>(host.eye.alt_ft) / 16000.f);
        float haze = 0.f;
        if (cfg_.render.atmosphere)
            haze = 3.2e-6f + 5.0e-6f * alt_k;
        if (env_u_.haze)
            env_u_.haze->set(haze);
        if (env_u_.city_dir && host.have_eyepoint) {
            // Lower Manhattan (public). Zeroed outside ~80 km so other fields
            // do not pick up a fake NYC glow.
            const double mlat = 40.7128, mlon = -74.0060;
            const double dlat = host.eye.lat_deg - mlat;
            const double dlon = (host.eye.lon_deg - mlon) *
                                std::cos(geo::deg2rad(host.eye.lat_deg));
            const double km = std::sqrt(dlat * dlat + dlon * dlon) * 111.32;
            if (km < 80.0) {
                const geo::Ecef eye = geo::lla_to_ecef(
                    host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM);
                const geo::Ecef city = geo::lla_to_ecef(mlat, mlon, 80.0);
                osg::Vec3 d(static_cast<float>(city.x - eye.x),
                            static_cast<float>(city.y - eye.y),
                            static_cast<float>(city.z - eye.z));
                if (d.length2() > 1e-6f)
                    d.normalize();
                env_u_.city_dir->set(d);
            } else {
                env_u_.city_dir->set(osg::Vec3(0.f, 0.f, 0.f));
            }
        }
        if (env_u_.viewport && !views_.empty()) {
            float vw = 1920.f, vh = 1200.f;
            if (auto* cam = views_.front()->getCamera()) {
                if (const osg::Viewport* vp = cam->getViewport()) {
                    if (vp->width() > 8 && vp->height() > 8) {
                        vw = static_cast<float>(vp->width());
                        vh = static_cast<float>(vp->height());
                    }
                }
            }
            env_u_.viewport->set(osg::Vec2(vw, vh));
        }
        if (env_u_.fog_color)
            env_u_.fog_color->set(osg::Vec3(0.42f + 0.06f * alt_k, 0.60f + 0.06f * alt_k,
                                            0.86f + 0.04f * alt_k));

        const float tod = host.have_env ? effective_tod_hours(host.env.tod_hours, host.env.tod_select)
                                        : 14.5f;
        const int tod_sel = host.have_env ? host.env.tod_select : -1;
        const int month = host.have_env ? host.env.month : 6;
        const int day = host.have_env ? host.env.day : 21;
        const double lat = host.have_eyepoint ? host.eye.lat_deg : 47.46;
        const double lon = host.have_eyepoint ? host.eye.lon_deg : -122.31;
        double el = 0;
        geo::Ecef s;
        if (cfg_.render.solar_astronomy) {
            s = geo::sun_ecef(lat, lon, tod, month, day, &el);
        } else {
            el = visual_sun_elev_deg(host.have_env ? host.env.tod_hours : 14.5f, tod_sel);
            const double az = visual_sun_az_deg(host.have_env ? host.env.tod_hours : 14.5f, tod_sel,
                                                lat);
            s = geo::sun_from_elev_az(lat, lon, el, az);
        }
        if (env_u_.sun_dir)
            env_u_.sun_dir->set(osg::Vec3(static_cast<float>(s.x), static_cast<float>(s.y),
                                          static_cast<float>(s.z)));
        {
            geo::Ecef ee, nn, uu;
            geo::ecef_to_enu_axes(lat, lon, ee, nn, uu);
            if (env_u_.up)
                env_u_.up->set(osg::Vec3(static_cast<float>(uu.x), static_cast<float>(uu.y),
                                         static_cast<float>(uu.z)));
            if (host.have_eyepoint && !channels_.empty()) {
                const ChannelConfig& ch = channels_.front();
                const auto v = geo::make_channel_view(
                    host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM,
                    host.eye.true_heading_deg(), host.eye.pitch_deg, host.eye.roll_deg,
                    ch.heading_offset_deg, ch.pitch_offset_deg, ch.roll_offset_deg, ch.hfov_deg,
                    ch.vfov_deg, ch.width, ch.height, ch.eye_fwd_m, ch.eye_right_m, ch.eye_up_m);
                if (env_u_.fwd)
                    env_u_.fwd->set(osg::Vec3(static_cast<float>(v.forward.x),
                                              static_cast<float>(v.forward.y),
                                              static_cast<float>(v.forward.z)));
                const osg::Vec3 fwd(static_cast<float>(v.forward.x),
                                    static_cast<float>(v.forward.y),
                                    static_cast<float>(v.forward.z));
                const osg::Vec3 upv(static_cast<float>(v.up.x), static_cast<float>(v.up.y),
                                    static_cast<float>(v.up.z));
                osg::Vec3 rgt = fwd ^ upv;
                if (rgt.length2() > 1e-12f)
                    rgt.normalize();
                else
                    rgt = osg::Vec3(1.f, 0.f, 0.f);
                if (env_u_.right)
                    env_u_.right->set(rgt);
                if (env_u_.tan_half_h)
                    env_u_.tan_half_h->set(static_cast<float>(
                        std::tan(geo::deg2rad(std::max(1.0, ch.hfov_deg) * 0.5))));
                if (env_u_.tan_half_v)
                    env_u_.tan_half_v->set(static_cast<float>(
                        std::tan(geo::deg2rad(std::max(1.0, ch.vfov_deg) * 0.5))));
                if (env_u_.cam_up)
                    env_u_.cam_up->set(upv);
            }
        }
        const float dayf = static_cast<float>(std::max(0.0, std::sin(geo::deg2rad(el))));
        const float nightf =
            el <= -8.0 ? 1.f : (el >= 6.0 ? 0.f : static_cast<float>((6.0 - el) / 14.0));
        if (env_u_.fog_color) {
            const float twilight = (1.f - dayf) * dayf * 4.f;
            env_u_.fog_color->set(osg::Vec3(
                0.03f + 0.39f * dayf + 0.22f * twilight + 0.06f * alt_k * (1.f - nightf),
                0.04f + 0.56f * dayf + 0.08f * twilight + 0.06f * alt_k * (1.f - nightf),
                0.07f + 0.79f * dayf + 0.04f * alt_k * (1.f - nightf)));
        }
        float flash = 0.f;
        if (host.have_env && host.env.lightning_trigger != 0) {
            if (host.env.lightning_trigger != last_lightning_)
                flash_left_ = 4;
            last_lightning_ = host.env.lightning_trigger;
        }
        for (int i = 0; i < xt4::kMaxActiveStorms; ++i) {
            const auto& cell = host.storms[static_cast<std::size_t>(i)];
            if (!cell.cfg.active)
                continue;
            if (cell.cfg.lightning > 1 && cell.cfg.lightning != last_storm_bolt_[i]) {
                flash_left_ = 5;
                storm_flash_left_ = 5;
            }
            last_storm_bolt_[i] = cell.cfg.lightning;
            if (cell.cfg.lightning == 1 && (storm_tick_ + i * 17) % 97 == 0) {
                flash_left_ = 3;
                storm_flash_left_ = 3;
            }
        }
        ++storm_tick_;
        if (flash_left_ > 0) {
            flash = 0.65f;
            --flash_left_;
        }
        storm_flash_ = 0.f;
        if (storm_flash_left_ > 0) {
            storm_flash_ = 0.85f;
            --storm_flash_left_;
        }
        if (env_u_.night)
            env_u_.night->set(nightf);
        if (env_u_.time)
            env_u_.time->set(tod * 37.f + 12.f);
        if (env_u_.wetness)
            env_u_.wetness->set(host.have_env ? runway_wetness(host.env.runway_condition) : 0.f);
        if (env_u_.bloom)
            env_u_.bloom->set(cfg_.render.bloom ? 1.f : 0.f);
        if (env_u_.filmic)
            env_u_.filmic->set(cfg_.render.filmic ? 1.f : 0.f);
        if (env_u_.atmosphere)
            env_u_.atmosphere->set(cfg_.render.atmosphere ? 1.f : 0.f);
        if (env_u_.shadows)
            env_u_.shadows->set(cfg_.render.shadows && shadow_cam_ ? 1.f : 0.f);
        if (env_u_.clustered)
            env_u_.clustered->set(cfg_.render.clustered ? 1.f : 0.f);
        if (env_u_.lod_fade)
            env_u_.lod_fade->set(1.f);
        if (env_u_.origin && host.have_eyepoint) {
            const geo::Ecef o =
                geo::lla_to_ecef(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM);
            env_u_.origin->set(osg::Vec3(static_cast<float>(o.x), static_cast<float>(o.y),
                                         static_cast<float>(o.z)));
        }
        if (shadow_cam_ && host.have_eyepoint) {
            const geo::Ecef eye =
                geo::lla_to_ecef(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM);
            osg::Vec3d center(eye.x, eye.y, eye.z);
            osg::Vec3d sd(s.x, s.y, s.z);
            if (sd.length2() > 1e-12) {
                sd.normalize();
                geo::Ecef ee2, nn2, uu2;
                geo::ecef_to_enu_axes(host.eye.lat_deg, host.eye.lon_deg, ee2, nn2, uu2);
                osg::Vec3d upv(uu2.x, uu2.y, uu2.z);
                osg::Matrixf bias(0.5f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f,
                                  0.5f, 0.5f, 0.5f, 1.f);
                auto place = [&](osg::Camera* cam, osg::Uniform* M, double half) {
                    if (!cam)
                        return;
                    osg::Vec3d from = center + sd * (half * 2.2);
                    cam->setViewMatrixAsLookAt(from, center, upv);
                    cam->setProjectionMatrixAsOrtho(-half, half, -half, half, 40.0, half * 8.0);
                    if (M)
                        M->set(osg::Matrixf(cam->getViewMatrix()) *
                               osg::Matrixf(cam->getProjectionMatrix()) * bias);
                };
                place(shadow_cam_.get(), env_u_.shadow_matrix, 400.0);
                place(shadow_cam_far_.get(), env_u_.shadow_matrix_far, 2200.0);
            }
        }
        if (env_u_.light_count && host.have_eyepoint && !lights_gpu_.lat.empty()) {
            const geo::Ecef eye =
                geo::lla_to_ecef(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM);
            struct Cand {
                float d2 = 0;
                std::size_t i = 0;
            };
            std::vector<Cand> cand;
            cand.reserve(lights_gpu_.lat.size());
            for (std::size_t i = 0; i < lights_gpu_.lat.size(); ++i) {
                const geo::Ecef p =
                    geo::lla_to_ecef(lights_gpu_.lat[i], lights_gpu_.lon[i], lights_gpu_.alt_m[i]);
                const float dx = static_cast<float>(p.x - eye.x);
                const float dy = static_cast<float>(p.y - eye.y);
                const float dz = static_cast<float>(p.z - eye.z);
                cand.push_back({dx * dx + dy * dy + dz * dz, i});
            }
            const std::size_t keep = std::min<std::size_t>(32, cand.size());
            if (cand.size() > keep) {
                std::nth_element(cand.begin(), cand.begin() + static_cast<std::ptrdiff_t>(keep),
                                 cand.end(),
                                 [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
            }
            int n = 0;
            const float night_boost = 0.35f + 0.65f * nightf;
            for (std::size_t k = 0; k < keep; ++k) {
                const auto i = cand[k].i;
                const geo::Ecef p =
                    geo::lla_to_ecef(lights_gpu_.lat[i], lights_gpu_.lon[i], lights_gpu_.alt_m[i]);
                const float r = cluster_radius_m(lights_gpu_.kinds[i]);
                const osg::Vec4 base = i < lights_gpu_.base.size() ? lights_gpu_.base[i]
                                                                   : osg::Vec4(1.f, 0.85f, 0.5f, 1.f);
                env_u_.light_pos->setElement(n, osg::Vec4(static_cast<float>(p.x),
                                                          static_cast<float>(p.y),
                                                          static_cast<float>(p.z), r));
                env_u_.light_col->setElement(
                    n, osg::Vec3(base.x(), base.y(), base.z()) * night_boost * 1.8f);
                ++n;
            }
            env_u_.light_count->set(n);
        } else if (env_u_.light_count) {
            env_u_.light_count->set(0);
        }
        const float dk = 0.15f + 0.85f * dayf + flash;
        if (env_u_.diffuse)
            env_u_.diffuse->set(osg::Vec3(1.0f * dk, 0.95f * dk, 0.85f * dk));
        if (env_u_.ambient)
            env_u_.ambient->set(osg::Vec3(0.04f + 0.12f * dayf + flash * 0.4f,
                                          0.04f + 0.12f * dayf + flash * 0.4f,
                                          0.06f + 0.12f * dayf + flash * 0.5f));
        const osg::Vec4 sky(0.55f * dayf + 0.04f + flash * 0.3f, 0.72f * dayf + 0.06f + flash * 0.3f,
                            0.95f * dayf + 0.10f + flash * 0.2f, 1);
        for (auto& view : views_) {
            if (auto* cam = view->getCamera())
                cam->setClearColor(sky);
        }
    }

    void rebuild_skyline() {
        drawn_era_ = active_skyline_era();
        if (!skyline_root_)
            return;
        skyline_root_->removeChildren(0, skyline_root_->getNumChildren());
        const auto mesh = mesh_landmarks(drawn_era_);
        if (auto* n = make_landmark_geode(mesh))
            skyline_root_->addChild(n);
        const auto lamps = landmark_warning_lights(drawn_era_);
        if (!lamps.empty()) {
            Airport tmp;
            tmp.lights = lamps;
            auto gpu = make_light_gpu(tmp);
            if (gpu.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(gpu.geom);
                skyline_root_->addChild(geode);
            }
        }
        if (!scene_apts_.empty())
            rebuild_session_solids(scene_apts_);
        refresh_hud();
    }

    void refresh_hud() {
        if (hud_era_) {
            if (session_era_unlocked()) {
                const char* e = session_era() == SkylineEra::Classic ? "Classic (pre-2001)"
                                                                    : "Current";
                hud_era_->setText(std::string("Era: ") + e);
            } else {
                hud_era_->setText("");
            }
        }
        if (hud_missiles_) {
            if (session_era_unlocked())
                hud_missiles_->setText(session_missiles() ? "Missiles: On" : "Missiles: Off");
            else
                hud_missiles_->setText("");
        }
        if (hud_toast_) {
            const std::string& mt = missile_world().toast;
            hud_toast_->setText(mt.empty() ? unlock_.toast : mt);
        }
    }

    osg::GLExtensions* gl_ext() {
        if (views_.empty() || !views_.front() || !views_.front()->getCamera())
            return nullptr;
        osg::GraphicsContext* gc = views_.front()->getCamera()->getGraphicsContext();
        if (!gc || !gc->getState())
            return nullptr;
        return osg::GLExtensions::Get(gc->getState()->getContextID(), true);
    }

    void begin_gpu_query() {
        osg::GLExtensions* ext = gl_ext();
        if (!ext || !ext->isTimerQuerySupported)
            return;
        if (gpu_query_ == 0)
            ext->glGenQueries(1, &gpu_query_);
        if (gpu_query_ == 0)
            return;
        harvest_gpu_query(false);
        ext->glBeginQuery(GL_TIME_ELAPSED, gpu_query_);
        gpu_query_open_ = true;
    }

    void harvest_gpu_query(bool wait) {
        if (!gpu_query_pending_ || gpu_query_ == 0)
            return;
        osg::GLExtensions* ext = gl_ext();
        if (!ext)
            return;
        if (!wait) {
            GLuint avail = 0;
            ext->glGetQueryObjectuiv(gpu_query_, GL_QUERY_RESULT_AVAILABLE, &avail);
            if (!avail)
                return;
        }
        GLuint64 ns = 0;
        ext->glGetQueryObjectui64v(gpu_query_, GL_QUERY_RESULT, &ns);
        last_gpu_ms_ = static_cast<float>(static_cast<double>(ns) * 1e-6);
        gpu_query_pending_ = false;
    }

    void end_gpu_query(bool wait) {
        osg::GLExtensions* ext = gl_ext();
        if (gpu_query_open_ && ext) {
            ext->glEndQuery(GL_TIME_ELAPSED);
            gpu_query_open_ = false;
            gpu_query_pending_ = true;
        }
        harvest_gpu_query(wait);
    }

    void apply_runtime_toggles() {
        if (env_u_.atmosphere)
            env_u_.atmosphere->set(cfg_.render.atmosphere ? 1.f : 0.f);
        if (env_u_.water)
            env_u_.water->set(cfg_.render.water ? 1.f : 0.f);
        if (env_u_.detail)
            env_u_.detail->set(cfg_.render.detail_textures ? 1.f : 0.f);
        if (env_u_.ssao)
            env_u_.ssao->set(cfg_.render.ssao ? 1.f : 0.f);
        if (env_u_.bloom)
            env_u_.bloom->set(cfg_.render.bloom ? 1.f : 0.f);
        if (env_u_.filmic)
            env_u_.filmic->set(cfg_.render.filmic ? 1.f : 0.f);
        if (env_u_.shadows)
            env_u_.shadows->set(cfg_.render.shadows && shadow_cam_ ? 1.f : 0.f);
        if (env_u_.clustered)
            env_u_.clustered->set(cfg_.render.clustered ? 1.f : 0.f);
        if (taa_u_)
            taa_u_->set(cfg_.render.taa ? 1.f : 0.f);
        if (env_u_.haze && !cfg_.render.atmosphere)
            env_u_.haze->set(0.f);
        if (!cfg_.render.lod_fade) {
            for (auto& r : resident_) {
                r.fade = 1.f;
                if (r.lod_fade_u)
                    r.lod_fade_u->set(1.f);
            }
        }
    }

    void fade_tiles() {
        const bool snap = !cfg_.render.lod_fade || last_host_.script_dt >= 0.f;
        for (auto& r : resident_) {
            if (snap)
                r.fade = 1.f;
            else if (r.fade < 1.f)
                r.fade = std::min(1.f, r.fade + 0.20f);
            if (r.lod_fade_u)
                r.lod_fade_u->set(r.fade);
        }
    }

    void flip_taa() {
        if (!cfg_.render.taa || !taa_a_ || !taa_b_ || !taa_scene_cam_ || !taa_ss_)
            return;
        taa_parity_ ^= 1;
        osg::Texture2D* curr = taa_parity_ ? taa_b_.get() : taa_a_.get();
        osg::Texture2D* hist = taa_parity_ ? taa_a_.get() : taa_b_.get();
        taa_scene_cam_->detach(osg::Camera::COLOR_BUFFER);
        taa_scene_cam_->attach(osg::Camera::COLOR_BUFFER, curr);
        taa_ss_->setTextureAttributeAndModes(0, curr, osg::StateAttribute::ON);
        taa_ss_->setTextureAttributeAndModes(1, hist, osg::StateAttribute::ON);
        if (taa_u_)
            taa_u_->set(1.f);
    }

    AppConfig cfg_;
    std::vector<ChannelConfig> channels_;
    std::unique_ptr<osgViewer::CompositeViewer> viewer_;
    std::vector<osg::ref_ptr<osgViewer::View>> views_;
    osg::ref_ptr<osg::Group> root_;
    ShaderEnv env_u_;
    LightGpu lights_gpu_;
    PrecipGpu precip_gpu_;
    PrecipGpu missile_gpu_;
    bool ptt_key_down_ = false;
    MeshGpu cloud_gpu_;
    MeshGpu storm_gpu_;
    TrafficGpu traffic_gpu_;
    int last_lightning_ = 0;
    int flash_left_ = 0;
    int last_storm_bolt_[xt4::kMaxActiveStorms] = {};
    int storm_flash_left_ = 0;
    int storm_tick_ = 0;
    float storm_flash_ = 0.f;
    float cloud_drift_m_ = 0.f;
    osg::ref_ptr<osg::Node> sky_xf_;
    bool used_earth_ = false;
    bool stripped_ff_ = false;
    bool gl_check_errors_ = false;
    HostState last_host_;
    struct ResidentTile {
        std::string path;
        osg::ref_ptr<osg::Node> node;
        std::uint64_t cpu = 0;
        std::uint64_t vram = 0;
        int lod = 0;
        double west = 0, south = 0, east = 0, north = 0;
        float fade = 1.f;
        osg::ref_ptr<osg::Uniform> lod_fade_u;
    };
    std::vector<CacheTileRef> tile_index_;
    std::vector<ResidentTile> resident_;
    osg::ref_ptr<osg::Group> tiles_root_;
    osg::ref_ptr<osg::Group> casters_;
    osg::ref_ptr<osg::Camera> shadow_cam_;
    osg::ref_ptr<osg::Texture2D> shadow_tex_;
    osg::ref_ptr<osg::Camera> shadow_cam_far_;
    osg::ref_ptr<osg::Texture2D> shadow_tex_far_;
    osg::ref_ptr<osg::Texture2D> taa_a_;
    osg::ref_ptr<osg::Texture2D> taa_b_;
    osg::ref_ptr<osg::Camera> taa_scene_cam_;
    osg::ref_ptr<osg::StateSet> taa_ss_;
    osg::ref_ptr<osg::Uniform> taa_u_;
    int taa_parity_ = 0;
    std::uint64_t ram_budget_ = 2ull << 30;
    std::uint64_t vram_budget_ = 5ull << 30;
    std::uint64_t resident_cpu_ = 0;
    std::uint64_t resident_vram_ = 0;
    int mesh_n_ = 96;
    bool tiles_ready_ = true;
    std::unordered_set<std::string> bad_tiles_;
    osg::ref_ptr<osg::Camera> shot_cam_;
    osg::ref_ptr<PackReadback> shot_cb_;
    int shot_w_ = 0;
    int shot_h_ = 0;
    osg::ref_ptr<osg::Group> skyline_root_;
    osg::ref_ptr<osgText::Text> hud_version_;
    osg::ref_ptr<osgText::Text> hud_toast_;
    osg::ref_ptr<osgText::Text> hud_era_;
    osg::ref_ptr<osgText::Text> hud_missiles_;
    EraUnlock unlock_;
    SkylineEra drawn_era_ = SkylineEra::Current;
    std::vector<Airport> scene_apts_;
    GLuint gpu_query_ = 0;
    bool gpu_query_open_ = false;
    bool gpu_query_pending_ = false;
    float last_gpu_ms_ = -1.f;
    float last_cpu_ms_ = -1.f;
    std::string autodrop_log_;
};

bool OsgRenderer::on_hud_click(float x, float y) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    const InstructorHit hit = instructor_hit(x, y, session_era_unlocked() || unlock_.unlocked);
    if (hit == InstructorHit::Version) {
        unlock_.click(now);
        if (unlock_.unlocked)
            set_session_era(session_era(), true);
        refresh_hud();
        return true;
    }
    if (hit == InstructorHit::EraCurrent) {
        set_session_era(SkylineEra::Current, true);
        rebuild_skyline();
        return true;
    }
    if (hit == InstructorHit::EraClassic) {
        set_session_era(SkylineEra::Classic, true);
        rebuild_skyline();
        return true;
    }
    if (hit == InstructorHit::MissilesOff) {
        set_session_missiles(false);
        refresh_hud();
        return true;
    }
    if (hit == InstructorHit::MissilesOn) {
        set_session_missiles(true);
        refresh_hud();
        return true;
    }
    return false;
}

} // namespace

std::unique_ptr<Renderer> make_osg_renderer() {
    return std::make_unique<OsgRenderer>();
}

} // namespace s376

#endif // S376_HAS_OSG
