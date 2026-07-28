#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D fogMask;

out vec4 finalColor;

void main()
{
    vec4 road = texture(texture0, fragTexCoord);
    float visibility = texture(fogMask, fragTexCoord).r;

    // Keep tracks opaque everywhere that counts as revealed terrain. The
    // radial texture can peak below 1.0 after filtering, so a near-white
    // threshold makes a road disappear even at the centre of its own reveal.
    // Match the point at which fog_of_war.fs begins restoring the real world.
    // The preceding amber transition intentionally does not leak road routes.
    float fullyVisible = step(0.58, visibility);
    finalColor = vec4(road.rgb, road.a * fullyVisible) * fragColor;
}
