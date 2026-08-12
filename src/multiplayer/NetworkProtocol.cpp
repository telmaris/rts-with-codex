#include "multiplayer/NetworkProtocol.h"

#include <limits>
#include <utility>

namespace
{
    void AppendU16(std::string& output, std::uint16_t value)
    {
        output.push_back(static_cast<char>((value >> 8) & 0xFFu));
        output.push_back(static_cast<char>(value & 0xFFu));
    }

    void AppendU32(std::string& output, std::uint32_t value)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
            output.push_back(static_cast<char>((value >> shift) & 0xFFu));
    }

    void AppendU64(std::string& output, std::uint64_t value)
    {
        for (int shift = 56; shift >= 0; shift -= 8)
            output.push_back(static_cast<char>((value >> shift) & 0xFFu));
    }

    std::uint16_t ReadU16(const std::string& input, std::size_t offset)
    {
        return (static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset])) << 8) |
               static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset + 1]));
    }

    std::uint32_t ReadU32(const std::string& input, std::size_t offset)
    {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i)
            value = (value << 8) | static_cast<unsigned char>(input[offset + i]);
        return value;
    }

    std::uint64_t ReadU64(const std::string& input, std::size_t offset)
    {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i)
            value = (value << 8) | static_cast<unsigned char>(input[offset + i]);
        return value;
    }
}

bool NetworkProtocolCodec::Encode(const NetworkFrame& frame, std::string& output,
                                  std::string& error, const NetworkProtocolLimits& limits)
{
    error.clear();
    if (!IsValidChannel(frame.channel))
    {
        error = "invalid protocol channel";
        return false;
    }
    if (!IsValidMessageType(frame.type))
    {
        error = "invalid protocol message type";
        return false;
    }
    if (!IsAllowedOnChannel(frame.type, frame.channel))
    {
        error = "message type is not allowed on this channel";
        return false;
    }
    if (frame.payload.size() > MaxPayloadBytes(frame.channel, limits) ||
        frame.payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        error = "protocol payload exceeds configured limit";
        return false;
    }

    output.clear();
    output.reserve(HeaderBytes + frame.payload.size());
    AppendU32(output, Magic);
    AppendU16(output, Version);
    AppendU16(output, static_cast<std::uint16_t>(frame.type));
    output.push_back(static_cast<char>(frame.channel));
    output.push_back(static_cast<char>(frame.flags));
    AppendU16(output, 0); // Reserved; must be zero until a protocol version uses it.
    AppendU32(output, static_cast<std::uint32_t>(frame.payload.size()));
    AppendU64(output, frame.sequence);
    output.append(frame.payload);
    return true;
}

bool NetworkProtocolCodec::IsValidChannel(NetworkChannel channel)
{
    switch (channel)
    {
        case NetworkChannel::Control:
        case NetworkChannel::Command:
        case NetworkChannel::Event:
        case NetworkChannel::Snapshot:
        case NetworkChannel::Lobby:
            return true;
    }
    return false;
}

bool NetworkProtocolCodec::IsValidMessageType(NetworkMessageType type)
{
    switch (type)
    {
        case NetworkMessageType::Hello:
        case NetworkMessageType::Welcome:
        case NetworkMessageType::Ping:
        case NetworkMessageType::Pong:
        case NetworkMessageType::Disconnect:
        case NetworkMessageType::Resume:
        case NetworkMessageType::ResumeAccepted:
        case NetworkMessageType::SyncReady:
        case NetworkMessageType::ClientCommand:
        case NetworkMessageType::CommandResult:
        case NetworkMessageType::ServerFrame:
        case NetworkMessageType::SnapshotBegin:
        case NetworkMessageType::SnapshotChunk:
        case NetworkMessageType::SnapshotEnd:
        case NetworkMessageType::SnapshotAck:
        case NetworkMessageType::SnapshotNack:
        case NetworkMessageType::CreateRoom:
        case NetworkMessageType::ListRooms:
        case NetworkMessageType::RoomList:
        case NetworkMessageType::JoinRoom:
        case NetworkMessageType::LeaveRoom:
        case NetworkMessageType::RoomState:
        case NetworkMessageType::SetReady:
        case NetworkMessageType::StartMatch:
        case NetworkMessageType::LobbyChat:
            return true;
    }
    return false;
}

