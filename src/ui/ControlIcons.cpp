#include "ui/ControlIcons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace
{
    std::map<std::string, Texture2D> textures;
    Texture2D hudAtlas{};
    Texture2D hudHoverAtlas{};
    Texture2D royalPanel{};
    Texture2D royalChip{};
    Texture2D royalButtonFrame{};
    Texture2D royalButtonFrameHover{};
    Texture2D royalCrest{};
    Texture2D royalWindowPanel{};
    Texture2D royalResourceSlot{};
    Texture2D royalTitleBar{};
    Texture2D royalCloseButton{};
    Texture2D royalCloseButtonHover{};
    Texture2D pixelHudFrame{};
    Texture2D pixelHudPanel{};
    Texture2D pixelHudButton{};
    Texture2D pixelHudButtonHover{};
    Texture2D pixelTopHudStyle{};
    Texture2D pixelHudWidgetFrame{};
    Texture2D pixelHudCrest{};
    Texture2D pixelHudGlyphs{};
    Texture2D pixelPopulationGlyph{};
    Shader pixelHudGlowShader{};
    int pixelGlowTexelSizeLocation{-1};
    int pixelGlowUvMinLocation{-1};
    int pixelGlowUvMaxLocation{-1};
    int pixelGlowColorLocation{-1};
    int pixelGlowIntensityLocation{-1};
    Texture2D unitPortraitAtlas{};
    Texture2D militaryStatAtlas{};

    constexpr std::array<const char*, 24> ActiveIconNames{
        "key_q", "key_r", "key_d", "key_e", "key_s", "key_f", "key_t", "key_u", "key_l",
        "key_space", "key_escape", "key_f6", "key_f7", "key_f8", "key_f10", "key_ctrl",
        "key_up", "key_down", "key_enter", "key_backspace", "mouse_lmb", "mouse_rmb",
        "mouse_mmb", "mouse_wheel"};

    // Source cells in assets/ui/hud/generated/royal_hud_icons_atlas_v2.png.
    // The atlas was intentionally kept as a single source asset; these tight
    // crops omit its presentation-sheet gutters while retaining each icon's
    // steel frame and player-colour placeholder inlay.
    constexpr std::array<Rectangle, 10> HudIconSources{{
        {40.0f, 64.0f, 332.0f, 359.0f},   // Build
        {401.0f, 64.0f, 319.0f, 359.0f},  // Destroy
        {746.0f, 64.0f, 312.0f, 359.0f},  // Road
        {1080.0f, 64.0f, 312.0f, 359.0f}, // Logistics
        {1427.0f, 64.0f, 309.0f, 359.0f}, // Resources
        {40.0f, 449.0f, 332.0f, 345.0f},  // Roster
        {401.0f, 449.0f, 319.0f, 345.0f}, // Decisions
        {746.0f, 449.0f, 312.0f, 345.0f}, // Technology
        {1080.0f, 449.0f, 312.0f, 345.0f},// Manpower
        {1427.0f, 449.0f, 309.0f, 345.0f} // Builders
    }};

    constexpr Rectangle ResourceSlotSource{178.0f, 170.0f, 896.0f, 907.0f};
    constexpr Rectangle TitleBarSource{56.0f, 271.0f, 1871.0f, 248.0f};
    // Tight square crop: only short rail stubs remain around the inset plate,
    // making the control read as part of the title frame without becoming a
    // second horizontal banner.
    constexpr Rectangle CloseButtonSource{394.0f, 357.0f, 467.0f, 493.0f};
    constexpr Rectangle ButtonFrameSource{120.0f, 121.0f, 1013.0f, 1012.0f};
    constexpr float PixelGlyphSize = 96.0f;
    constexpr std::array<Rectangle, 10> PixelHudGlyphSources{{
        {0.0f, 0.0f, PixelGlyphSize, PixelGlyphSize},                         // Build
        {PixelGlyphSize, 0.0f, PixelGlyphSize, PixelGlyphSize},                // Destroy
        {PixelGlyphSize * 2.0f, 0.0f, PixelGlyphSize, PixelGlyphSize},         // Road
        {0.0f, PixelGlyphSize, PixelGlyphSize, PixelGlyphSize},                // Logistics
        {PixelGlyphSize, PixelGlyphSize, PixelGlyphSize, PixelGlyphSize},      // Resources
        {PixelGlyphSize * 2.0f, PixelGlyphSize, PixelGlyphSize, PixelGlyphSize},// Roster
        {0.0f, PixelGlyphSize * 2.0f, PixelGlyphSize, PixelGlyphSize},         // Decisions
        {PixelGlyphSize, PixelGlyphSize * 2.0f, PixelGlyphSize, PixelGlyphSize},// Technology
        {},                                                                    // Manpower: own sprite
        {PixelGlyphSize * 2.0f, PixelGlyphSize * 2.0f,
         PixelGlyphSize, PixelGlyphSize}                                       // Builders
    }};

    bool ResolvePixelHudGlyph(UiControlIcons::HudIcon icon, Texture2D& texture,
                              Rectangle& source)
    {
        if (icon == UiControlIcons::HudIcon::Manpower)
        {
            if (pixelPopulationGlyph.id == 0)
                return false;
            texture = pixelPopulationGlyph;
            source = {0.0f, 0.0f, static_cast<float>(texture.width),
                      static_cast<float>(texture.height)};
            return true;
        }

        const size_t index = static_cast<size_t>(icon);
        if (pixelHudGlyphs.id == 0 || index >= PixelHudGlyphSources.size() ||
            PixelHudGlyphSources[index].width <= 0.0f)
            return false;
        texture = pixelHudGlyphs;
        source = PixelHudGlyphSources[index];
        return true;
    }
    constexpr std::array<const char*, 12> UnitPortraitIds{{
        "militia", "swordsman", "armored_swordsman",
        "heavy_infantry", "archer", "heavy_archer",
        "spearman", "light_cavalry", "knight",
        "ballista", "ram", "catapult"
    }};
    constexpr int UnitPortraitColumns = 3;
    constexpr int MilitaryStatColumns = 3;
    // Tight subject bounds inside each 362x362 source cell. The generated
    // atlas has a few disconnected pixels from the neighbouring row near the
    // bottom of some cells; drawing whole cells made those fragments visible
    // and also stretched every portrait to a square. These authored crops keep
    // only the actual unit while retaining a small safety margin.
    constexpr std::array<Rectangle, 12> UnitPortraitCrops{{
        {86.0f, 61.0f, 184.0f, 269.0f},
        {70.0f, 62.0f, 216.0f, 279.0f},
        {41.0f, 69.0f, 232.0f, 266.0f},
        {59.0f, 11.0f, 253.0f, 275.0f},
        {39.0f, 11.0f, 269.0f, 277.0f},
        {19.0f, 10.0f, 289.0f, 281.0f},
        {70.0f, 0.0f, 226.0f, 277.0f},
        {19.0f, 0.0f, 284.0f, 264.0f},
        {1.0f, 0.0f, 278.0f, 270.0f},
        {29.0f, 0.0f, 289.0f, 267.0f},
        {14.0f, 0.0f, 306.0f, 243.0f},
        {11.0f, 0.0f, 293.0f, 271.0f}
    }};

    bool DrawNineSlice(Texture2D texture, Rectangle source, float sourceCap,
                       Rectangle destination, float destinationCap, Color tint)
    {
        if (texture.id == 0 || destination.width <= 0.0f || destination.height <= 0.0f)
            return false;

        // Point-filtered pixel art must land on whole pixels. Fractional card
        // widths previously left a one-pixel slit where the left rail met its
        // corner slices. Snap the outer box and every split coordinate once,
        // so adjacent pieces always share exactly the same edge.
        const float left = std::round(destination.x);
        const float top = std::round(destination.y);
        const float right = std::round(destination.x + destination.width);
        const float bottom = std::round(destination.y + destination.height);
        destination = {left, top, std::max(1.0f, right - left),
                        std::max(1.0f, bottom - top)};
        const float cap = std::round(std::min({destinationCap,
                                               destination.width * 0.5f,
                                               destination.height * 0.5f}));
        const float sourceMiddleW = source.width - sourceCap * 2.0f;
        const float sourceMiddleH = source.height - sourceCap * 2.0f;
        const float destinationMiddleW = std::max(0.0f, destination.width - cap * 2.0f);
        const float destinationMiddleH = std::max(0.0f, destination.height - cap * 2.0f);
        auto drawSlice = [&](Rectangle src, Rectangle dest)
        {
            if (dest.width > 0.0f && dest.height > 0.0f)
                DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, tint);
        };

        drawSlice({source.x, source.y, sourceCap, sourceCap},
                  {destination.x, destination.y, cap, cap});
        drawSlice({source.x + sourceCap, source.y, sourceMiddleW, sourceCap},
                  {destination.x + cap, destination.y, destinationMiddleW, cap});
        drawSlice({source.x + source.width - sourceCap, source.y, sourceCap, sourceCap},
                  {destination.x + destination.width - cap, destination.y, cap, cap});
        drawSlice({source.x, source.y + sourceCap, sourceCap, sourceMiddleH},
                  {destination.x, destination.y + cap, cap, destinationMiddleH});
        drawSlice({source.x + sourceCap, source.y + sourceCap, sourceMiddleW, sourceMiddleH},
                  {destination.x + cap, destination.y + cap,
                   destinationMiddleW, destinationMiddleH});
        drawSlice({source.x + source.width - sourceCap, source.y + sourceCap,
                   sourceCap, sourceMiddleH},
                  {destination.x + destination.width - cap, destination.y + cap,
                   cap, destinationMiddleH});
        drawSlice({source.x, source.y + source.height - sourceCap, sourceCap, sourceCap},
                  {destination.x, destination.y + destination.height - cap, cap, cap});
        drawSlice({source.x + sourceCap, source.y + source.height - sourceCap,
                   sourceMiddleW, sourceCap},
                  {destination.x + cap, destination.y + destination.height - cap,
                   destinationMiddleW, cap});
        drawSlice({source.x + source.width - sourceCap,
                   source.y + source.height - sourceCap, sourceCap, sourceCap},
                  {destination.x + destination.width - cap,
                   destination.y + destination.height - cap, cap, cap});
        return true;
    }

    bool DrawAsymmetricNineSlice(Texture2D texture, Rectangle source,
                                 float sourceLeft, float sourceRight,
                                 float sourceTop, float sourceBottom,
                                 Rectangle destination, float destinationLeft,
                                 float destinationRight, float destinationTop,
                                 float destinationBottom, Color tint,
                                 bool drawCenter = true)
    {
        if (texture.id == 0 || destination.width <= 0.0f || destination.height <= 0.0f ||
            source.width <= 0.0f || source.height <= 0.0f)
            return false;

        const float left = std::round(destination.x);
        const float top = std::round(destination.y);
        const float right = std::round(destination.x + destination.width);
        const float bottom = std::round(destination.y + destination.height);
        destination = {left, top, std::max(1.0f, right - left),
                        std::max(1.0f, bottom - top)};

        sourceLeft = std::clamp(sourceLeft, 0.0f, source.width);
        sourceRight = std::clamp(sourceRight, 0.0f, source.width - sourceLeft);
        sourceTop = std::clamp(sourceTop, 0.0f, source.height);
        sourceBottom = std::clamp(sourceBottom, 0.0f, source.height - sourceTop);

        // Keep both rails visible on narrow window sizes without letting them
        // overlap. This is only a fallback for unusually small destinations;
        // normal game resolutions retain the authored border sizes below.
        const float sourceHorizontalTotal = sourceLeft + sourceRight;
        const float sourceVerticalTotal = sourceTop + sourceBottom;
        if (sourceHorizontalTotal > source.width)
        {
            const float factor = source.width / sourceHorizontalTotal;
            sourceLeft *= factor;
            sourceRight *= factor;
        }
        if (sourceVerticalTotal > source.height)
        {
            const float factor = source.height / sourceVerticalTotal;
            sourceTop *= factor;
            sourceBottom *= factor;
        }

        auto fitDestinationCaps = [](float total, float& first, float& second)
        {
            const float requested = first + second;
            if (requested > total && requested > 0.0f)
            {
                const float factor = total / requested;
                first *= factor;
                second *= factor;
            }
        };
        destinationLeft = std::max(0.0f, destinationLeft);
        destinationRight = std::max(0.0f, destinationRight);
        destinationTop = std::max(0.0f, destinationTop);
        destinationBottom = std::max(0.0f, destinationBottom);
        fitDestinationCaps(destination.width, destinationLeft, destinationRight);
        fitDestinationCaps(destination.height, destinationTop, destinationBottom);

        const float sourceMiddleW = source.width - sourceLeft - sourceRight;
        const float sourceMiddleH = source.height - sourceTop - sourceBottom;
        const float destinationMiddleW = std::max(
            0.0f, destination.width - destinationLeft - destinationRight);
        const float destinationMiddleH = std::max(
            0.0f, destination.height - destinationTop - destinationBottom);
        auto drawSlice = [&](Rectangle src, Rectangle dest)
        {
            if (src.width > 0.0f && src.height > 0.0f &&
                dest.width > 0.0f && dest.height > 0.0f)
                DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, tint);
        };

        drawSlice({source.x, source.y, sourceLeft, sourceTop},
                  {destination.x, destination.y, destinationLeft, destinationTop});
        drawSlice({source.x + sourceLeft, source.y, sourceMiddleW, sourceTop},
                  {destination.x + destinationLeft, destination.y,
                   destinationMiddleW, destinationTop});
        drawSlice({source.x + source.width - sourceRight, source.y,
                   sourceRight, sourceTop},
                  {destination.x + destination.width - destinationRight,
                   destination.y, destinationRight, destinationTop});
        drawSlice({source.x, source.y + sourceTop, sourceLeft, sourceMiddleH},
                  {destination.x, destination.y + destinationTop,
                   destinationLeft, destinationMiddleH});
        if (drawCenter)
            drawSlice({source.x + sourceLeft, source.y + sourceTop,
                       sourceMiddleW, sourceMiddleH},
                      {destination.x + destinationLeft, destination.y + destinationTop,
                       destinationMiddleW, destinationMiddleH});
        drawSlice({source.x + source.width - sourceRight, source.y + sourceTop,
                   sourceRight, sourceMiddleH},
                  {destination.x + destination.width - destinationRight,
                   destination.y + destinationTop, destinationRight,
                   destinationMiddleH});
        drawSlice({source.x, source.y + source.height - sourceBottom,
                   sourceLeft, sourceBottom},
                  {destination.x, destination.y + destination.height - destinationBottom,
                   destinationLeft, destinationBottom});
        drawSlice({source.x + sourceLeft, source.y + source.height - sourceBottom,
                   sourceMiddleW, sourceBottom},
                  {destination.x + destinationLeft,
                   destination.y + destination.height - destinationBottom,
                   destinationMiddleW, destinationBottom});
        drawSlice({source.x + source.width - sourceRight,
                   source.y + source.height - sourceBottom,
                   sourceRight, sourceBottom},
                  {destination.x + destination.width - destinationRight,
                   destination.y + destination.height - destinationBottom,
                   destinationRight, destinationBottom});
        return true;
    }

    bool DrawPixelPilotFrame(Texture2D texture, Rectangle source,
                             float sourceLeft, float sourceRight,
                             float sourceTop, float sourceBottom,
                             Rectangle destination, Color tint,
                             bool drawCenter = true)
    {
        if (texture.id == 0 || source.width <= 0.0f || source.height <= 0.0f)
            return false;

        // Preserve the authored corner aspect ratio when a compact control is
        // smaller than the full border budget. Once it fits, the corners stay
        // at their native pixel dimensions and only rails/centre stretch.
        const float cornerScale = std::min({
            1.0f,
            destination.width / (sourceLeft + sourceRight),
            destination.height / (sourceTop + sourceBottom)});
        const float destinationLeft = std::round(sourceLeft * cornerScale);
        const float destinationRight = std::round(sourceRight * cornerScale);
        const float destinationTop = std::round(sourceTop * cornerScale);
        const float destinationBottom = std::round(sourceBottom * cornerScale);
        return DrawAsymmetricNineSlice(
            texture,
            source,
            sourceLeft, sourceRight, sourceTop, sourceBottom,
            destination, destinationLeft, destinationRight,
            destinationTop, destinationBottom, tint, drawCenter);
    }

    bool DrawWidgetNineSlice(Texture2D texture, Rectangle destination, Color tint)
    {
        if (texture.id == 0 || destination.width <= 0.0f || destination.height <= 0.0f)
            return false;

        const float left = std::round(destination.x);
        const float top = std::round(destination.y);
        const float right = std::round(destination.x + destination.width);
        const float bottom = std::round(destination.y + destination.height);
        destination = {left, top, std::max(1.0f, right - left),
                        std::max(1.0f, bottom - top)};

        // Stretch only the quiet centre texture. The authored rail sprites
        // contain deliberate dark notches; scaling them produced apparent
        // holes and broken lines at arbitrary widget widths. Continuous
        // integer-aligned one-pixel rails retain the same graphite palette
        // without sampling any gaps from the source artwork.
        constexpr Rectangle centreSource{24.0f, 24.0f, 208.0f, 208.0f};
        DrawTexturePro(texture, centreSource, destination,
                       {0.0f, 0.0f}, 0.0f, tint);

        const Color outer = ColorTint(Color{8, 8, 9, 255}, tint);
        const Color steel = ColorTint(Color{112, 112, 112, 255}, tint);
        DrawRectangleLinesEx(destination, 1.0f, outer);
        Rectangle inner{destination.x + 1.0f, destination.y + 1.0f,
                        std::max(0.0f, destination.width - 2.0f),
                        std::max(0.0f, destination.height - 2.0f)};
        DrawRectangleLinesEx(inner, 1.0f, steel);
        return true;
    }
}

