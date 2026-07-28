#include "scenes/Scenes.h"

// Initializes OptionsScene::OptionsScene.
OptionsScene::OptionsScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.5f, 0.7f});
    backButton.func = std::bind(&OptionsScene::OnBackPressed, this);

    fullScreenCheckBox.ChangeText("Fullscreen");
    fullScreenCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.17f});

    masterVolume.ChangeText("Master Volume");
    masterVolume.ChangePositionAnchor(Vec2f{0.4f, 0.24f});

    musicVolume.ChangeText("Music Volume");
    musicVolume.ChangePositionAnchor(Vec2f{0.4f, 0.31f});

    sfxVolume.ChangeText("SFX Volume");
    sfxVolume.ChangePositionAnchor(Vec2f{0.4f, 0.38f});

    fogOfWarCheckBox.ChangeText("Fog of War (pilot)");
    fogOfWarCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.45f});

    colorGradingCheckBox.ChangeText("World color grading");
    colorGradingCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.52f});

    retroFilterCheckBox.ChangeText("Retro world filter");
    retroFilterCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.59f});

    localLightBloomCheckBox.ChangeText("Local light bloom");
    localLightBloomCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.66f});

    rainOverlayCheckBox.ChangeText("Rain overlay (visual pilot)");
    rainOverlayCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.73f});

    logisticsOverlayCheckBox.ChangeText("Logistics load overlay (pilot)");
    logisticsOverlayCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.80f});

    backButton.ChangePositionAnchor(Vec2f{0.5f, 0.91f});
}

// Advances this object's state for one frame.
void OptionsScene::Update(double dt)
{
    ProcessGuiInput(dt);
    render.Draw({&backButton, &fullScreenCheckBox, &masterVolume, &musicVolume, &sfxVolume,
        &fogOfWarCheckBox, &colorGradingCheckBox, &retroFilterCheckBox,
        &localLightBloomCheckBox, &rainOverlayCheckBox, &logisticsOverlayCheckBox}, dt);

    if (fullScreenCheckBox.HasChanged())
    {
        auto msg = std::make_shared<ToggleFullscreenEvent>();
        msg->sender = this;
        broker->Broadcast(msg);
    }

    if (fogOfWarCheckBox.HasChanged())
        SetFogOfWarPreferenceEnabled(fogOfWarCheckBox.IsActive());
    if (colorGradingCheckBox.HasChanged())
        SetColorGradingPreferenceEnabled(colorGradingCheckBox.IsActive());
    if (retroFilterCheckBox.HasChanged())
        SetRetroFilterPreferenceEnabled(retroFilterCheckBox.IsActive());
    if (localLightBloomCheckBox.HasChanged())
        SetLocalLightBloomPreferenceEnabled(localLightBloomCheckBox.IsActive());
    if (rainOverlayCheckBox.HasChanged())
        SetRainOverlayPreferenceEnabled(rainOverlayCheckBox.IsActive());
    if (logisticsOverlayCheckBox.HasChanged())
        SetLogisticsOverlayPreferenceEnabled(logisticsOverlayCheckBox.IsActive());

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
    fogOfWarCheckBox.currentState = IsFogOfWarPreferenceEnabled();
    fogOfWarCheckBox.previousState = fogOfWarCheckBox.currentState;
    colorGradingCheckBox.currentState = IsColorGradingPreferenceEnabled();
    colorGradingCheckBox.previousState = colorGradingCheckBox.currentState;
    retroFilterCheckBox.currentState = IsRetroFilterPreferenceEnabled();
    retroFilterCheckBox.previousState = retroFilterCheckBox.currentState;
    localLightBloomCheckBox.currentState = IsLocalLightBloomPreferenceEnabled();
    localLightBloomCheckBox.previousState = localLightBloomCheckBox.currentState;
    rainOverlayCheckBox.currentState = IsRainOverlayPreferenceEnabled();
    rainOverlayCheckBox.previousState = rainOverlayCheckBox.currentState;
    logisticsOverlayCheckBox.currentState = IsLogisticsOverlayPreferenceEnabled();
    logisticsOverlayCheckBox.previousState = logisticsOverlayCheckBox.currentState;

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
        fogOfWarCheckBox.UpdateSize(ptr->windowSize);
        colorGradingCheckBox.UpdateSize(ptr->windowSize);
        retroFilterCheckBox.UpdateSize(ptr->windowSize);
        localLightBloomCheckBox.UpdateSize(ptr->windowSize);
        rainOverlayCheckBox.UpdateSize(ptr->windowSize);
        logisticsOverlayCheckBox.UpdateSize(ptr->windowSize);
    }
}
