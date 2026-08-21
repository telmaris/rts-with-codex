// Shared text/tooltip rendering. Moved verbatim out of Gui.cpp (see UiText.h for
// why); Gui.cpp's file-local MeasureUiText/DrawUiText/DrawTextFit/WrapText
// helpers now forward here so its ~200 call sites stayed untouched.

#include "ui/UiText.h"
#include "ui/ControlIcons.h"
#include "ui/RaylibResource.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <sstream>

namespace
{
    tvorin::ui::FontHandle uiFont{};
    bool uiFontLoaded{false};
    std::string uiFontPath;
    std::map<int, tvorin::ui::FontHandle> uiFontsBySize;
    tvorin::ui::FontHandle plainFont{};
    bool plainFontLoaded{false};
    std::string plainFontPath;
    std::map<int, tvorin::ui::FontHandle> plainFontsBySize;
    UiFontRole activeRole{UiFontRole::Display};

    std::string StripTooltipLinePrefix(const std::string& line)
    {
        if (line.rfind("{bonus}", 0) == 0)
            return line.substr(7);
        if (line.rfind("{penalty}", 0) == 0)
            return line.substr(9);
        return line;
    }

    std::string StripTooltipInlineMarkup(std::string line)
    {
        static const std::array<const char*, 6> tags{
            "{building}", "{/building}", "{resource}", "{/resource}", "{category}", "{/category}"};
        for (const char* tag : tags)
        {
            size_t position = 0;
            while ((position = line.find(tag, position)) != std::string::npos)
                line.erase(position, std::char_traits<char>::length(tag));
        }
        return line;
    }

    bool HasTooltipInlineMarkup(const std::string& line)
    {
        return line.find("{building}") != std::string::npos ||
               line.find("{resource}") != std::string::npos ||
               line.find("{category}") != std::string::npos;
    }

    void DrawTooltipInlineMarkup(const std::string& source, float x, float y, int fontSize, Color baseColor)
    {
        std::string text = StripTooltipLinePrefix(source);
        Color color = baseColor;
        std::string segment;
        auto flush = [&]()
        {
            if (segment.empty())
                return;
            UiText::Draw(segment, x, y, fontSize, color);
            x += static_cast<float>(UiText::Measure(segment, fontSize));
            segment.clear();
        };

        for (size_t index = 0; index < text.size();)
        {
            if (text.compare(index, 10, "{building}") == 0 || text.compare(index, 10, "{resource}") == 0)
            {
                flush();
                color = UiTheme::Gold;
                index += 10;
            }
            else if (text.compare(index, 10, "{category}") == 0)
            {
                flush();
                color = UiTheme::Cyan;
                index += 10;
            }
            else if (text.compare(index, 11, "{/building}") == 0 || text.compare(index, 11, "{/resource}") == 0)
            {
                flush();
                color = baseColor;
                index += 11;
            }
            else if (text.compare(index, 11, "{/category}") == 0)
            {
                flush();
                color = baseColor;
                index += 11;
            }
            else
                segment.push_back(text[index++]);
        }
        flush();
    }

    // Resolves the face for the active role. Plain without a loaded font falls
    // through to the Display font rather than raylib's blocky built-in, unless
    // nothing is loaded at all.
    bool ExactSizeFont(const std::string& path, const tvorin::ui::FontHandle& base,
                       std::map<int, tvorin::ui::FontHandle>& cache,
                       int requestedSize, const Font*& out)
    {
        requestedSize = std::max(8, requestedSize);
        if (requestedSize == base.Get().baseSize || path.empty())
        {
            out = &base.Get();
            return base.IsValid();
        }

        auto cached = cache.find(requestedSize);
        if (cached == cache.end())
        {
            tvorin::ui::FontHandle font{LoadFontEx(path.c_str(), requestedSize, nullptr, 0)};
            if (!font)
                return false;
            // The glyph atlas is rasterized at the exact requested size, so
            // point sampling preserves its authored antialiasing without a
            // second blurry interpolation pass.
            SetTextureFilter(font.Get().texture, TEXTURE_FILTER_POINT);
            cached = cache.emplace(requestedSize, std::move(font)).first;
        }
        out = &cached->second.Get();
        return true;
    }

