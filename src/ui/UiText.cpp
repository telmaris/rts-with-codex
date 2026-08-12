// Shared text/tooltip rendering. Moved verbatim out of Gui.cpp (see UiText.h for
// why); Gui.cpp's file-local MeasureUiText/DrawUiText/DrawTextFit/WrapText
// helpers now forward here so its ~200 call sites stayed untouched.

#include "ui/UiText.h"
#include "ui/ControlIcons.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace
{
    Font uiFont{};
    bool uiFontLoaded{false};
    Font plainFont{};
    bool plainFontLoaded{false};
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
    bool ActiveFont(Font& out)
    {
        if (activeRole == UiFontRole::Plain && plainFontLoaded)
        {
            out = plainFont;
            return true;
        }
        if (uiFontLoaded)
        {
            out = uiFont;
            return true;
        }
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

    if (uiFontLoaded)
        UnloadFont(uiFont);

    uiFont = LoadFont(path.c_str());
    uiFontLoaded = uiFont.texture.id != 0;
    if (uiFontLoaded)
        SetTextureFilter(uiFont.texture, TEXTURE_FILTER_BILINEAR);
}

void UiTextFont::LoadPlain(const std::string& path, int baseSize)
{
    if (!FileExists(path.c_str()))
        return;

    if (plainFontLoaded)
        UnloadFont(plainFont);

    // LoadFontEx (not LoadFont) so the rasterization size can be chosen: dense
    // form text is drawn around 13-16px, and a 32px atlas downscales cleanly.
    plainFont = LoadFontEx(path.c_str(), baseSize, nullptr, 0);
    plainFontLoaded = plainFont.texture.id != 0;
    if (plainFontLoaded)
        SetTextureFilter(plainFont.texture, TEXTURE_FILTER_BILINEAR);
}

UiFontRole UiText::SetRole(UiFontRole role)
{
    UiFontRole previous = activeRole;
    activeRole = role;
    return previous;
}

UiFontRole UiText::GetRole()
{
    return activeRole;
}

void UiTextFont::Unload()
{
    if (uiFontLoaded)
        UnloadFont(uiFont);
    uiFontLoaded = false;
    if (plainFontLoaded)
        UnloadFont(plainFont);
    plainFontLoaded = false;
}

bool UiTextFont::IsLoaded()
{
    return uiFontLoaded;
}

const Font& UiTextFont::Get()
{
    return uiFont;
}

int UiText::Measure(const std::string& text, int fontSize)
{
    Font font{};
    if (!ActiveFont(font))
        return MeasureText(text.c_str(), fontSize);

    return static_cast<int>(std::ceil(MeasureTextEx(font, text.c_str(), static_cast<float>(fontSize), 0.0f).x));
}

void UiText::Draw(const std::string& text, float x, float y, int fontSize, Color color)
{
    Font font{};
    if (ActiveFont(font))
        DrawTextEx(font, text.c_str(), {x, y}, static_cast<float>(fontSize), 0.0f, color);
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
    int titleFont = std::max(21, std::min(30, static_cast<int>(titleBar.height) / 2 + 4));
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
                   const std::function<void(Rectangle)>& titleIcon)
{
    int titleFont = 24;
    int lineFont = 20;
    float padding = 14.0f;
    float lineH = 27.0f;
    float paragraphGap = 5.0f;

    const float iconSize = titleIcon ? 36.0f : 0.0f;
    const float iconGap = titleIcon ? 8.0f : 0.0f;
    const float headerHeight = std::max(static_cast<float>(titleFont), iconSize);

    float width = std::max(240.0f, preferredWidth);
    width = std::max(width, std::min(520.0f, static_cast<float>(UiText::Measure(title, titleFont)) + padding * 2.0f + iconSize + iconGap));
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
    int visualLineCount = 0;
    for (const auto& line : lines)
    {
        if (line == "{separator}")
        {
            wrappedLines.push_back(TooltipParagraph{true, {}, line});
            visualLineCount++;
            continue;
        }

        std::string displayLine = StripTooltipInlineMarkup(StripTooltipLinePrefix(line));

        TooltipParagraph paragraph;
        paragraph.source = line;
        paragraph.lines = UiText::WrapWithControlIcons(displayLine, lineFont, textWidth, 24.0f);
        visualLineCount += static_cast<int>(paragraph.lines.size());
        wrappedLines.push_back(std::move(paragraph));
    }

    float height = padding * 2.0f + headerHeight + 8.0f +
                   std::max(1, visualLineCount) * lineH +
                   std::max(0, static_cast<int>(wrappedLines.size()) - 1) * paragraphGap;
    Vector2 mouse = GetMousePosition();
    Rectangle bounds{mouse.x + 14.0f, mouse.y + 14.0f, width, height};
    bounds.x = std::min(bounds.x, static_cast<float>(GetScreenWidth()) - bounds.width - 8.0f);
    bounds.y = std::min(bounds.y, static_cast<float>(GetScreenHeight()) - bounds.height - 8.0f);
    bounds.x = std::max(8.0f, bounds.x);
    bounds.y = std::max(8.0f, bounds.y);

    // Tooltips share the window/button chrome: steel outer bezel, recessed
    // navy body and a small header rail. This avoids the old flat brown card.
    DrawRectangleRounded(bounds, 0.05f, 8, UiTheme::Ink);
    DrawRectangleRoundedLines(bounds, 0.05f, 8, 1.4f, UiTheme::Iron);
    Rectangle inner{bounds.x + 3.0f, bounds.y + 3.0f,
                    bounds.width - 6.0f, bounds.height - 6.0f};
    DrawRectangleRounded(inner, 0.045f, 8, Fade(UiTheme::Surface, 0.99f));
    DrawRectangleRoundedLines(inner, 0.045f, 8, 1.0f, Fade(UiTheme::Bronze, 0.68f));
    if (titleIcon)
        titleIcon(Rectangle{bounds.x + padding, bounds.y + padding, iconSize, iconSize});
    UiText::Draw(title, bounds.x + padding + iconSize + iconGap,
                 bounds.y + padding + (headerHeight - titleFont) * 0.5f - 1.0f,
                 titleFont, UiTheme::Parchment);

    float y = bounds.y + padding + headerHeight + 6.0f;
    DrawLineEx(Vector2{bounds.x + padding, y - 4.0f},
               Vector2{bounds.x + bounds.width - padding, y - 4.0f},
               1.0f, Fade(UiTheme::Bronze, 0.78f));
        for (size_t paragraphIndex = 0; paragraphIndex < wrappedLines.size(); paragraphIndex++)
    {
        const auto& paragraph = wrappedLines[paragraphIndex];
        if (paragraph.separator)
        {
            y += 5.0f;
            DrawLineEx(Vector2{bounds.x + padding, y}, Vector2{bounds.x + bounds.width - padding, y}, 1.0f, Fade(UiTheme::Bronze, 0.82f));
            y += 8.0f;
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
        y += paragraphGap;
    }
}
