#include "scenes/SceneUtils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>

std::string SanitizeSaveName(std::string name)
{
    if (name.empty() || name == "Default textbox text")
        return "default";

    for (auto& c : name)
    {
        bool valid = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
        if (!valid)
            c = '_';
    }
    return name;
}

std::string ReadSaveDisplayName(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return path.stem().string();

    std::string tag;
    int version = 0;
    file >> tag >> version;
    if (tag == "RTS_SAVE")
    {
        std::string worldTag;
        std::string worldName;
        file >> worldTag >> std::quoted(worldName);
        if (worldTag == "WORLD" && !worldName.empty())
            return worldName;
    }

    return path.stem().string();
}

bool SaveExists(const std::string& saveName)
{
    return std::filesystem::exists(std::filesystem::path("saves") / (SanitizeSaveName(saveName) + ".save"));
}

void PopulateSaveButtons(VBox& saveButtons, const std::function<void(std::string)>& onSavePressed,
                         const std::filesystem::path& root)
{
    namespace fs = std::filesystem;
    saveButtons.ClearChildren();

    if (!fs::exists(root))
    {
        fs::create_directories(root);
        return;
    }

    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".save")
            continue;

        const std::string displayName = ReadSaveDisplayName(entry.path());
        const std::string saveName = entry.path().stem().string();

        auto button = std::make_shared<UiButton>();
        button->ChangeText(displayName);
        button->func = [onSavePressed, saveName]()
        {
            onSavePressed(saveName);
        };

        saveButtons.AddChild(button);
    }
}

std::string MapSizeName(MapSizePreset preset)
{
    switch (preset)
    {
        case MapSizePreset::S: return "S 301x301";
        case MapSizePreset::M: return "M 501x501";
        case MapSizePreset::L: return "L 701x701";
        case MapSizePreset::XL: return "XL 1001x1001";
        default: return "S 301x301";
    }
}

std::string DifficultyName(int difficulty)
{
    switch (difficulty)
    {
        case 1: return "Easy";
        case 2: return "Normal";
        case 3: return "Hard";
        default: return "Primitive";
    }
}

int SliderToInt(float value, int minValue, int maxValue)
{
    return minValue + static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * (maxValue - minValue)));
}