    bool ActiveFont(const Font*& out, int requestedSize)
    {
        if (activeRole == UiFontRole::Plain && plainFontLoaded)
            return ExactSizeFont(plainFontPath, plainFont, plainFontsBySize,
                                 requestedSize, out);
        if (uiFontLoaded)
            return ExactSizeFont(uiFontPath, uiFont, uiFontsBySize,
                                 requestedSize, out);
        return false;
    }

    float InlineRunWidth(const UiInlineRun& run, int fontSize, float iconSize)
    {
        if (run.icon)
            return iconSize + 5.0f;
        return static_cast<float>(UiText::Measure(run.value, fontSize));
    }

    const char* ShortcutIconName(const std::string& token)
    {
        if (token == "[Q]") return "key_q";
        if (token == "[R]") return "key_r";
        if (token == "[D]") return "key_d";
        if (token == "[E]") return "key_e";
        if (token == "[S]") return "key_s";
        if (token == "[F]") return "key_f";
        if (token == "[T]") return "key_t";
        if (token == "[U]") return "key_u";
        if (token == "[L]") return "key_l";
        if (token == "[Space]") return "key_space";
        if (token == "[ESC]") return "key_escape";
        if (token == "[LMB]") return "mouse_lmb";
        if (token == "[RMB]") return "mouse_rmb";
        if (token == "[MMB]") return "mouse_mmb";
        if (token == "[Scroll]") return "mouse_wheel";
        return nullptr;
    }
}

void UiTextFont::Load(const std::string& path)
{
    if (!FileExists(path.c_str()))
        return;

    tvorin::ui::FontHandle next{LoadFontEx(path.c_str(), 32, nullptr, 0)};
    if (!next)
        return;

    SetTextureFilter(next.Get().texture, TEXTURE_FILTER_POINT);
    uiFont.Reset();
    for (auto& [size, font] : uiFontsBySize)
        font.Reset();
    uiFontsBySize.clear();
    uiFontPath = path;
    uiFont = std::move(next);
    uiFontLoaded = true;
}

void UiTextFont::LoadPlain(const std::string& path, int baseSize)
{
    if (!FileExists(path.c_str()))
        return;

    tvorin::ui::FontHandle next{LoadFontEx(path.c_str(), baseSize, nullptr, 0)};
    if (!next)
        return;

    SetTextureFilter(next.Get().texture, TEXTURE_FILTER_POINT);
    plainFont.Reset();
    for (auto& [size, font] : plainFontsBySize)
        font.Reset();
    plainFontsBySize.clear();
    plainFontPath = path;

    // LoadFontEx (not LoadFont) so the rasterization size can be chosen: dense
    // form text is drawn around 13-16px, and a 32px atlas downscales cleanly.
    plainFont = std::move(next);
    plainFontLoaded = true;
}

UiFontRole UiText::SetRole(UiFontRole role)
{
    UiFontRole previous = activeRole;
    activeRole = role;
    return previous;
}

UiFontRoleScope::UiFontRoleScope(UiFontRole role)
    : previousRole(UiText::SetRole(role))
{
}

UiFontRoleScope::~UiFontRoleScope()
{
    UiText::SetRole(previousRole);
}

UiFontRole UiText::GetRole()
{
    return activeRole;
}

void UiTextFont::Unload()
{
    for (auto& [size, font] : uiFontsBySize)
        font.Reset();
    uiFontsBySize.clear();
    uiFontPath.clear();
    uiFontLoaded = false;
    for (auto& [size, font] : plainFontsBySize)
        font.Reset();
    plainFontsBySize.clear();
    plainFontPath.clear();
    plainFontLoaded = false;
    uiFont.Reset();
    plainFont.Reset();
}

bool UiTextFont::IsLoaded()
{
    return uiFontLoaded;
}

const Font& UiTextFont::Get()
{
    return uiFont.Get();
}

const Font& UiTextFont::GetPlain()
{
    return plainFontLoaded ? plainFont.Get() : uiFont.Get();
}

int UiText::Measure(const std::string& text, int fontSize)
{
    const Font* font = nullptr;
    if (!ActiveFont(font, fontSize))
        return MeasureText(text.c_str(), fontSize);

    return static_cast<int>(std::ceil(MeasureTextEx(*font, text.c_str(), static_cast<float>(fontSize), 0.0f).x));
}

void UiText::Draw(const std::string& text, float x, float y, int fontSize, Color color)
{
    const Font* font = nullptr;
    if (ActiveFont(font, fontSize))
        DrawTextEx(*font, text.c_str(), {std::round(x), std::round(y)},
                   static_cast<float>(fontSize), 0.0f, color);
    else
        DrawText(text.c_str(), static_cast<int>(x), static_cast<int>(y), fontSize, color);
}

