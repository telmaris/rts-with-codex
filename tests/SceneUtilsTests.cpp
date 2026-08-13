#include "scenes/SceneUtils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    class TemporarySaveDirectory
    {
    public:
        TemporarySaveDirectory()
            : path(std::filesystem::temp_directory_path() / "rts_scene_utils_save_tests")
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
            std::filesystem::create_directories(path);
        }

        ~TemporarySaveDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        std::filesystem::path path;
    };
}

TEST(SceneUtilsTests, SaveButtonDisplaysWorldNameButSelectsFileStem)
{
    TemporarySaveDirectory saves;
    const auto savePath = saves.path / "slot_01.save";
    {
        std::ofstream save(savePath);
        ASSERT_TRUE(save.is_open());
        save << "RTS_SAVE 32\nWORLD \"Displayed World\"\n";
    }

    VBox buttons;
    std::string selectedSave;
    PopulateSaveButtons(buttons, [&](std::string name) { selectedSave = std::move(name); }, saves.path);

    ASSERT_EQ(buttons.children.size(), 1u);
    auto button = std::dynamic_pointer_cast<UiButton>(buttons.children.front());
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text, "Displayed World");

    button->OnClick();
    EXPECT_EQ(selectedSave, "slot_01");
}

TEST(SceneUtilsTests, SaveButtonFallsBackToFileStemForMalformedHeader)
{
    TemporarySaveDirectory saves;
    const auto savePath = saves.path / "recoverable_slot.save";
    {
        std::ofstream save(savePath);
        ASSERT_TRUE(save.is_open());
        save << "not a supported save header\n";
    }

    VBox buttons;
    std::string selectedSave;
    PopulateSaveButtons(buttons, [&](std::string name) { selectedSave = std::move(name); }, saves.path);

    ASSERT_EQ(buttons.children.size(), 1u);
    auto button = std::dynamic_pointer_cast<UiButton>(buttons.children.front());
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text, "recoverable_slot");

    button->OnClick();
    EXPECT_EQ(selectedSave, "recoverable_slot");
}
