#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

out vec4 finalColor;

void main()
{
    // texture0 is only the visibility mask. Fog does not know the albedo,
    // lightmap or postprocess output; normal alpha blending composes this dark
    // overlay over the already finished world.
    float visibility = clamp(texture(texture0, fragTexCoord).r, 0.0, 1.0);
    // Hold the fully dark state slightly longer, then transition more quickly
    // to clear terrain. This keeps the explored edge decisive without turning
    // the centre of the revealed area grey.
    float revealed = smoothstep(0.20, 0.78, visibility);

    // Unexplored space must be completely opaque. In the transition band a
    // neutral black alpha overlay is equivalent to multiplying the finished
    // world by `revealed`: light intensity falls off naturally, while the hue
    // authored by mineral materials stays unchanged. Fully revealed pixels
    // receive zero fog alpha and therefore remain exact copies of the world.
    float opacity = 1.0 - revealed;
    finalColor = vec4(0.0, 0.0, 0.0, opacity) * fragColor;
}
