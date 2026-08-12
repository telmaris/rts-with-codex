#include "multiplayer/SnapshotTransfer.h"

#include <gtest/gtest.h>

namespace
{
    std::string MakePayload(std::size_t bytes)
    {
        std::string payload(bytes, '\0');
        for (std::size_t index = 0; index < bytes; ++index)
            payload[index] = static_cast<char>(index % 251);
        return payload;
    }
}

TEST(SnapshotTransferTests, ManifestAndChunksRoundTripBinaryPayload)
{
    const std::string payload = MakePayload(100);
    SnapshotTransferManifest manifest;
    ASSERT_TRUE(SnapshotTransferCodec::CreateManifest(19, 1234, payload, manifest));

    std::string wireManifest;
    ASSERT_TRUE(SnapshotTransferCodec::SerializeManifest(manifest, wireManifest));
    SnapshotTransferManifest parsedManifest;
    ASSERT_EQ(SnapshotTransferCodec::TryDeserializeManifest(wireManifest, parsedManifest), SnapshotTransferError::None);
    EXPECT_EQ(parsedManifest.transferId, manifest.transferId);
    EXPECT_EQ(parsedManifest.simulationTick, manifest.simulationTick);
    EXPECT_EQ(parsedManifest.totalBytes, manifest.totalBytes);
    EXPECT_EQ(parsedManifest.payloadHash, manifest.payloadHash);

    SnapshotTransferChunk chunk;
    ASSERT_TRUE(SnapshotTransferCodec::CreateChunk(manifest, payload, 0, chunk));
    std::string wireChunk;
    ASSERT_TRUE(SnapshotTransferCodec::SerializeChunk(chunk, wireChunk));
    SnapshotTransferChunk parsedChunk;
    ASSERT_EQ(SnapshotTransferCodec::TryDeserializeChunk(wireChunk, parsedChunk), SnapshotTransferError::None);
    EXPECT_EQ(parsedChunk.transferId, chunk.transferId);
    EXPECT_EQ(parsedChunk.index, chunk.index);
    EXPECT_EQ(parsedChunk.data, payload);

    SnapshotTransferEnd end{manifest.transferId, manifest.payloadHash};
    std::string wireEnd;
    SnapshotTransferCodec::SerializeEnd(end, wireEnd);
    SnapshotTransferEnd parsedEnd;
    ASSERT_EQ(SnapshotTransferCodec::TryDeserializeEnd(wireEnd, parsedEnd), SnapshotTransferError::None);
    EXPECT_EQ(parsedEnd.transferId, end.transferId);
    EXPECT_EQ(parsedEnd.payloadHash, end.payloadHash);
}

TEST(SnapshotTransferTests, AssemblesChunksOutOfOrderAndTracksMissingChunks)
{
    const std::size_t chunkBytes = SnapshotTransferLimits::DefaultChunkDataBytes;
    const std::string payload = MakePayload(chunkBytes * 2 + 7);
    SnapshotTransferManifest manifest;
    ASSERT_TRUE(SnapshotTransferCodec::CreateManifest(4, 88, payload, manifest));
    ASSERT_EQ(manifest.chunkCount, 3u);

    SnapshotChunkAssembler assembler;
    ASSERT_EQ(assembler.Begin(manifest), SnapshotTransferError::None);

    SnapshotTransferChunk second;
    SnapshotTransferChunk first;
    SnapshotTransferChunk third;
    ASSERT_TRUE(SnapshotTransferCodec::CreateChunk(manifest, payload, 1, second));
    ASSERT_TRUE(SnapshotTransferCodec::CreateChunk(manifest, payload, 0, first));
    ASSERT_TRUE(SnapshotTransferCodec::CreateChunk(manifest, payload, 2, third));
    EXPECT_EQ(assembler.AddChunk(second), SnapshotTransferError::None);
    EXPECT_EQ(assembler.GetMissingChunkIndices(), (std::vector<std::uint32_t>{0, 2}));
    EXPECT_EQ(assembler.AddChunk(first), SnapshotTransferError::None);
    EXPECT_EQ(assembler.AddChunk(third), SnapshotTransferError::None);
    EXPECT_FALSE(assembler.IsComplete());
    EXPECT_EQ(assembler.Finish(SnapshotTransferEnd{manifest.transferId, manifest.payloadHash}), SnapshotTransferError::None);
    EXPECT_TRUE(assembler.IsComplete());

    std::string completed;
    ASSERT_EQ(assembler.TakeCompletedPayload(completed), SnapshotTransferError::None);
    EXPECT_EQ(completed, payload);
    EXPECT_FALSE(assembler.IsActive());
}