void UiText::DrawFit(const std::string& text, Rectangle bounds, int fontSize, Color color)
{
    int measured = Measure(text, fontSize);
    while (fontSize > 8 && measured > bounds.width)
    {
        fontSize--;
        measured = Measure(text, fontSize);
    }

    Draw(text,
        bounds.x + (bounds.width - measured) * 0.5f,
        bounds.y + (bounds.height - fontSize) * 0.5f,
        fontSize,
        color);
}

void UiText::DrawTitleBar(Rectangle titleBar, const std::string& text, float closeButtonReserve)
{
    UiFontRoleScope displayRole{UiFontRole::Display};
    // Panel titles are a primary navigation cue. Keep them a little larger
    // across every panel while preserving the existing fit-to-close-button
    // behavior for longer localized names.
    int titleFont = std::clamp(static_cast<int>(titleBar.height * 0.68f), 28, 40);
    int titleWidth = Measure(text, titleFont);
    while (titleFont > 14 && titleWidth > titleBar.width - closeButtonReserve)
    {
        titleFont--;
        titleWidth = Measure(text, titleFont);
    }
    Draw(text,
         titleBar.x + (titleBar.width - titleWidth) * 0.5f,
         titleBar.y + (titleBar.height - titleFont) * 0.5f,
         titleFont,
         UiTheme::Parchment);
}

std::vector<std::string> UiText::Wrap(const std::string& text, int fontSize, float maxWidth)
{
    std::vector<std::string> wrapped;
    std::istringstream words(text);
    std::string word;
    std::string line;

    while (words >> word)
    {
        std::string candidate = line.empty() ? word : line + " " + word;
        if (Measure(candidate, fontSize) <= maxWidth || line.empty())
        {
            line = candidate;
            continue;
        }

        wrapped.push_back(line);
        line = word;
    }

    if (!line.empty())
        wrapped.push_back(line);
    if (wrapped.empty())
        wrapped.push_back("");
    return wrapped;
}

std::vector<std::vector<UiInlineRun>> UiText::WrapWithControlIcons(
    const std::string& text, int fontSize, float maxWidth, float iconSize)
{
    std::istringstream words(text);
    std::string token;
    std::vector<UiInlineRun> currentLine;
    std::vector<std::vector<UiInlineRun>> wrapped;
    float currentWidth = 0.0f;
    bool resourceHighlight = false;

    auto flushLine = [&]()
    {
        if (!currentLine.empty())
        {
            wrapped.push_back(std::move(currentLine));
            currentLine.clear();
            currentWidth = 0.0f;
        }
    };

    while (words >> token)
    {
        std::vector<UiInlineRun> tokenRuns;
        constexpr const char* Prefix = "{icon:";
        if (token.rfind(Prefix, 0) == 0)
        {
            const size_t closingBrace = token.find('}', 6);
            if (closingBrace != std::string::npos && closingBrace > 6)
            {
                tokenRuns.push_back({token.substr(6, closingBrace - 6), true, false});
                if (closingBrace + 1 < token.size())
                    tokenRuns.push_back({token.substr(closingBrace + 1), false, resourceHighlight});
            }
        }
        if (tokenRuns.empty())
        {
            if (const char* iconName = ShortcutIconName(token))
                tokenRuns.push_back({iconName, true, false});
        }
        if (tokenRuns.empty())
        {
            // Tooltip-style inline markup is also accepted in tutorial prose.
            // Keep the tags out of the measured/drawn text while preserving
            // the resource highlight state across whitespace-separated words.
            size_t cursor = 0;
            while (cursor < token.size())
            {
                const size_t open = token.find("{resource}", cursor);
                const size_t close = token.find("{/resource}", cursor);
                size_t marker = std::min(open == std::string::npos ? token.size() : open,
                                         close == std::string::npos ? token.size() : close);
                if (marker > cursor)
                    tokenRuns.push_back({token.substr(cursor, marker - cursor), false, resourceHighlight});
                if (marker == token.size())
                    break;
                if (open != std::string::npos && open == marker)
                {
                    resourceHighlight = true;
                    cursor = marker + 10;
                }
                else
                {
                    resourceHighlight = false;
                    cursor = marker + 11;
                }
            }
            if (tokenRuns.empty())
                tokenRuns.push_back({token, false, resourceHighlight});
        }

        const float separatorWidth = currentLine.empty()
            ? 0.0f
            : static_cast<float>(Measure(" ", fontSize));
        float tokenWidth = 0.0f;
        for (const UiInlineRun& run : tokenRuns)
            tokenWidth += InlineRunWidth(run, fontSize, iconSize);
        if (!currentLine.empty() && currentWidth + separatorWidth + tokenWidth > maxWidth)
            flushLine();

        if (!currentLine.empty())
        {
            currentLine.push_back({" ", false, false});
            currentWidth += static_cast<float>(Measure(" ", fontSize));
        }
        for (UiInlineRun& run : tokenRuns)
            currentLine.push_back(std::move(run));
        currentWidth += tokenWidth;
    }

    flushLine();
    if (wrapped.empty())
        wrapped.push_back({});
    return wrapped;
}

