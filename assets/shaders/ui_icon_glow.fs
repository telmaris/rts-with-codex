#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 atlasTexelSize;
uniform vec2 sourceUvMin;
uniform vec2 sourceUvMax;
uniform vec3 glowColor;
uniform float glowIntensity;

out vec4 finalColor;

float CellAlpha(vec2 uv)
{
    float inside = step(sourceUvMin.x, uv.x) * step(uv.x, sourceUvMax.x) *
                   step(sourceUvMin.y, uv.y) * step(uv.y, sourceUvMax.y);
    return texture(texture0, clamp(uv, sourceUvMin, sourceUvMax)).a * inside;
}

float Ring(float radius)
{
    vec2 x = vec2(atlasTexelSize.x * radius, 0.0);
    vec2 y = vec2(0.0, atlasTexelSize.y * radius);
    vec2 diagonal = atlasTexelSize * radius * 0.70710678;
    float alpha = 0.0;
    alpha = max(alpha, CellAlpha(fragTexCoord + x));
    alpha = max(alpha, CellAlpha(fragTexCoord - x));
    alpha = max(alpha, CellAlpha(fragTexCoord + y));
    alpha = max(alpha, CellAlpha(fragTexCoord - y));
    alpha = max(alpha, CellAlpha(fragTexCoord + vec2( diagonal.x,  diagonal.y)));
    alpha = max(alpha, CellAlpha(fragTexCoord + vec2(-diagonal.x,  diagonal.y)));
    alpha = max(alpha, CellAlpha(fragTexCoord + vec2( diagonal.x, -diagonal.y)));
    alpha = max(alpha, CellAlpha(fragTexCoord + vec2(-diagonal.x, -diagonal.y)));
    return alpha;
}

void main()
{
    float sourceAlpha = CellAlpha(fragTexCoord);
    float nearGlow = Ring(2.0) * 0.66;
    float middleGlow = Ring(5.0) * 0.34;
    float outerGlow = Ring(9.0) * 0.14;
    float halo = max(nearGlow, max(middleGlow, outerGlow)) * (1.0 - sourceAlpha);
    float illuminatedCore = sourceAlpha * 0.10;
    float alpha = clamp((halo + illuminatedCore) * glowIntensity, 0.0, 0.92);
    finalColor = vec4(glowColor, alpha) * fragColor;
}