TEST(SnapshotTransferTests, AcceptsIdenticalDuplicateAndRejectsConflictingDuplicate)
{
    const std::string payload = MakePayload(25);
    SnapshotTransferManifest manifest;
    ASSERT_TRUE(SnapshotTransferCodec::CreateManifest(7, 0, payload, manifest));
    SnapshotTransferChunk original;
    ASSERT_TRUE(SnapshotTransferCodec::CreateChunk(manifest, payload, 0, original));

    SnapshotChunkAssembler duplicateAssembler;
    ASSERT_EQ(duplicateAssembler.Begin(manifest), SnapshotTransferError::None);
    EXPECT_EQ(duplicateAssembler.AddChunk(original), SnapshotTransferError::None);
    EXPECT_EQ(duplicateAssembler.AddChunk(original), SnapshotTransferError::None);

    SnapshotChunkAssembler conflictingAssembler;
    ASSERT_EQ(conflictingAssembler.Begin(manifest), SnapshotTransferError::None);
    SnapshotTransferChunk first = original;
    EXPECT_EQ(conflictingAssembler.AddChunk(first), SnapshotTransferError::None);
    SnapshotTransferChunk conflicting = original;
    conflicting.data[0] ^= 0x01;
    EXPECT_EQ(conflictingAssembler.AddChunk(conflicting), SnapshotTransferError::ConflictingDuplicateChunk);
}

TEST(SnapshotTransferTests, RejectsPayloadWithIncorrectHashAfterFinalChunk)
{
    const std::string payload = MakePayload(32);
    SnapshotTransferManifest manifest;
    ASSERT_TRUE(SnapshotTransferCodec::CreateManifest(8, 0, payload, manifest));
    manifest.payloadHash ^= 1;

    SnapshotTransferChunk chunk;
    chunk.transferId = manifest.transferId;
    chunk.index = 0;
    chunk.data = payload;
    SnapshotChunkAssembler assembler;
    ASSERT_EQ(assembler.Begin(manifest), SnapshotTransferError::None);
    EXPECT_EQ(assembler.AddChunk(chunk), SnapshotTransferError::None);
    EXPECT_EQ(assembler.Finish(SnapshotTransferEnd{manifest.transferId, manifest.payloadHash}),
              SnapshotTransferError::PayloadHashMismatch);
    EXPECT_FALSE(assembler.IsComplete());
}

TEST(SnapshotTransferTests, FinishReportsMissingChunksForNack)
{
    const std::string payload = MakePayload(SnapshotTransferLimits::DefaultChunkDataBytes + 1);
    SnapshotTransferManifest manifest;
    ASSERT_TRUE(SnapshotTransferCodec::CreateManifest(14, 0, payload, manifest));
    SnapshotTransferChunk first;
    ASSERT_TRUE(SnapshotTransferCodec::CreateChunk(manifest, payload, 0, first));

    SnapshotChunkAssembler assembler;
    ASSERT_EQ(assembler.Begin(manifest), SnapshotTransferError::None);
    ASSERT_EQ(assembler.AddChunk(first), SnapshotTransferError::None);
    EXPECT_EQ(assembler.Finish(SnapshotTransferEnd{manifest.transferId, manifest.payloadHash}),
              SnapshotTransferError::TransferIncomplete);
    EXPECT_EQ(assembler.GetMissingChunkIndices(), (std::vector<std::uint32_t>{1}));
}

TEST(SnapshotTransferTests, RejectsManifestAndChunkBeyondConfiguredLimits)
{
    SnapshotTransferLimits limits;
    limits.maxTotalBytes = 64;
    limits.maxChunkDataBytes = 16;
    limits.maxChunkCount = 4;

    const std::string oversizedPayload(65, 'x');
    SnapshotTransferManifest manifest;
    EXPECT_FALSE(SnapshotTransferCodec::CreateManifest(1, 0, oversizedPayload, manifest, limits));

    SnapshotTransferChunk oversizedChunk;
    oversizedChunk.data.assign(17, 'x');
    std::string wire;
    EXPECT_FALSE(SnapshotTransferCodec::SerializeChunk(oversizedChunk, wire, limits));
}
