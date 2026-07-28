#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D fogMask;

out vec4 finalColor;

void main()
{
    vec4 world = texture(texture0, fragTexCoord);
    float visibility = texture(fogMask, fragTexCoord).r;

    // Leave no readable silhouette in the dark part of fog. The fully
    // revealed centre must be an exact copy of the lit world; only the
    // transition band receives a warm exploration tint before it disappears.
    // This avoids the old permanently yellow cast over owned territory.
    vec3 obscuredColor = vec3(0.002, 0.004, 0.008);
    vec3 explorationTint = vec3(0.24, 0.18, 0.055);
    float edge = smoothstep(0.12, 0.60, visibility);
    float fullyRevealed = smoothstep(0.58, 0.82, visibility);
    vec3 edgeColor = mix(obscuredColor, explorationTint, edge);
    vec3 color = mix(edgeColor, world.rgb, fullyRevealed);

    finalColor = vec4(color, 1.0) * fragColor;
}
