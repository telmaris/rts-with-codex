#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform float exposure;
uniform float saturation;
uniform float contrast;

out vec4 finalColor;

void main()
{
    vec4 albedo = texture(texture0, fragTexCoord);
    // This pass owns only global time-of-day grading. Local emitters are
    // composited directly onto the lit target afterwards, using the same
    // Camera2D matrix as world sprites. No screen-space light texture is
    // sampled here, so camera pan/zoom cannot de-register the two images.
    vec3 color = albedo.rgb * (ambientColor * ambientIntensity);
    color *= exposure;

    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, saturation);
    color = (color - 0.5) * contrast + 0.5;

    finalColor = vec4(clamp(color, 0.0, 1.0), albedo.a) * fragColor;
}
