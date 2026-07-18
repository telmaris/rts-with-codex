#include "data/Resource.h"

#include <gtest/gtest.h>

// The resource pool backing ResourceBuffer::GenerateResource/FreeResource is
// a single process-wide static (src/data/Resource.cpp) shared by every
// GameWorld built in this test binary. Real gameplay never runs more than
// one host/client pair sharing a process's worth of GameWorlds without
// deliberately tearing one down first, so a leaked-on-destroy resource
// instance is invisible there. This test binary is different: it constructs
// hundreds of short-lived GameWorlds back to back, and any that don't free
// every resource they hold before going out of scope leave that type's pool
// permanently smaller for every test that runs after. A test that then
// builds two structurally identical worlds (e.g.
// UtilityAIModelTests.TwoWorldsSameSeedWithNoisyAIStayInSync) and expects
// identical starting stock can silently break: whichever world's
// GenerateResource() calls run first that tick gets first pick of what's
// left in a partially-exhausted type, so the second gets a truncated grant
// — a real divergence, not a checksum artifact (root-caused 2026-07-18,
// see docs/tech_debt.md).
//
// Resetting the pool to full before every test case restores per-test
// isolation without touching the shared-singleton architecture itself
// (that's a separate, larger refactor — see docs/tech_debt.md).
namespace
{
    class ResourcePoolResetListener : public ::testing::EmptyTestEventListener
    {
        void OnTestStart(const ::testing::TestInfo&) override
        {
            ResetResourcePool();
        }
    };

    struct ResourcePoolResetListenerRegistrar
    {
        ResourcePoolResetListenerRegistrar()
        {
            ::testing::UnitTest::GetInstance()->listeners().Append(new ResourcePoolResetListener());
        }
    };

    ResourcePoolResetListenerRegistrar resourcePoolResetListenerRegistrar;
}
