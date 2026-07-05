#include "ui/Renderer.h"

#include <gtest/gtest.h>

// ResolveAnimationFrame is a pure function of clip data + elapsed time, so it
// is testable without a raylib window (unlike texture loading/drawing).

TEST(RendererAnimationTests, StaticClipAlwaysReturnsStartFrame)
{
    AnimationClip clip{5, 1, 0.1f, true};

    EXPECT_EQ(ResolveAnimationFrame(clip, 0.0f), 5);
    EXPECT_EQ(ResolveAnimationFrame(clip, 100.0f), 5);
}

TEST(RendererAnimationTests, LoopingClipAdvancesThroughFrames)
{
    AnimationClip clip{0, 4, 0.1f, true};

    EXPECT_EQ(ResolveAnimationFrame(clip, 0.0f), 0);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.15f), 1);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.25f), 2);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.35f), 3);
}

TEST(RendererAnimationTests, LoopingClipWrapsAroundTotalDuration)
{
    AnimationClip clip{0, 4, 0.1f, true};  // Total duration = 0.4s

    // 0.45s should wrap to the same frame as 0.05s (frame 0).
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.45f), ResolveAnimationFrame(clip, 0.05f));
}

TEST(RendererAnimationTests, NonLoopingClipClampsAtLastFrame)
{
    AnimationClip clip{0, 4, 0.1f, false};

    EXPECT_EQ(ResolveAnimationFrame(clip, 10.0f), 3);  // Well past total duration, stays on last frame.
}

TEST(RendererAnimationTests, StartFrameIdOffsetsAllFrames)
{
    AnimationClip clip{10, 3, 0.1f, true};

    EXPECT_EQ(ResolveAnimationFrame(clip, 0.0f), 10);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.15f), 11);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.25f), 12);
}
