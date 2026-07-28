#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D materialMask;
uniform vec4 playerPrimary;
uniform vec4 playerSecondary;
uniform int hasMaterialMask;
uniform int useAlbedoBlueKey;
uniform int blueKeyProfile;

out vec4 finalColor;

void main()
{
    vec4 albedo = texture(texture0, fragTexCoord);
    vec3 mask = hasMaterialMask != 0 ? texture(materialMask, fragTexCoord).rgb : vec3(0.0);

    // Transitional fallback for the current art. Explicit material masks take
    // precedence; until they are authored, only blue heraldic details are
    // recolored instead of tinting the entire building.
    float blueKey = 0.0;
    if (useAlbedoBlueKey != 0)
    {
        if (blueKeyProfile == 1)
        {
            // The refreshed HQ has a richer, darker blue trim. Its green
            // channel can approach blue in antialiased highlights, so key the
            // blue hue and saturation separately rather than requiring a large
            // blue-channel dominance.
            float maxChannel = max(albedo.r, max(albedo.g, albedo.b));
            float minChannel = min(albedo.r, min(albedo.g, albedo.b));
            float hue = smoothstep(0.015, 0.12, albedo.b - albedo.r) *
                        smoothstep(-0.035, 0.10, albedo.b - albedo.g);
            float saturation = smoothstep(0.06, 0.25, maxChannel - minChannel);
            float brightness = smoothstep(0.05, 0.28, maxChannel);
            blueKey = hue * saturation * brightness;
        }
        else
        {
            float nonBlue = max(albedo.r, albedo.g);
            blueKey = smoothstep(0.08, 0.30, albedo.b - nonBlue) *
                      smoothstep(0.18, 0.55, albedo.b);
        }
    }

    // Preserve painted highlights and shadows in the recolored material.
    float detail = dot(albedo.rgb, vec3(0.2126, 0.7152, 0.0722));
    detail = clamp(0.20 + detail * 1.15, 0.18, 1.15);

    vec3 primary = playerPrimary.rgb * detail;
    vec3 secondary = playerSecondary.rgb * detail;
    vec3 color = mix(albedo.rgb, primary, max(mask.r, blueKey));
    color = mix(color, secondary, mask.g);

    finalColor = vec4(color, albedo.a) * fragColor;
}
