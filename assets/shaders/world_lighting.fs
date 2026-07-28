#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D lightMap;
uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform float exposure;
uniform float saturation;
uniform float contrast;
uniform vec2 lightMapTexelSize;
uniform int localLightBloomEnabled;

out vec4 finalColor;

void main()
{
    vec4 albedo = texture(texture0, fragTexCoord);
    vec3 localLight = texture(lightMap, fragTexCoord).rgb;
    vec3 localBloom = vec3(0.0);
    if (localLightBloomEnabled != 0)
    {
        // Approximate bloom directly from the light-only buffer. This avoids
        // bright terrain and UI bleeding, and requires no emissive material
        // mask while artists are still preparing those assets.
        vec2 nearOffset = lightMapTexelSize * 3.0;
        vec2 farOffset = lightMapTexelSize * 8.0;
        localBloom += texture(lightMap, fragTexCoord + vec2( nearOffset.x, 0.0)).rgb * 0.13;
        localBloom += texture(lightMap, fragTexCoord + vec2(-nearOffset.x, 0.0)).rgb * 0.13;
        localBloom += texture(lightMap, fragTexCoord + vec2(0.0,  nearOffset.y)).rgb * 0.13;
        localBloom += texture(lightMap, fragTexCoord + vec2(0.0, -nearOffset.y)).rgb * 0.13;
        localBloom += texture(lightMap, fragTexCoord + vec2( farOffset.x,  farOffset.y)).rgb * 0.06;
        localBloom += texture(lightMap, fragTexCoord + vec2(-farOffset.x,  farOffset.y)).rgb * 0.06;
        localBloom += texture(lightMap, fragTexCoord + vec2( farOffset.x, -farOffset.y)).rgb * 0.06;
        localBloom += texture(lightMap, fragTexCoord + vec2(-farOffset.x, -farOffset.y)).rgb * 0.06;
    }
    vec3 color = albedo.rgb * (ambientColor * ambientIntensity + localLight) + localBloom * 0.32;
    color *= exposure;

    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, saturation);
    color = (color - 0.5) * contrast + 0.5;

    finalColor = vec4(clamp(color, 0.0, 1.0), albedo.a) * fragColor;
}
