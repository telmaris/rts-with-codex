#include "scenes/Scenes.h"

// Initializes OptionsScene::OptionsScene.
OptionsScene::OptionsScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.5f, 0.7f});
    backButton.func = std::bind(&OptionsScene::OnBackPressed, this);

    fullScreenCheckBox.ChangeText("Fullscreen");
    fullScreenCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.4f});

    masterVolume.ChangeText("Master Volume");
    masterVolume.ChangePositionAnchor(Vec2f{0.4f, 0.48f});

    musicVolume.ChangeText("Music Volume");
    musicVolume.ChangePositionAnchor(Vec2f{0.4f, 0.55f});

    sfxVolume.ChangeText("SFX Volume");
    sfxVolume.ChangePositionAnchor(Vec2f{0.4f, 0.62f});
}

// Advances this object's state for one frame.
void OptionsScene::Update(double dt)
{
    render.Draw({&backButton, &fullScreenCheckBox, &masterVolume, &musicVolume, &sfxVolume}, dt);

    if (fullScreenCheckBox.HasChanged())
    {
        auto msg = std::make_shared<ToggleFullscreenEvent>();
        msg->sender = this;
        broker->Broadcast(msg);
    }

    bool volumeChanged = false;
    if (masterVolume.HasChanged())
    {
        if (audioSystem != nullptr)
            audioSystem->SetMasterVolume(masterVolume.GetValue());
        volumeChanged = true;
    }
    if (musicVolume.HasChanged())
    {
        if (audioSystem != nullptr)
            audioSystem->SetMusicVolume(musicVolume.GetValue());
        volumeChanged = true;
    }
    if (sfxVolume.HasChanged())
    {
        if (audioSystem != nullptr)
            audioSystem->SetSfxVolume(sfxVolume.GetValue());
        volumeChanged = true;
    }

    if (volumeChanged && audioSystem != nullptr)
    {
        SaveAudioConfig(AudioConfig{
            audioSystem->GetMasterVolume(),
            audioSystem->GetMusicVolume(),
            audioSystem->GetSfxVolume()});
    }
}

// Syncs the volume sliders to the current audio system state.
void OptionsScene::OnActivated()
{
    if (audioSystem != nullptr)
    {
        masterVolume.currentValue  = audioSystem->GetMasterVolume();
        masterVolume.previousValue = masterVolume.currentValue;
        musicVolume.currentValue   = audioSystem->GetMusicVolume();
        musicVolume.previousValue  = musicVolume.currentValue;
        sfxVolume.currentValue     = audioSystem->GetSfxVolume();
        sfxVolume.previousValue    = sfxVolume.currentValue;
    }
}

// Handles the UI action represented by OnBackPressed.
void OptionsScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the requested event or transfer.
void OptionsScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        fullScreenCheckBox.UpdateSize(ptr->windowSize);
        masterVolume.UpdateSize(ptr->windowSize);
        musicVolume.UpdateSize(ptr->windowSize);
        sfxVolume.UpdateSize(ptr->windowSize);
    }
}
