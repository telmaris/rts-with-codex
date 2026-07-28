#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 lightColor;
uniform float intensity;
uniform float animationTime;
uniform float stablePhase;
uniform float animationAmount;
uniform int maskOnly;

out vec4 finalColor;

void main()
{
    vec2 centered = fragTexCoord - vec2(0.5);
    float angle = atan(centered.y, centered.x);

    // Two slow, low-amplitude waves make the edge breathe without moving the
    // light/reveal centre. The phase is stable per source, so neighbouring
    // buildings do not pulse in lockstep.
    float edgeWave =
        sin(angle * 6.0 + animationTime * 0.85 + stablePhase) * 0.018 +
        sin(angle * 11.0 - animationTime * 0.53 + stablePhase * 1.37) * 0.009;
    float breathing = sin(animationTime * 0.62 + stablePhase) * 0.012;
    float radialScale = 1.0 + (edgeWave + breathing) * animationAmount;
    vec2 animatedUv = vec2(0.5) + centered * radialScale;

    float inside = step(0.0, animatedUv.x) * step(animatedUv.x, 1.0) *
                   step(0.0, animatedUv.y) * step(animatedUv.y, 1.0);
    float falloff = texture(texture0, clamp(animatedUv, 0.0, 1.0)).a * inside;

    // Fog-mask authoring needs the original alpha-compositing semantics:
    // white source color with radial alpha. Lights use additive RGB.
    if (maskOnly != 0)
        finalColor = vec4(1.0, 1.0, 1.0, falloff) * fragColor;
    else
        finalColor = vec4(lightColor.rgb * intensity * falloff, falloff) * fragColor;
}