void UiControlIcons::Load(const std::string& directory)
{
    Unload();

    for (const char* name : ActiveIconNames)
    {
        const std::string path = directory + "/" + name + ".png";
        if (!FileExists(path.c_str()))
            continue;

        Texture2D texture = LoadTexture(path.c_str());
        if (texture.id == 0)
            continue;

        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        textures.emplace(name, texture);
    }

    constexpr const char* HudAtlasPath = "assets/ui/hud/generated/royal_hud_icons_atlas_v2.png";
    if (FileExists(HudAtlasPath))
    {
        hudAtlas = LoadTexture(HudAtlasPath);
        if (hudAtlas.id != 0)
            SetTextureFilter(hudAtlas, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* HudHoverAtlasPath = "assets/ui/hud/generated/royal_hud_icons_hover_atlas_v2.png";
    if (FileExists(HudHoverAtlasPath))
    {
        hudHoverAtlas = LoadTexture(HudHoverAtlasPath);
        if (hudHoverAtlas.id != 0)
            SetTextureFilter(hudHoverAtlas, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalPanelPath = "assets/ui/hud/generated/royal_panel_modular_graphite_v1.png";
    if (FileExists(RoyalPanelPath))
    {
        royalPanel = LoadTexture(RoyalPanelPath);
        if (royalPanel.id != 0)
            SetTextureFilter(royalPanel, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalChipPath = "assets/ui/hud/generated/royal_stat_chip_graphite_v1.png";
    if (FileExists(RoyalChipPath))
    {
        royalChip = LoadTexture(RoyalChipPath);
        if (royalChip.id != 0)
            SetTextureFilter(royalChip, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalButtonFramePath = "assets/ui/hud/generated/royal_button_frame_graphite_v1.png";
    if (FileExists(RoyalButtonFramePath))
    {
        royalButtonFrame = LoadTexture(RoyalButtonFramePath);
        if (royalButtonFrame.id != 0)
            SetTextureFilter(royalButtonFrame, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalButtonFrameHoverPath = "assets/ui/hud/generated/royal_button_frame_graphite_hover_v1.png";
    if (FileExists(RoyalButtonFrameHoverPath))
    {
        royalButtonFrameHover = LoadTexture(RoyalButtonFrameHoverPath);
        if (royalButtonFrameHover.id != 0)
            SetTextureFilter(royalButtonFrameHover, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalCrestPath = "assets/ui/hud/generated/royal_lion_crest_v1.png";
    if (FileExists(RoyalCrestPath))
    {
        royalCrest = LoadTexture(RoyalCrestPath);
        if (royalCrest.id != 0)
            SetTextureFilter(royalCrest, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalWindowPanelPath = "assets/ui/hud/generated/royal_window_panel_graphite_v1.png";
    if (FileExists(RoyalWindowPanelPath))
    {
        royalWindowPanel = LoadTexture(RoyalWindowPanelPath);
        if (royalWindowPanel.id != 0)
            SetTextureFilter(royalWindowPanel, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalResourceSlotPath = "assets/ui/hud/generated/royal_resource_slot_graphite_v1.png";
    if (FileExists(RoyalResourceSlotPath))
    {
        royalResourceSlot = LoadTexture(RoyalResourceSlotPath);
        if (royalResourceSlot.id != 0)
            SetTextureFilter(royalResourceSlot, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalTitleBarPath = "assets/ui/hud/generated/royal_title_bar_v2.png";
    if (FileExists(RoyalTitleBarPath))
    {
        royalTitleBar = LoadTexture(RoyalTitleBarPath);
        if (royalTitleBar.id != 0)
            SetTextureFilter(royalTitleBar, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalCloseButtonPath = "assets/ui/hud/generated/royal_close_socket_square_v4.png";
    if (FileExists(RoyalCloseButtonPath))
    {
        royalCloseButton = LoadTexture(RoyalCloseButtonPath);
        if (royalCloseButton.id != 0)
            SetTextureFilter(royalCloseButton, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* RoyalCloseButtonHoverPath = "assets/ui/hud/generated/royal_close_socket_square_hover_v4.png";
    if (FileExists(RoyalCloseButtonHoverPath))
    {
        royalCloseButtonHover = LoadTexture(RoyalCloseButtonHoverPath);
        if (royalCloseButtonHover.id != 0)
            SetTextureFilter(royalCloseButtonHover, TEXTURE_FILTER_BILINEAR);
    }

    constexpr const char* PixelHudFramePath = "assets/ui/hud/pixel_pilot/top_hud_frame.png";
    constexpr const char* PixelTopHudStylePath = "assets/ui/hud/pixel_pilot/top_hud_style.png";
    constexpr const char* PixelHudWidgetFramePath = "assets/ui/hud/pixel_pilot/widget_frame.png";
    constexpr const char* PixelHudCrestPath = "assets/ui/hud/pixel_pilot/top_hud_crest.png";
    constexpr const char* PixelGlyphAtlasPath = "assets/ui/hud/pixel_pilot/action_glyphs.png";
    constexpr const char* PixelPopulationPath = "assets/ui/hud/pixel_pilot/population_farmer.png";
    constexpr const char* PixelGlowShaderPath = "assets/shaders/ui_icon_glow.fs";
    constexpr const char* UnitPortraitAtlasPath = "assets/ui/barracks/unit_portraits_atlas.png";
    constexpr const char* MilitaryStatAtlasPath = "assets/ui/barracks/military_stat_icons_atlas.png";
    auto loadPixelTexture = [](const char* path)
    {
        Texture2D texture{};
        if (FileExists(path))
        {
            texture = LoadTexture(path);
            if (texture.id != 0)
                SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        }
        return texture;
    };
    pixelHudFrame = loadPixelTexture(PixelHudFramePath);
    pixelHudPanel = loadPixelTexture("assets/ui/hud/pixel_pilot/panel_hud.png");
    pixelHudButton = loadPixelTexture("assets/ui/hud/pixel_pilot/button_hud.png");
    pixelHudButtonHover = loadPixelTexture("assets/ui/hud/pixel_pilot/button_hud_hover.png");
    pixelTopHudStyle = loadPixelTexture(PixelTopHudStylePath);
    pixelHudWidgetFrame = loadPixelTexture(PixelHudWidgetFramePath);
    pixelHudCrest = loadPixelTexture(PixelHudCrestPath);
    pixelHudGlyphs = loadPixelTexture(PixelGlyphAtlasPath);
    pixelPopulationGlyph = loadPixelTexture(PixelPopulationPath);
    if (FileExists(PixelGlowShaderPath))
    {
        pixelHudGlowShader = LoadShader(nullptr, PixelGlowShaderPath);
        if (pixelHudGlowShader.id != 0)
        {
            pixelGlowTexelSizeLocation = GetShaderLocation(pixelHudGlowShader, "atlasTexelSize");
            pixelGlowUvMinLocation = GetShaderLocation(pixelHudGlowShader, "sourceUvMin");
            pixelGlowUvMaxLocation = GetShaderLocation(pixelHudGlowShader, "sourceUvMax");
            pixelGlowColorLocation = GetShaderLocation(pixelHudGlowShader, "glowColor");
            pixelGlowIntensityLocation = GetShaderLocation(pixelHudGlowShader, "glowIntensity");
        }
    }
    unitPortraitAtlas = loadPixelTexture(UnitPortraitAtlasPath);
    militaryStatAtlas = loadPixelTexture(MilitaryStatAtlasPath);
}

void UiControlIcons::Unload()
{
    for (auto& [name, texture] : textures)
        UnloadTexture(texture);
    textures.clear();
    if (hudAtlas.id != 0)
        UnloadTexture(hudAtlas);
    hudAtlas = {};
    if (hudHoverAtlas.id != 0)
        UnloadTexture(hudHoverAtlas);
    hudHoverAtlas = {};
    if (royalPanel.id != 0)
        UnloadTexture(royalPanel);
    royalPanel = {};
    if (royalChip.id != 0)
        UnloadTexture(royalChip);
    royalChip = {};
    if (royalButtonFrame.id != 0)
        UnloadTexture(royalButtonFrame);
    royalButtonFrame = {};
    if (royalButtonFrameHover.id != 0)
        UnloadTexture(royalButtonFrameHover);
    royalButtonFrameHover = {};
    if (royalCrest.id != 0)
        UnloadTexture(royalCrest);
    royalCrest = {};
    if (royalWindowPanel.id != 0)
        UnloadTexture(royalWindowPanel);
    royalWindowPanel = {};
    if (royalResourceSlot.id != 0)
        UnloadTexture(royalResourceSlot);
    royalResourceSlot = {};
    if (royalTitleBar.id != 0)
        UnloadTexture(royalTitleBar);
    royalTitleBar = {};
    if (royalCloseButton.id != 0)
        UnloadTexture(royalCloseButton);
    royalCloseButton = {};
    if (royalCloseButtonHover.id != 0)
        UnloadTexture(royalCloseButtonHover);
    royalCloseButtonHover = {};
    if (pixelHudFrame.id != 0)
        UnloadTexture(pixelHudFrame);
    pixelHudFrame = {};
    if (pixelHudPanel.id != 0)
        UnloadTexture(pixelHudPanel);
    pixelHudPanel = {};
    if (pixelHudButton.id != 0)
        UnloadTexture(pixelHudButton);
    pixelHudButton = {};
    if (pixelHudButtonHover.id != 0)
        UnloadTexture(pixelHudButtonHover);
    pixelHudButtonHover = {};
    if (pixelTopHudStyle.id != 0)
        UnloadTexture(pixelTopHudStyle);
    pixelTopHudStyle = {};
    if (pixelHudWidgetFrame.id != 0)
        UnloadTexture(pixelHudWidgetFrame);
    pixelHudWidgetFrame = {};
    if (pixelHudCrest.id != 0)
        UnloadTexture(pixelHudCrest);
    pixelHudCrest = {};
    if (pixelHudGlyphs.id != 0)
        UnloadTexture(pixelHudGlyphs);
    pixelHudGlyphs = {};
    if (pixelPopulationGlyph.id != 0)
        UnloadTexture(pixelPopulationGlyph);
    pixelPopulationGlyph = {};
    if (pixelHudGlowShader.id != 0)
        UnloadShader(pixelHudGlowShader);
    pixelHudGlowShader = {};
    pixelGlowTexelSizeLocation = -1;
    pixelGlowUvMinLocation = -1;
    pixelGlowUvMaxLocation = -1;
    pixelGlowColorLocation = -1;
    pixelGlowIntensityLocation = -1;
    if (unitPortraitAtlas.id != 0)
        UnloadTexture(unitPortraitAtlas);
    unitPortraitAtlas = {};
    if (militaryStatAtlas.id != 0)
        UnloadTexture(militaryStatAtlas);
    militaryStatAtlas = {};
}

bool UiControlIcons::IsLoaded()
{
    return !textures.empty();
}

bool UiControlIcons::Draw(const std::string& name, Rectangle destination, Color tint)
{
    auto it = textures.find(name);
    if (it == textures.end())
        return false;

    const Texture2D texture = it->second;
    Rectangle source{0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawHud(HudIcon icon, Rectangle destination, bool hovered, Color tint)
{
    const Texture2D texture = hovered && hudHoverAtlas.id != 0 ? hudHoverAtlas : hudAtlas;
    if (texture.id == 0)
        return false;

    const size_t index = static_cast<size_t>(icon);
    if (index >= HudIconSources.size())
        return false;

    // Action slots are deliberately square. The source cells vary by only a
    // few pixels, while fitting their native aspect left the whole right-side
    // strip visibly narrower than the square HUD cells around it.
    DrawTexturePro(texture, HudIconSources[index], destination,
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawHudGlyph(HudIcon icon, Rectangle destination, Color tint)
{
    if (hudAtlas.id == 0)
        return false;

    const size_t index = static_cast<size_t>(icon);
    if (index >= HudIconSources.size())
        return false;

    Rectangle source = HudIconSources[index];
    source.x += source.width * 0.13f;
    source.y += source.height * 0.13f;
    source.width *= 0.74f;
    source.height *= 0.70f;
    DrawTexturePro(hudAtlas, source, destination, {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawPixelHudFrame(Rectangle destination, bool hovered, Color tint)
{
    if (!DrawPixelHudPanelFrame(destination, tint))
        return false;

    if (hovered)
    {
        Rectangle inset{destination.x + 3.0f, destination.y + 3.0f,
                        destination.width - 6.0f, destination.height - 6.0f};
        DrawRectangleRec(inset, Fade(Color{86, 151, 164, 255}, 0.07f));
        DrawRectangleLinesEx(inset, 2.0f, Color{151, 207, 214, 230});
    }
    return true;
}

bool UiControlIcons::DrawPixelHudPanelFrame(Rectangle destination, Color tint)
{
    // The artwork is authored on a 256 px transparent canvas. These are the
    // actual visible bounds and the equivalent cap sizes after removing that
    // canvas gutter. Keeping the crop here makes the frame's visible edge
    // coincide with the layout rectangle passed by every panel.
    constexpr Rectangle source{47.0f, 64.0f, 159.0f, 122.0f};
    return DrawPixelPilotFrame(pixelHudPanel, source, 23.0f, 20.0f, 26.0f, 20.0f,
                               destination, tint);
}

bool UiControlIcons::DrawPixelHudButtonFrame(Rectangle destination, bool hovered,
                                              Color tint)
{
    // Same treatment as the panel frame: button_hud has a transparent gutter
    // around its authored 160x154 visible frame.
    constexpr Rectangle source{48.0f, 50.0f, 160.0f, 154.0f};
    const Texture2D frame = hovered && pixelHudButtonHover.id != 0
        ? pixelHudButtonHover
        : pixelHudButton;
    return DrawPixelPilotFrame(frame, source, 32.0f, 32.0f, 34.0f, 32.0f,
                               destination, tint);
}

bool UiControlIcons::DrawPixelTopHudFrame(Rectangle destination, Color tint)
{
    constexpr Rectangle source{0.0f, 0.0f, 344.0f, 192.0f};
    constexpr float sourceLeft = 128.0f;
    constexpr float sourceRight = 128.0f;
    constexpr float sourceTop = 74.0f;
    constexpr float sourceBottom = 74.0f;

    // The authored frame is taller than the current strategic HUD. Scale the
    // complete corner rectangles uniformly to fit the existing bar; this
    // keeps their proportions intact. Only the four rails and the centre are
    // allowed to stretch along their respective axes.
    const float cornerScale = std::min({
        1.0f,
        destination.height / (sourceTop + sourceBottom),
        destination.width / (sourceLeft + sourceRight)});
    const float destinationLeft = sourceLeft * cornerScale;
    const float destinationRight = sourceRight * cornerScale;
    const float destinationTop = sourceTop * cornerScale;
    const float destinationBottom = sourceBottom * cornerScale;
    return DrawAsymmetricNineSlice(pixelTopHudStyle, source,
                                   sourceLeft, sourceRight, sourceTop, sourceBottom,
                                   destination, destinationLeft, destinationRight,
                                   destinationTop, destinationBottom, tint);
}

float UiControlIcons::PixelHudFrameInset(Rectangle destination)
{
    // This inset is a layout affordance, not the source 9-slice cap. Keep the
    // historical values so existing title and body text do not move when the
    // frame artwork changes.
    return std::clamp(std::min(destination.width, destination.height) * 0.025f,
                      12.0f, 20.0f);
}

Rectangle UiControlIcons::PixelHudCloseButtonRect(Rectangle destination)
{
    constexpr float closeSize = 34.0f;
    // Sit fully inside the quiet header field rather than on the ornamental
    // corner. The same gap on both axes moves the socket left and down while
    // retaining top-right symmetry.
    const float edgeGap = std::clamp(PixelHudFrameInset(destination) + 5.0f,
                                     19.0f, 25.0f);
    return {destination.x + destination.width - edgeGap - closeSize,
            destination.y + edgeGap, closeSize, closeSize};
}

bool UiControlIcons::DrawPixelHudWidgetFrame(Rectangle destination, bool hovered, Color tint)
{
    return DrawPixelHudButtonFrame(destination, hovered, tint);
}

bool UiControlIcons::DrawPixelTopHudWidgetFrame(Rectangle destination, bool hovered,
                                                Color tint)
{
    if (!DrawWidgetNineSlice(pixelHudWidgetFrame, destination, tint))
        return false;

    if (hovered)
    {
        Rectangle inset{destination.x + 3.0f, destination.y + 3.0f,
                        destination.width - 6.0f, destination.height - 6.0f};
        DrawRectangleRec(inset, Fade(Color{86, 151, 164, 255}, 0.08f));
        DrawRectangleLinesEx(inset, 1.0f, Color{151, 207, 214, 220});
    }
    return true;
}

bool UiControlIcons::DrawPixelTopHudCrest(Rectangle destination, Color tint)
{
    if (pixelHudCrest.id == 0)
        return false;
    DrawTexturePro(pixelHudCrest,
                   {0.0f, 0.0f, static_cast<float>(pixelHudCrest.width),
                    static_cast<float>(pixelHudCrest.height)},
                   destination, {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawPixelHudGlyph(HudIcon icon, Rectangle destination, Color tint)
{
    Texture2D texture{};
    Rectangle source{};
    if (!ResolvePixelHudGlyph(icon, texture, source))
        return false;
    DrawTexturePro(texture, source, destination,
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawPixelHudGlow(HudIcon icon, Rectangle destination, Color color,
                                      float intensity)
{
    Texture2D texture{};
    Rectangle source{};
    if (pixelHudGlowShader.id == 0 ||
        !ResolvePixelHudGlyph(icon, texture, source))
        return false;

    const float texelSize[2]{1.0f / static_cast<float>(texture.width),
                             1.0f / static_cast<float>(texture.height)};
    const float uvMin[2]{source.x / static_cast<float>(texture.width),
                         source.y / static_cast<float>(texture.height)};
    const float uvMax[2]{(source.x + source.width) / static_cast<float>(texture.width),
                         (source.y + source.height) / static_cast<float>(texture.height)};
    const float glow[3]{static_cast<float>(color.r) / 255.0f,
                        static_cast<float>(color.g) / 255.0f,
                        static_cast<float>(color.b) / 255.0f};
    intensity = std::clamp(intensity, 0.0f, 2.0f);

    if (pixelGlowTexelSizeLocation >= 0)
        SetShaderValue(pixelHudGlowShader, pixelGlowTexelSizeLocation, texelSize,
                       SHADER_UNIFORM_VEC2);
    if (pixelGlowUvMinLocation >= 0)
        SetShaderValue(pixelHudGlowShader, pixelGlowUvMinLocation, uvMin,
                       SHADER_UNIFORM_VEC2);
    if (pixelGlowUvMaxLocation >= 0)
        SetShaderValue(pixelHudGlowShader, pixelGlowUvMaxLocation, uvMax,
                       SHADER_UNIFORM_VEC2);
    if (pixelGlowColorLocation >= 0)
        SetShaderValue(pixelHudGlowShader, pixelGlowColorLocation, glow,
                       SHADER_UNIFORM_VEC3);
    if (pixelGlowIntensityLocation >= 0)
        SetShaderValue(pixelHudGlowShader, pixelGlowIntensityLocation, &intensity,
                       SHADER_UNIFORM_FLOAT);

    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(pixelHudGlowShader);
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
    EndShaderMode();
    EndBlendMode();
    return true;
}

bool UiControlIcons::DrawUnitPortrait(const std::string& unitDefId,
                                      Rectangle destination, Color tint)
{
    if (unitPortraitAtlas.id == 0)
        return false;

    auto it = std::find_if(UnitPortraitIds.begin(), UnitPortraitIds.end(),
        [&unitDefId](const char* id) { return unitDefId == id; });
    if (it == UnitPortraitIds.end())
        return false;

    const int index = static_cast<int>(std::distance(UnitPortraitIds.begin(), it));
    const float cellWidth = unitPortraitAtlas.width / static_cast<float>(UnitPortraitColumns);
    const float cellHeight = unitPortraitAtlas.height / 4.0f;
    const Rectangle crop = UnitPortraitCrops[static_cast<size_t>(index)];
    Rectangle source{
        static_cast<float>(index % UnitPortraitColumns) * cellWidth + crop.x,
        static_cast<float>(index / UnitPortraitColumns) * cellHeight + crop.y,
        crop.width,
        crop.height};

    // Preserve the original silhouette proportions and leave a quiet inset;
    // weapons and horse heads must never touch the card's 9-slice rails.
    Rectangle safeDestination{
        destination.x + 3.0f,
        destination.y + 3.0f,
        std::max(0.0f, destination.width - 6.0f),
        std::max(0.0f, destination.height - 6.0f)};
    const float scale = std::min(safeDestination.width / source.width,
                                 safeDestination.height / source.height);
    Rectangle fitted{
        safeDestination.x + (safeDestination.width - source.width * scale) * 0.5f,
        safeDestination.y + (safeDestination.height - source.height * scale) * 0.5f,
        source.width * scale,
        source.height * scale};
    DrawTexturePro(unitPortraitAtlas, source, fitted,
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawMilitaryStat(MilitaryStatIcon icon,
                                      Rectangle destination, Color tint)
{
    if (militaryStatAtlas.id == 0)
        return false;

    const int index = static_cast<int>(icon);
    if (index < 0 || index >= 6)
        return false;
    const float cellWidth = militaryStatAtlas.width / static_cast<float>(MilitaryStatColumns);
    const float cellHeight = militaryStatAtlas.height / 2.0f;
    Rectangle source{
        static_cast<float>(index % MilitaryStatColumns) * cellWidth,
        static_cast<float>(index / MilitaryStatColumns) * cellHeight,
        cellWidth,
        cellHeight};
    DrawTexturePro(militaryStatAtlas, source, destination,
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawRoyalPanel(Rectangle destination, Color tint)
{
    if (royalPanel.id == 0)
        return false;

    // This restrained source deliberately has uniform rails and no center
    // ornament. Only those detail-free spans stretch; the four folded corner
    // plates keep a compact, fixed destination cap.
    constexpr Rectangle source{92.0f, 103.0f, 1070.0f, 1048.0f};
    constexpr float sourceCap = 96.0f;
    const float cap = std::clamp(destination.height * 0.22f, 14.0f, 22.0f);
    const float sourceMiddleW = source.width - sourceCap * 2.0f;
    const float sourceMiddleH = source.height - sourceCap * 2.0f;
    const float destinationMiddleW = std::max(0.0f, destination.width - cap * 2.0f);
    const float destinationMiddleH = std::max(0.0f, destination.height - cap * 2.0f);
    auto drawSlice = [&](Rectangle src, Rectangle dest)
    {
        if (dest.width > 0.0f && dest.height > 0.0f)
            DrawTexturePro(royalPanel, src, dest, Vector2{0.0f, 0.0f}, 0.0f, tint);
    };

    drawSlice({source.x, source.y, sourceCap, sourceCap},
              {destination.x, destination.y, cap, cap});
    drawSlice({source.x + sourceCap, source.y, sourceMiddleW, sourceCap},
              {destination.x + cap, destination.y, destinationMiddleW, cap});
    drawSlice({source.x + source.width - sourceCap, source.y, sourceCap, sourceCap},
              {destination.x + destination.width - cap, destination.y, cap, cap});
    drawSlice({source.x, source.y + sourceCap, sourceCap, sourceMiddleH},
              {destination.x, destination.y + cap, cap, destinationMiddleH});
    // The leather/stone grain in the original centre is intentionally kept
    // at its authored density. Tiling this quiet section avoids the blurry
    // low-res-to-wide stretch visible on large decisions/technology panels.
    const Rectangle sourceCenter{source.x + sourceCap, source.y + sourceCap,
                                 sourceMiddleW, sourceMiddleH};
    constexpr float tileSize = 172.0f;
    for (float y = destination.y + cap; y < destination.y + cap + destinationMiddleH; y += tileSize)
    {
        for (float x = destination.x + cap; x < destination.x + cap + destinationMiddleW; x += tileSize)
        {
            const float tileW = std::min(tileSize, destination.x + cap + destinationMiddleW - x);
            const float tileH = std::min(tileSize, destination.y + cap + destinationMiddleH - y);
            Rectangle tileSource{sourceCenter.x, sourceCenter.y,
                                 sourceCenter.width * tileW / tileSize,
                                 sourceCenter.height * tileH / tileSize};
            drawSlice(tileSource, {x, y, tileW, tileH});
        }
    }
    drawSlice({source.x + source.width - sourceCap, source.y + sourceCap,
               sourceCap, sourceMiddleH},
              {destination.x + destination.width - cap, destination.y + cap,
               cap, destinationMiddleH});
    drawSlice({source.x, source.y + source.height - sourceCap, sourceCap, sourceCap},
              {destination.x, destination.y + destination.height - cap, cap, cap});
    drawSlice({source.x + sourceCap, source.y + source.height - sourceCap,
               sourceMiddleW, sourceCap},
              {destination.x + cap, destination.y + destination.height - cap,
               destinationMiddleW, cap});
    drawSlice({source.x + source.width - sourceCap,
               source.y + source.height - sourceCap, sourceCap, sourceCap},
              {destination.x + destination.width - cap,
               destination.y + destination.height - cap, cap, cap});
    return true;
}

bool UiControlIcons::DrawRoyalChip(Rectangle destination, Color tint)
{
    if (royalChip.id == 0)
        return false;

    constexpr Rectangle source{273.0f, 281.0f, 1228.0f, 325.0f};
    constexpr float sourceCap = 92.0f;
    const float cap = std::clamp(destination.height * 0.32f, 16.0f,
                                 destination.width * 0.28f);
    const float middleSourceWidth = source.width - sourceCap * 2.0f;
    const float middleDestinationWidth = std::max(0.0f, destination.width - cap * 2.0f);

    DrawTexturePro(royalChip, {source.x, source.y, sourceCap, source.height},
                   {destination.x, destination.y, cap, destination.height},
                   {0.0f, 0.0f}, 0.0f, tint);
    if (middleDestinationWidth > 0.0f)
        DrawTexturePro(royalChip,
                       {source.x + sourceCap, source.y, middleSourceWidth, source.height},
                       {destination.x + cap, destination.y, middleDestinationWidth, destination.height},
                       {0.0f, 0.0f}, 0.0f, tint);
    DrawTexturePro(royalChip,
                   {source.x + source.width - sourceCap, source.y, sourceCap, source.height},
                   {destination.x + destination.width - cap, destination.y, cap, destination.height},
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawRoyalButtonFrame(Rectangle destination, bool hovered, Color tint)
{
    const Texture2D texture = hovered && royalButtonFrameHover.id != 0
        ? royalButtonFrameHover
        : royalButtonFrame;
    if (texture.id == 0)
        return false;

    constexpr float sourceCap = 220.0f;
    const float cap = std::clamp(destination.height * 0.46f, 12.0f,
                                 std::min(28.0f, destination.width * 0.28f));
    const float sourceMiddleW = ButtonFrameSource.width - sourceCap * 2.0f;
    const float sourceMiddleH = ButtonFrameSource.height - sourceCap * 2.0f;
    const float destinationMiddleW = std::max(0.0f, destination.width - cap * 2.0f);
    const float destinationMiddleH = std::max(0.0f, destination.height - cap * 2.0f);
    auto drawSlice = [&](Rectangle src, Rectangle dest)
    {
        if (dest.width > 0.0f && dest.height > 0.0f)
            DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, tint);
    };

    const Rectangle source = ButtonFrameSource;
    drawSlice({source.x, source.y, sourceCap, sourceCap},
              {destination.x, destination.y, cap, cap});
    drawSlice({source.x + sourceCap, source.y, sourceMiddleW, sourceCap},
              {destination.x + cap, destination.y, destinationMiddleW, cap});
    drawSlice({source.x + source.width - sourceCap, source.y, sourceCap, sourceCap},
              {destination.x + destination.width - cap, destination.y, cap, cap});
    drawSlice({source.x, source.y + sourceCap, sourceCap, sourceMiddleH},
              {destination.x, destination.y + cap, cap, destinationMiddleH});
    drawSlice({source.x + sourceCap, source.y + sourceCap, sourceMiddleW, sourceMiddleH},
              {destination.x + cap, destination.y + cap, destinationMiddleW, destinationMiddleH});
    drawSlice({source.x + source.width - sourceCap, source.y + sourceCap, sourceCap, sourceMiddleH},
              {destination.x + destination.width - cap, destination.y + cap, cap, destinationMiddleH});
    drawSlice({source.x, source.y + source.height - sourceCap, sourceCap, sourceCap},
              {destination.x, destination.y + destination.height - cap, cap, cap});
    drawSlice({source.x + sourceCap, source.y + source.height - sourceCap, sourceMiddleW, sourceCap},
              {destination.x + cap, destination.y + destination.height - cap, destinationMiddleW, cap});
    drawSlice({source.x + source.width - sourceCap, source.y + source.height - sourceCap, sourceCap, sourceCap},
              {destination.x + destination.width - cap, destination.y + destination.height - cap, cap, cap});
    return true;
}

bool UiControlIcons::DrawRoyalCrest(Rectangle destination, Color tint)
{
    if (royalCrest.id == 0)
        return false;

    constexpr Rectangle source{240.0f, 184.0f, 766.0f, 888.0f};
    const float sourceAspect = source.width / source.height;
    Rectangle fitted = destination;
    if (destination.width / destination.height > sourceAspect)
    {
        fitted.width = destination.height * sourceAspect;
        fitted.x += (destination.width - fitted.width) * 0.5f;
    }
    else
    {
        fitted.height = destination.width / sourceAspect;
        fitted.y += (destination.height - fitted.height) * 0.5f;
    }
    DrawTexturePro(royalCrest, source, fitted, {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawRoyalWindowPanel(Rectangle destination, Color tint)
{
    if (royalWindowPanel.id == 0)
        return false;

    constexpr Rectangle source{70.0f, 72.0f, 1114.0f, 1110.0f};
    constexpr float sourceCap = 100.0f;
    const float cap = RoyalWindowPanelInset(destination);
    const float sourceMiddleW = source.width - sourceCap * 2.0f;
    const float sourceMiddleH = source.height - sourceCap * 2.0f;
    const float destinationMiddleW = std::max(0.0f, destination.width - cap * 2.0f);
    const float destinationMiddleH = std::max(0.0f, destination.height - cap * 2.0f);
    auto drawSlice = [&](Rectangle src, Rectangle dest)
    {
        if (dest.width > 0.0f && dest.height > 0.0f)
            DrawTexturePro(royalWindowPanel, src, dest, {0.0f, 0.0f}, 0.0f, tint);
    };

    drawSlice({source.x, source.y, sourceCap, sourceCap},
              {destination.x, destination.y, cap, cap});
    drawSlice({source.x + sourceCap, source.y, sourceMiddleW, sourceCap},
              {destination.x + cap, destination.y, destinationMiddleW, cap});
    drawSlice({source.x + source.width - sourceCap, source.y, sourceCap, sourceCap},
              {destination.x + destination.width - cap, destination.y, cap, cap});
    drawSlice({source.x, source.y + sourceCap, sourceCap, sourceMiddleH},
              {destination.x, destination.y + cap, cap, destinationMiddleH});
    // Preserve texture density in tall/wide windows; decisions and tech trees
    // expose this centre at large sizes where stretching was visibly blurry.
    const Rectangle sourceCenter{source.x + sourceCap, source.y + sourceCap,
                                 sourceMiddleW, sourceMiddleH};
    constexpr float tileSize = 172.0f;
    for (float y = destination.y + cap; y < destination.y + cap + destinationMiddleH; y += tileSize)
    {
        for (float x = destination.x + cap; x < destination.x + cap + destinationMiddleW; x += tileSize)
        {
            const float tileW = std::min(tileSize, destination.x + cap + destinationMiddleW - x);
            const float tileH = std::min(tileSize, destination.y + cap + destinationMiddleH - y);
            Rectangle tileSource{sourceCenter.x, sourceCenter.y,
                                 sourceCenter.width * tileW / tileSize,
                                 sourceCenter.height * tileH / tileSize};
            drawSlice(tileSource, {x, y, tileW, tileH});
        }
    }
    drawSlice({source.x + source.width - sourceCap, source.y + sourceCap,
               sourceCap, sourceMiddleH},
              {destination.x + destination.width - cap, destination.y + cap,
               cap, destinationMiddleH});
    drawSlice({source.x, source.y + source.height - sourceCap, sourceCap, sourceCap},
              {destination.x, destination.y + destination.height - cap, cap, cap});
    drawSlice({source.x + sourceCap, source.y + source.height - sourceCap,
               sourceMiddleW, sourceCap},
              {destination.x + cap, destination.y + destination.height - cap,
               destinationMiddleW, cap});
    drawSlice({source.x + source.width - sourceCap,
               source.y + source.height - sourceCap, sourceCap, sourceCap},
              {destination.x + destination.width - cap,
               destination.y + destination.height - cap, cap, cap});
    return true;
}

float UiControlIcons::RoyalWindowPanelInset(Rectangle destination)
{
    return std::clamp(std::min(destination.width, destination.height) * 0.055f,
                      14.0f, 32.0f);
}

bool UiControlIcons::DrawRoyalResourceSlot(Rectangle destination, Color tint)
{
    if (royalResourceSlot.id == 0)
        return false;

    DrawTexturePro(royalResourceSlot, ResourceSlotSource, destination,
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawRoyalTitleBar(Rectangle destination, Color tint)
{
    if (royalTitleBar.id == 0)
        return false;

    constexpr float sourceCap = 112.0f;
    const float cap = std::clamp(destination.height * 0.46f, 18.0f,
                                 std::min(34.0f, destination.width * 0.24f));
    const float sourceMiddle = TitleBarSource.width - sourceCap * 2.0f;
    const float destinationMiddle = std::max(0.0f, destination.width - cap * 2.0f);

    DrawTexturePro(royalTitleBar,
                   {TitleBarSource.x, TitleBarSource.y, sourceCap, TitleBarSource.height},
                   {destination.x, destination.y, cap, destination.height},
                   {0.0f, 0.0f}, 0.0f, tint);
    if (destinationMiddle > 0.0f)
        DrawTexturePro(royalTitleBar,
                       {TitleBarSource.x + sourceCap, TitleBarSource.y,
                        sourceMiddle, TitleBarSource.height},
                       {destination.x + cap, destination.y,
                        destinationMiddle, destination.height},
                       {0.0f, 0.0f}, 0.0f, tint);
    DrawTexturePro(royalTitleBar,
                   {TitleBarSource.x + TitleBarSource.width - sourceCap,
                    TitleBarSource.y, sourceCap, TitleBarSource.height},
                   {destination.x + destination.width - cap, destination.y,
                    cap, destination.height},
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}

bool UiControlIcons::DrawPanelCloseButton(Rectangle destination, bool hovered, Color tint)
{
    const Texture2D texture = hovered && royalCloseButtonHover.id != 0
        ? royalCloseButtonHover
        : royalCloseButton;
    if (texture.id == 0)
        return false;

    DrawTexturePro(texture, CloseButtonSource, destination,
                   {0.0f, 0.0f}, 0.0f, tint);
    return true;
}
