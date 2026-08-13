#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 atlasTexelSize;
uniform vec2 sourceUvMin;
uniform vec2 sourceUvMax;
uniform vec3 shadowColor;
uniform vec3 baseColor;
uniform vec3 highlightColor;
uniform float luminanceScale;
uniform float luminanceBias;
uniform float edgeHighlightStrength;

out vec4 finalColor;

float CellAlpha(vec2 uv)
{
    float inside = step(sourceUvMin.x, uv.x) * step(uv.x, sourceUvMax.x) *
                   step(sourceUvMin.y, uv.y) * step(uv.y, sourceUvMax.y);
    return texture(texture0, clamp(uv, sourceUvMin, sourceUvMax)).a * inside;
}

// The minimum alpha immediately inside the source silhouette. A value below
// one identifies the inner edge of an individual rock without brightening its
// transparent surroundings or the terrain below it.
float InteriorAlpha(float radius)
{
    vec2 x = vec2(atlasTexelSize.x * radius, 0.0);
    vec2 y = vec2(0.0, atlasTexelSize.y * radius);
    vec2 diagonal = atlasTexelSize * radius * 0.70710678;
    float result = 1.0;
    result = min(result, CellAlpha(fragTexCoord + x));
    result = min(result, CellAlpha(fragTexCoord - x));
    result = min(result, CellAlpha(fragTexCoord + y));
    result = min(result, CellAlpha(fragTexCoord - y));
    result = min(result, CellAlpha(fragTexCoord + vec2( diagonal.x,  diagonal.y)));
    result = min(result, CellAlpha(fragTexCoord + vec2(-diagonal.x,  diagonal.y)));
    result = min(result, CellAlpha(fragTexCoord + vec2( diagonal.x, -diagonal.y)));
    result = min(result, CellAlpha(fragTexCoord + vec2(-diagonal.x, -diagonal.y)));
    return result;
}

// Maximum nearby source alpha, restricted to the current atlas cell. This is
// used for one subtle silhouette outline only; it is deliberately narrower
// than the former multi-pixel mineral halo.
float NeighbourAlpha(float radius)
{
    vec2 x = vec2(atlasTexelSize.x * radius, 0.0);
    vec2 y = vec2(0.0, atlasTexelSize.y * radius);
    vec2 diagonal = atlasTexelSize * radius * 0.70710678;
    float result = 0.0;
    result = max(result, CellAlpha(fragTexCoord + x));
    result = max(result, CellAlpha(fragTexCoord - x));
    result = max(result, CellAlpha(fragTexCoord + y));
    result = max(result, CellAlpha(fragTexCoord - y));
    result = max(result, CellAlpha(fragTexCoord + vec2( diagonal.x,  diagonal.y)));
    result = max(result, CellAlpha(fragTexCoord + vec2(-diagonal.x,  diagonal.y)));
    result = max(result, CellAlpha(fragTexCoord + vec2( diagonal.x, -diagonal.y)));
    result = max(result, CellAlpha(fragTexCoord + vec2(-diagonal.x, -diagonal.y)));
    return result;
}

void main()
{
    vec4 source = texture(texture0, fragTexCoord);
    float alpha = source.a;

    // The coal artwork occupies a much narrower and darker luminance range
    // than the other deposits. Per-material remapping lets it use the full
    // shadow/base/highlight palette while preserving its near-black crevices.
    float luminance = clamp(
        dot(source.rgb, vec3(0.2126, 0.7152, 0.0722)) * luminanceScale + luminanceBias,
        0.0, 1.0);
    vec3 mineral = mix(shadowColor, baseColor, smoothstep(0.05, 0.58, luminance));
    mineral = mix(mineral, highlightColor, smoothstep(0.58, 0.96, luminance));
    // Coal uses this to keep the bulk of a briquette close to black while a
    // narrow pale rim separates neighbouring pieces at distant zoom.
    float innerEdge = (1.0 - InteriorAlpha(0.90)) * alpha;
    mineral = mix(mineral, highlightColor, innerEdge * edgeHighlightStrength);

    // Restore a single, dark mineral-coloured rim at the outer silhouette.
    // It separates deposits from bright grass at distant zoom, without the
    // heavy black halo that previously filled the space between every rock.
    float outlineAlpha = max(0.0, NeighbourAlpha(0.95) - alpha) * 0.52;
    vec3 outlineColor = shadowColor * 0.16;
    float outlineContribution = outlineAlpha * (1.0 - alpha);
    float outputAlpha = alpha + outlineContribution;
    vec3 outputColor = (mineral * alpha + outlineColor * outlineContribution) /
                       max(outputAlpha, 0.0001);

    finalColor = vec4(outputColor, outputAlpha) * fragColor;
}
