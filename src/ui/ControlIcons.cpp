#include "ui/ControlIcons.h"

#include <algorithm>
#include <array>
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
