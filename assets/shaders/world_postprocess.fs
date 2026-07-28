#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform int colorGradingEnabled;
uniform int retroFilterEnabled;
uniform int rainOverlayEnabled;
uniform vec2 resolution;
uniform float animationTime;

out vec4 finalColor;

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float Bayer4(vec2 pixel)
{
    ivec2 p = ivec2(mod(floor(pixel), 4.0));
    int index = p.x + p.y * 4;
    const float matrix[16] = float[16](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    return (matrix[index] + 0.5) / 16.0;
}

vec3 ApplyColorGrade(vec3 color)
{
    float luma = Luminance(color);
    vec3 coolShadows = color * vec3(0.975, 0.995, 1.035);
    vec3 warmHighlights = color * vec3(1.030, 1.005, 0.970);
    color = mix(color, coolShadows, (1.0 - luma) * 0.16);
    color = mix(color, warmHighlights, luma * 0.13);
    color = (color - 0.5) * 1.035 + 0.5;

    float gradedLuma = Luminance(color);
    color = mix(vec3(gradedLuma), color, 1.045);
    return clamp(color, 0.0, 1.0);
}

vec3 ApplyRetro(vec3 color)
{
    // A deliberately distinct but still strategy-readable palette. The old
    // 18-level quantization was too fine to be distinguishable from the
    // normal grade on hand-painted terrain.
    float luma = Luminance(color);
    color = mix(vec3(luma), color, 0.86);
    color *= vec3(1.055, 1.010, 0.895);

    float threshold = Bayer4(fragTexCoord * resolution) - 0.5;
    const float levels = 9.0;
    color = floor(clamp(color + threshold / levels, 0.0, 1.0) * levels + 0.5) / levels;

    // Visible on a still screenshot, but not strong enough to obscure roads
    // and unit silhouettes during a moving RTS camera.
    float scanline = mix(0.900, 1.0, step(0.5, mod(floor(fragTexCoord.y * resolution.y), 2.0)));
    vec2 centered = fragTexCoord - vec2(0.5);
    float vignette = 1.0 - 0.11 * smoothstep(0.18, 0.72, dot(centered, centered));
    return clamp(color * scanline * vignette, 0.0, 1.0);
}

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 ApplyRain(vec3 color)
{
    // Screen-space only: no texture asset, no gameplay weather state. The
    // Fewer, longer diagonal streaks read more naturally than a dense screen
    // of bright dots, while still leaving units and roads easy to read.
    vec2 pixel = fragTexCoord * resolution;
    vec2 cell = vec2(pixel.x * 0.070 + pixel.y * 0.022,
                     pixel.y * 0.090 - animationTime * 5.5);
    vec2 cellId = floor(cell);
    vec2 local = fract(cell);
    float seed = Hash12(cellId);
    float streak = (1.0 - smoothstep(0.0, 0.060, abs(local.x - seed))) *
                   (1.0 - smoothstep(0.06, 0.98, local.y));
    float visibleWorld = smoothstep(0.025, 0.10, Luminance(color));
    vec3 rainTint = vec3(0.40, 0.60, 0.82);
    color *= 1.0 - 0.055 * visibleWorld;
    color = mix(color, color * vec3(0.94, 0.98, 1.035), 0.045 * visibleWorld);
    color += rainTint * streak * visibleWorld * 0.22;
    return clamp(color, 0.0, 1.0);
}

void main()
{
    vec4 source = texture(texture0, fragTexCoord);
    vec3 color = source.rgb;

    if (colorGradingEnabled != 0)
        color = ApplyColorGrade(color);
    if (retroFilterEnabled != 0)
        color = ApplyRetro(color);
    if (rainOverlayEnabled != 0)
        color = ApplyRain(color);

    finalColor = vec4(color, source.a) * fragColor;
}