void UiText::DrawWithControlIcons(const std::vector<UiInlineRun>& line,
                                  float x, float y, int fontSize, Color color,
                                  float iconSize)
{
    for (const UiInlineRun& run : line)
    {
        if (run.icon)
        {
            const float iconY = y + (fontSize - iconSize) * 0.5f;
            if (!UiControlIcons::Draw(run.value, {x, iconY, iconSize, iconSize}))
            {
                const std::string fallback = "[" + run.value + "]";
                Draw(fallback, x, y, fontSize, color);
                x += static_cast<float>(Measure(fallback, fontSize));
            }
            else
                x += iconSize + 5.0f;
        }
        else
        {
            Draw(run.value, x, y, fontSize, run.highlighted ? UiTheme::Gold : color);
            x += static_cast<float>(Measure(run.value, fontSize));
        }
    }
}

std::string Utf8::Encode(int codepoint)
{
    std::string encoded;
    if (codepoint <= 0x7F)
    {
        encoded.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        encoded.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        encoded.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0x10FFFF)
    {
        encoded.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return encoded;
}

void Utf8::RemoveLast(std::string& value)
{
    if (value.empty())
        return;

    size_t index = value.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80)
        index--;
    value.erase(index);
}

void Tooltip::Draw(const std::string& title, const std::vector<std::string>& lines, float preferredWidth,
                   const std::function<void(Rectangle)>& titleIcon, int titleFontSize)
{
    // Tooltips are intentionally self-contained: their descriptive body
    // remains sans even if a caller is currently drawing a display heading.
    UiFontRoleScope bodyRole{UiFontRole::Plain};
    int titleFont = std::max(16, titleFontSize);
    int lineFont = 20;
    float padding = 14.0f;
    float lineH = 27.0f;
    float paragraphGap = 5.0f;

    const float iconSize = titleIcon ? 36.0f : 0.0f;
    const float iconGap = titleIcon ? 8.0f : 0.0f;
    const float headerHeight = std::max(static_cast<float>(titleFont), iconSize);

    float width = std::max(240.0f, preferredWidth);
    {
        UiFontRoleScope displayRole{UiFontRole::Display};
        width = std::max(width, std::min(520.0f, static_cast<float>(UiText::Measure(title, titleFont)) + padding * 2.0f + iconSize + iconGap));
    }
    width = std::min(width, 520.0f);
    float textWidth = width - padding * 2.0f;

    struct TooltipParagraph
    {
        bool separator{false};
        std::vector<std::vector<UiInlineRun>> lines;
        std::string source;
    };

    std::vector<TooltipParagraph> wrappedLines;
    wrappedLines.reserve(lines.size());
    for (const auto& line : lines)
    {
        if (line == "{separator}")
        {
            wrappedLines.push_back(TooltipParagraph{true, {}, line});
            continue;
        }

        std::string displayLine = StripTooltipInlineMarkup(StripTooltipLinePrefix(line));

        TooltipParagraph paragraph;
        paragraph.source = line;
        paragraph.lines = UiText::WrapWithControlIcons(displayLine, lineFont, textWidth, 24.0f);
        wrappedLines.push_back(std::move(paragraph));
    }

    // Measure separators by their actual visual footprint. Treating one as a
    // full text line used to accumulate invisible height at the bottom of a
    // tooltip and made flavor text sit much closer to its upper rule than its
    // lower rule.
    constexpr float headerRuleOffset = 6.0f;
    constexpr float bodyTopGap = 12.0f;
    constexpr float separatorTopGap = 5.0f;
    constexpr float separatorBottomGap = 12.0f;
    float bodyHeight = 0.0f;
    for (size_t index = 0; index < wrappedLines.size(); ++index)
    {
        const auto& paragraph = wrappedLines[index];
        if (paragraph.separator)
        {
            bodyHeight += separatorTopGap + 1.0f + separatorBottomGap;
            continue;
        }

        bodyHeight += std::max<size_t>(1, paragraph.lines.size()) * lineH;
        if (index + 1 < wrappedLines.size() && !wrappedLines[index + 1].separator)
            bodyHeight += paragraphGap;
    }
    if (wrappedLines.empty())
        bodyHeight = lineH;

    float height = padding + headerHeight + headerRuleOffset + bodyTopGap +
                   bodyHeight + padding;
    Vector2 mouse = GetMousePosition();
    Rectangle bounds{mouse.x + 14.0f, mouse.y + 14.0f, width, height};
    bounds.x = std::min(bounds.x, static_cast<float>(GetScreenWidth()) - bounds.width - 8.0f);
    bounds.y = std::min(bounds.y, static_cast<float>(GetScreenHeight()) - bounds.height - 8.0f);
    bounds.x = std::max(8.0f, bounds.x);
    bounds.y = std::max(8.0f, bounds.y);

    // Tooltips are panels, so they use the same panel_hud chrome as windows.
    if (!UiControlIcons::DrawPixelHudPanelFrame(bounds))
    {
        DrawRectangleRounded(bounds, 0.05f, 8, UiTheme::Ink);
        DrawRectangleRoundedLines(bounds, 0.05f, 8, 1.4f, UiTheme::Iron);
        Rectangle inner{bounds.x + 3.0f, bounds.y + 3.0f,
                        bounds.width - 6.0f, bounds.height - 6.0f};
        DrawRectangleRounded(inner, 0.045f, 8, Fade(UiTheme::Surface, 0.99f));
        DrawRectangleRoundedLines(inner, 0.045f, 8, 1.0f, Fade(UiTheme::Bronze, 0.68f));
    }
    if (titleIcon)
        titleIcon(Rectangle{bounds.x + padding, bounds.y + padding, iconSize, iconSize});
    {
        UiFontRoleScope displayRole{UiFontRole::Display};
        UiText::Draw(title, bounds.x + padding + iconSize + iconGap,
                     bounds.y + padding + (headerHeight - titleFont) * 0.5f - 1.0f,
                     titleFont, UiTheme::Parchment);
    }

    float headerRuleY = bounds.y + padding + headerHeight + headerRuleOffset;
    DrawLineEx(Vector2{bounds.x + padding, headerRuleY},
               Vector2{bounds.x + bounds.width - padding, headerRuleY},
               1.0f, Fade(UiTheme::Bronze, 0.78f));
    float y = headerRuleY + bodyTopGap;
    for (size_t paragraphIndex = 0; paragraphIndex < wrappedLines.size(); paragraphIndex++)
    {
        const auto& paragraph = wrappedLines[paragraphIndex];
        if (paragraph.separator)
        {
            y += separatorTopGap;
            DrawLineEx(Vector2{bounds.x + padding, y}, Vector2{bounds.x + bounds.width - padding, y}, 1.0f, Fade(UiTheme::Bronze, 0.82f));
            y += 1.0f + separatorBottomGap;
            continue;
        }

        Color lineColor = UiTheme::ParchmentDim;
        const std::string& sourceLine = paragraph.source;
        if (sourceLine.rfind("{penalty}", 0) == 0)
            lineColor = UiTheme::Rust;
        else if (sourceLine.rfind("{bonus}", 0) == 0)
            lineColor = UiTheme::Sage;

        for (const auto& line : paragraph.lines)
        {
            // Marked names are intentionally kept on one visual line.  This
            // covers the compact modifier/cost rows; descriptive prose still
            // uses the regular wrapping path above.
            if (paragraph.lines.size() == 1 && HasTooltipInlineMarkup(sourceLine))
                DrawTooltipInlineMarkup(sourceLine, bounds.x + padding, y, lineFont, lineColor);
            else
                UiText::DrawWithControlIcons(line, bounds.x + padding, y, lineFont, lineColor, 24.0f);
            y += lineH;
        }
        if (paragraphIndex + 1 < wrappedLines.size() && !wrappedLines[paragraphIndex + 1].separator)
            y += paragraphGap;
    }
}
