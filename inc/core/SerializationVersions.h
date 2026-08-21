#ifndef SERIALIZATION_VERSIONS_H
#define SERIALIZATION_VERSIONS_H

// The only source of version numbers for the public wire and state formats.
// Keep format compatibility decisions next to the number instead of duplicating
// literals in individual message/state classes.
struct SerializationVersion
{
    static constexpr int GameCommandVersion = 15; // removed client-controlled build payment bit
    static constexpr int GameCommandResultVersion = 4;
    static constexpr int GameServerFrameVersion = 1;
    static constexpr int GameSnapshotVersion = 14;
    static constexpr int GameWorldSaveVersion = 34;
};

#endif
