#include "multiplayer/NetworkProtocol.h"

#include <gtest/gtest.h>

namespace
{
    std::string EncodeOrFail(const NetworkFrame& frame)
    {
        std::string encoded;
        std::string error;
        EXPECT_TRUE(NetworkProtocolCodec::Encode(frame, encoded, error)) << error;
        return encoded;
    }

    void SetU16(std::string& bytes, std::size_t offset, std::uint16_t value)
    {
        bytes[offset] = static_cast<char>((value >> 8) & 0xFFu);
        bytes[offset + 1] = static_cast<char>(value & 0xFFu);
    }

    void SetU32(std::string& bytes, std::size_t offset, std::uint32_t value)
    {
        for (int shift = 24, index = 0; shift >= 0; shift -= 8, ++index)
            bytes[offset + static_cast<std::size_t>(index)] = static_cast<char>((value >> shift) & 0xFFu);
    }
}

TEST(NetworkProtocolTests, RoundTripsBinaryPayloadAcrossPartialReceives)
{
    NetworkFrame expected;
    expected.type = NetworkMessageType::ClientCommand;
    expected.channel = NetworkChannel::Command;
    expected.flags = 0xA5;
    expected.sequence = 42;
    expected.payload = std::string{"build\0tower", 11};

    const std::string encoded = EncodeOrFail(expected);
    NetworkFrameDecoder decoder;
    std::vector<NetworkFrame> frames;

    const FrameDecodeResult first = decoder.Push(std::string_view(encoded).substr(0, 9), frames);
    ASSERT_TRUE(first);
    EXPECT_TRUE(first.needsMoreData);
    EXPECT_TRUE(frames.empty());

    const FrameDecodeResult second = decoder.Push(std::string_view(encoded).substr(9), frames);
    ASSERT_TRUE(second);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].type, expected.type);
    EXPECT_EQ(frames[0].channel, expected.channel);
    EXPECT_EQ(frames[0].flags, expected.flags);
    EXPECT_EQ(frames[0].sequence, expected.sequence);
    EXPECT_EQ(frames[0].payload, expected.payload);
}

TEST(NetworkProtocolTests, DecodesMultipleFramesFromOneReceive)
{
    NetworkFrame ping{NetworkMessageType::Ping, NetworkChannel::Control, 0, 1, "ping"};
    NetworkFrame result{NetworkMessageType::CommandResult, NetworkChannel::Event, 0, 2, "accepted"};
    std::string combined = EncodeOrFail(ping);
    combined += EncodeOrFail(result);

    NetworkFrameDecoder decoder;
    std::vector<NetworkFrame> frames;
    const FrameDecodeResult decodeResult = decoder.Push(combined, frames);

    ASSERT_TRUE(decodeResult);
    ASSERT_EQ(decodeResult.decodedFrames, 2u);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].payload, "ping");
    EXPECT_EQ(frames[1].payload, "accepted");
}

TEST(NetworkProtocolTests, RejectsHeaderWithBadMagicBeforePayloadArrives)
{
    NetworkFrame frame{NetworkMessageType::Hello, NetworkChannel::Control, 0, 0, {}};
    std::string encoded = EncodeOrFail(frame);
    encoded[0] = '\0';

    NetworkFrameDecoder decoder;
    std::vector<NetworkFrame> frames;
    const FrameDecodeResult result = decoder.Push(encoded, frames);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, FrameDecodeError::InvalidMagic);
    EXPECT_TRUE(decoder.HasFailed());
    EXPECT_TRUE(frames.empty());
}

TEST(NetworkProtocolTests, RejectsPayloadClaimLargerThanTheChannelLimit)
{
    NetworkFrame frame{NetworkMessageType::SnapshotChunk, NetworkChannel::Snapshot, 0, 4, {}};
    std::string encoded = EncodeOrFail(frame);
    SetU32(encoded, 12, 32 * 1024 + 1);

    NetworkFrameDecoder decoder;
    std::vector<NetworkFrame> frames;
    const FrameDecodeResult result = decoder.Push(encoded, frames);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, FrameDecodeError::PayloadLimitExceeded);
    EXPECT_TRUE(decoder.HasFailed());
}

TEST(NetworkProtocolTests, RejectsTypeOnTheWrongLogicalChannel)
{
    NetworkFrame invalid;
    invalid.type = NetworkMessageType::ClientCommand;
    invalid.channel = NetworkChannel::Lobby;

    std::string encoded;
    std::string error;
    EXPECT_FALSE(NetworkProtocolCodec::Encode(invalid, encoded, error));
    EXPECT_EQ(error, "message type is not allowed on this channel");
}

TEST(NetworkProtocolTests, RejectsInputThatWouldExceedTheBufferedDataLimit)
{
    NetworkProtocolLimits limits;
    limits.maxBufferedBytes = NetworkProtocolCodec::HeaderBytes + 4;
    NetworkFrameDecoder decoder(limits);
    std::vector<NetworkFrame> frames;
    const std::string excessive(limits.maxBufferedBytes + 1, 'x');

    const FrameDecodeResult result = decoder.Push(excessive, frames);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, FrameDecodeError::BufferedDataLimitExceeded);
    EXPECT_TRUE(decoder.HasFailed());
}

TEST(NetworkProtocolTests, RejectsUnsupportedProtocolVersion)
{
    NetworkFrame frame{NetworkMessageType::Hello, NetworkChannel::Control, 0, 0, {}};
    std::string encoded = EncodeOrFail(frame);
    SetU16(encoded, 4, NetworkProtocolCodec::Version + 1);

    NetworkFrameDecoder decoder;
    std::vector<NetworkFrame> frames;
    const FrameDecodeResult result = decoder.Push(encoded, frames);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, FrameDecodeError::UnsupportedVersion);
}
