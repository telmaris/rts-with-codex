#ifndef NETWORK_PROTOCOL_H
#define NETWORK_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Versioned, length-prefixed protocol shared by direct TCP, a VPS relay and a
// future Steam transport. The codec deliberately has no dependency on sockets,
// GameWorld, raylib or lobby/UI code.
enum class NetworkChannel : std::uint8_t
{
    Control = 1,
    Command = 2,
    Event = 3,
    Snapshot = 4,
    Lobby = 5
};

enum class NetworkMessageType : std::uint16_t
{
    Hello = 1,
    Welcome = 2,
    Ping = 3,
    Pong = 4,
    Disconnect = 5,
    Resume = 6,
    ResumeAccepted = 7,
    SyncReady = 8,

    ClientCommand = 20,
    CommandResult = 30,
    ServerFrame = 31,

    SnapshotBegin = 40,
    SnapshotChunk = 41,
    SnapshotEnd = 42,
    SnapshotAck = 43,
    SnapshotNack = 44,

    CreateRoom = 60,
    ListRooms = 61,
    RoomList = 62,
    JoinRoom = 63,
    LeaveRoom = 64,
    RoomState = 65,
    SetReady = 66,
    StartMatch = 67,
    LobbyChat = 68
};

struct NetworkFrame
{
    NetworkMessageType type{NetworkMessageType::Hello};
    NetworkChannel channel{NetworkChannel::Control};
    std::uint8_t flags{0};
    std::uint64_t sequence{0};
    std::string payload;
};

struct NetworkProtocolLimits
{
    std::size_t maxControlPayloadBytes{8 * 1024};
    std::size_t maxCommandPayloadBytes{64 * 1024};
    std::size_t maxEventPayloadBytes{256 * 1024};
    std::size_t maxSnapshotPayloadBytes{32 * 1024};
    std::size_t maxLobbyPayloadBytes{16 * 1024};
    std::size_t maxBufferedBytes{1024 * 1024};
    std::size_t maxFramesPerPush{256};
};

enum class FrameDecodeError
{
    None,
    BufferedDataLimitExceeded,
    TooManyFramesInSinglePush,
    InvalidMagic,
    UnsupportedVersion,
    InvalidMessageType,
    InvalidChannel,
    InvalidReservedBits,
    MessageNotAllowedOnChannel,
    PayloadLimitExceeded
};

struct FrameDecodeResult
{
    FrameDecodeError error{FrameDecodeError::None};
    std::size_t decodedFrames{0};
    bool needsMoreData{false};

    explicit operator bool() const { return error == FrameDecodeError::None; }
};

class NetworkProtocolCodec
{
public:
    static constexpr std::uint32_t Magic = 0x5254534Eu; // "RTSN"
    static constexpr std::uint16_t Version = 1;
    static constexpr std::size_t HeaderBytes = 24;

    static bool Encode(const NetworkFrame& frame, std::string& output,
                       std::string& error, const NetworkProtocolLimits& limits = {});
    static bool IsValidChannel(NetworkChannel channel);
    static bool IsValidMessageType(NetworkMessageType type);
    static bool IsAllowedOnChannel(NetworkMessageType type, NetworkChannel channel);
    static std::size_t MaxPayloadBytes(NetworkChannel channel, const NetworkProtocolLimits& limits);
};

// Maintains the partial bytes left by a TCP recv stream. A decode error is
// terminal for that connection; callers should close it and call Reset only for
// a new connection.
class NetworkFrameDecoder
{
public:
    explicit NetworkFrameDecoder(NetworkProtocolLimits limits = {});

    FrameDecodeResult Push(std::string_view receivedBytes, std::vector<NetworkFrame>& output);
    void Reset();
    bool HasFailed() const { return failed; }
    FrameDecodeError GetLastError() const { return lastError; }
    std::size_t GetBufferedByteCount() const { return buffered.size(); }

private:
    FrameDecodeResult Fail(FrameDecodeError error, std::size_t decodedFrames);

    NetworkProtocolLimits limits;
    std::string buffered;
    bool failed{false};
    FrameDecodeError lastError{FrameDecodeError::None};
};

#endif
