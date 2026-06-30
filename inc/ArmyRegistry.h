#ifndef ARMY_REGISTRY_H
#define ARMY_REGISTRY_H

// Aggregated army state used by HUD, stats and future army overview panels.
struct ArmyRegistry
{
    int militia{0};
    int swordsmen{0};
    int archers{0};
    int queuedMilitia{0};
    int queuedSwordsmen{0};
    int queuedArchers{0};
    int garrisonCapacity{0};
    int supply{0};
    int supplyCapacity{0};
    int supplyConsumption{0};
    int strength{0};

    int TotalTroops() const { return militia + swordsmen + archers; }
    int TotalQueued() const { return queuedMilitia + queuedSwordsmen + queuedArchers; }
};

#endif