bool NetworkProtocolCodec::IsAllowedOnChannel(NetworkMessageType type, NetworkChannel channel)
{
    switch (channel)
    {
        case NetworkChannel::Control:
            return type == NetworkMessageType::Hello || type == NetworkMessageType::Welcome ||
                   type == NetworkMessageType::Ping || type == NetworkMessageType::Pong ||
                   type == NetworkMessageType::Disconnect || type == NetworkMessageType::Resume ||
                   type == NetworkMessageType::ResumeAccepted || type == NetworkMessageType::SyncReady;
        case NetworkChannel::Command:
            return type == NetworkMessageType::ClientCommand;
        case NetworkChannel::Event:
            return type == NetworkMessageType::CommandResult || type == NetworkMessageType::ServerFrame;
        case NetworkChannel::Snapshot:
            return type == NetworkMessageType::SnapshotBegin || type == NetworkMessageType::SnapshotChunk ||
                   type == NetworkMessageType::SnapshotEnd || type == NetworkMessageType::SnapshotAck ||
                   type == NetworkMessageType::SnapshotNack;
        case NetworkChannel::Lobby:
            return type == NetworkMessageType::CreateRoom || type == NetworkMessageType::ListRooms ||
                   type == NetworkMessageType::RoomList || type == NetworkMessageType::JoinRoom ||
                   type == NetworkMessageType::LeaveRoom || type == NetworkMessageType::RoomState ||
                   type == NetworkMessageType::SetReady || type == NetworkMessageType::StartMatch ||
                   type == NetworkMessageType::LobbyChat;
    }
    return false;
}

std::size_t NetworkProtocolCodec::MaxPayloadBytes(NetworkChannel channel, const NetworkProtocolLimits& limits)
{
    switch (channel)
    {
        case NetworkChannel::Control: return limits.maxControlPayloadBytes;
        case NetworkChannel::Command: return limits.maxCommandPayloadBytes;
        case NetworkChannel::Event: return limits.maxEventPayloadBytes;
        case NetworkChannel::Snapshot: return limits.maxSnapshotPayloadBytes;
        case NetworkChannel::Lobby: return limits.maxLobbyPayloadBytes;
    }
    return 0;
}

NetworkFrameDecoder::NetworkFrameDecoder(NetworkProtocolLimits limits) : limits(std::move(limits))
{
}

FrameDecodeResult NetworkFrameDecoder::Push(std::string_view receivedBytes, std::vector<NetworkFrame>& output)
{
    if (failed)
        return FrameDecodeResult{lastError, 0, false};

    if (receivedBytes.size() > limits.maxBufferedBytes - std::min(limits.maxBufferedBytes, buffered.size()))
        return Fail(FrameDecodeError::BufferedDataLimitExceeded, 0);

    if (!receivedBytes.empty())
        buffered.append(receivedBytes.data(), receivedBytes.size());
    std::size_t decodedFrames = 0;
    while (buffered.size() >= NetworkProtocolCodec::HeaderBytes)
    {
        if (decodedFrames >= limits.maxFramesPerPush)
            return Fail(FrameDecodeError::TooManyFramesInSinglePush, decodedFrames);

        if (ReadU32(buffered, 0) != NetworkProtocolCodec::Magic)
            return Fail(FrameDecodeError::InvalidMagic, decodedFrames);
        if (ReadU16(buffered, 4) != NetworkProtocolCodec::Version)
            return Fail(FrameDecodeError::UnsupportedVersion, decodedFrames);

        const auto type = static_cast<NetworkMessageType>(ReadU16(buffered, 6));
        const auto channel = static_cast<NetworkChannel>(static_cast<unsigned char>(buffered[8]));
        const std::uint16_t reserved = ReadU16(buffered, 10);
        const std::uint32_t payloadBytes = ReadU32(buffered, 12);
        const std::uint64_t sequence = ReadU64(buffered, 16);

        if (!NetworkProtocolCodec::IsValidMessageType(type))
            return Fail(FrameDecodeError::InvalidMessageType, decodedFrames);
        if (!NetworkProtocolCodec::IsValidChannel(channel))
            return Fail(FrameDecodeError::InvalidChannel, decodedFrames);
        if (reserved != 0)
            return Fail(FrameDecodeError::InvalidReservedBits, decodedFrames);
        if (!NetworkProtocolCodec::IsAllowedOnChannel(type, channel))
            return Fail(FrameDecodeError::MessageNotAllowedOnChannel, decodedFrames);
        if (payloadBytes > NetworkProtocolCodec::MaxPayloadBytes(channel, limits))
            return Fail(FrameDecodeError::PayloadLimitExceeded, decodedFrames);

        const std::size_t fullFrameBytes = NetworkProtocolCodec::HeaderBytes + static_cast<std::size_t>(payloadBytes);
        if (buffered.size() < fullFrameBytes)
            return FrameDecodeResult{FrameDecodeError::None, decodedFrames, true};

        NetworkFrame frame;
        frame.type = type;
        frame.channel = channel;
        frame.flags = static_cast<std::uint8_t>(static_cast<unsigned char>(buffered[9]));
        frame.sequence = sequence;
        frame.payload.assign(buffered.data() + NetworkProtocolCodec::HeaderBytes, payloadBytes);
        output.push_back(std::move(frame));
        buffered.erase(0, fullFrameBytes);
        ++decodedFrames;
    }

    return FrameDecodeResult{FrameDecodeError::None, decodedFrames, !buffered.empty()};
}

void NetworkFrameDecoder::Reset()
{
    buffered.clear();
    failed = false;
    lastError = FrameDecodeError::None;
}

FrameDecodeResult NetworkFrameDecoder::Fail(FrameDecodeError error, std::size_t decodedFrames)
{
    buffered.clear();
    failed = true;
    lastError = error;
    return FrameDecodeResult{error, decodedFrames, false};
}
