#include "multiplayer/SnapshotTransfer.h"

#include <algorithm>
#include <limits>
#include <new>
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

    std::uint16_t ReadU16(std::string_view input, std::size_t offset)
    {
        return (static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset])) << 8) |
               static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset + 1]));
    }

    std::uint32_t ReadU32(std::string_view input, std::size_t offset)
    {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i)
            value = (value << 8) | static_cast<unsigned char>(input[offset + i]);
        return value;
    }

    std::uint64_t ReadU64(std::string_view input, std::size_t offset)
    {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i)
            value = (value << 8) | static_cast<unsigned char>(input[offset + i]);
        return value;
    }
}

std::uint64_t SnapshotTransferCodec::ComputePayloadHash(std::string_view payload)
{
    constexpr std::uint64_t OffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t Prime = 1099511628211ull;
    std::uint64_t hash = OffsetBasis;
    for (unsigned char value : payload)
    {
        hash ^= value;
        hash *= Prime;
    }
    return hash;
}

bool SnapshotTransferCodec::IsManifestValid(const SnapshotTransferManifest& manifest,
                                            const SnapshotTransferLimits& limits)
{
    if (limits.maxChunkDataBytes == 0 || manifest.totalBytes > limits.maxTotalBytes)
        return false;

    const std::uint64_t maxSize = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    if (manifest.totalBytes > maxSize)
        return false;

    const std::uint64_t chunkBytes = static_cast<std::uint64_t>(limits.maxChunkDataBytes);
    const std::uint64_t expectedChunkCount = manifest.totalBytes == 0
        ? 0
        : 1 + (manifest.totalBytes - 1) / chunkBytes;
    return expectedChunkCount <= limits.maxChunkCount && manifest.chunkCount == expectedChunkCount;
}

std::uint32_t SnapshotTransferCodec::CalculateChunkCount(std::size_t totalBytes,
                                                         const SnapshotTransferLimits& limits)
{
    if (limits.maxChunkDataBytes == 0 || totalBytes > limits.maxTotalBytes)
        return 0;

    const std::size_t chunkCount = totalBytes == 0
        ? 0
        : 1 + (totalBytes - 1) / limits.maxChunkDataBytes;
    if (chunkCount > limits.maxChunkCount || chunkCount > std::numeric_limits<std::uint32_t>::max())
        return 0;
    return static_cast<std::uint32_t>(chunkCount);
}

bool SnapshotTransferCodec::CreateManifest(std::uint64_t transferId, std::uint64_t simulationTick,
                                           std::string_view payload, SnapshotTransferManifest& manifest,
                                           const SnapshotTransferLimits& limits)
{
    const std::uint32_t chunkCount = CalculateChunkCount(payload.size(), limits);
    if ((payload.empty() && chunkCount != 0) || (!payload.empty() && chunkCount == 0))
        return false;

    SnapshotTransferManifest created;
    created.transferId = transferId;
    created.simulationTick = simulationTick;
    created.totalBytes = static_cast<std::uint64_t>(payload.size());
    created.chunkCount = chunkCount;
    created.payloadHash = ComputePayloadHash(payload);
    if (!IsManifestValid(created, limits))
        return false;
    manifest = created;
    return true;
}

bool SnapshotTransferCodec::CreateChunk(const SnapshotTransferManifest& manifest, std::string_view payload,
                                        std::uint32_t index, SnapshotTransferChunk& chunk,
                                        const SnapshotTransferLimits& limits)
{
    if (!IsManifestValid(manifest, limits) || payload.size() != manifest.totalBytes ||
        ComputePayloadHash(payload) != manifest.payloadHash || index >= manifest.chunkCount)
    {
        return false;
    }

    const std::size_t offset = static_cast<std::size_t>(index) * limits.maxChunkDataBytes;
    const std::size_t bytes = std::min(limits.maxChunkDataBytes, payload.size() - offset);
    chunk.transferId = manifest.transferId;
    chunk.index = index;
    chunk.data.assign(payload.data() + offset, bytes);
    return true;
}

bool SnapshotTransferCodec::SerializeManifest(const SnapshotTransferManifest& manifest, std::string& output,
                                              const SnapshotTransferLimits& limits)
{
    if (!IsManifestValid(manifest, limits))
        return false;

    output.clear();
    output.reserve(ManifestBytes);
    AppendU16(output, ManifestVersion);
    AppendU64(output, manifest.transferId);
    AppendU64(output, manifest.simulationTick);
    AppendU64(output, manifest.totalBytes);
    AppendU32(output, manifest.chunkCount);
    AppendU64(output, manifest.payloadHash);
    return true;
}

SnapshotTransferError SnapshotTransferCodec::TryDeserializeManifest(std::string_view payload,
                                                                     SnapshotTransferManifest& manifest,
                                                                     const SnapshotTransferLimits& limits)
{
    if (payload.size() != ManifestBytes || ReadU16(payload, 0) != ManifestVersion)
        return SnapshotTransferError::InvalidManifestEncoding;

    SnapshotTransferManifest parsed;
    parsed.transferId = ReadU64(payload, 2);
    parsed.simulationTick = ReadU64(payload, 10);
    parsed.totalBytes = ReadU64(payload, 18);
    parsed.chunkCount = ReadU32(payload, 26);
    parsed.payloadHash = ReadU64(payload, 30);
    if (!IsManifestValid(parsed, limits))
        return SnapshotTransferError::InvalidManifest;
    manifest = parsed;
    return SnapshotTransferError::None;
}

bool SnapshotTransferCodec::SerializeChunk(const SnapshotTransferChunk& chunk, std::string& output,
                                           const SnapshotTransferLimits& limits)
{
    if (chunk.data.size() > limits.maxChunkDataBytes)
        return false;

    output.clear();
    output.reserve(ChunkHeaderBytes + chunk.data.size());
    AppendU64(output, chunk.transferId);
    AppendU32(output, chunk.index);
    output.append(chunk.data);
    return true;
}

SnapshotTransferError SnapshotTransferCodec::TryDeserializeChunk(std::string_view payload,
                                                                  SnapshotTransferChunk& chunk,
                                                                  const SnapshotTransferLimits& limits)
{
    if (payload.size() < ChunkHeaderBytes)
        return SnapshotTransferError::InvalidChunkEncoding;
    if (payload.size() - ChunkHeaderBytes > limits.maxChunkDataBytes)
        return SnapshotTransferError::ChunkExceedsLimit;

    SnapshotTransferChunk parsed;
    parsed.transferId = ReadU64(payload, 0);
    parsed.index = ReadU32(payload, 8);
    parsed.data.assign(payload.data() + ChunkHeaderBytes, payload.size() - ChunkHeaderBytes);
    chunk = std::move(parsed);
    return SnapshotTransferError::None;
}

void SnapshotTransferCodec::SerializeEnd(const SnapshotTransferEnd& end, std::string& output)
{
    output.clear();
    output.reserve(EndBytes);
    AppendU64(output, end.transferId);
    AppendU64(output, end.payloadHash);
}

SnapshotTransferError SnapshotTransferCodec::TryDeserializeEnd(std::string_view payload, SnapshotTransferEnd& end)
{
    if (payload.size() != EndBytes)
        return SnapshotTransferError::InvalidChunkEncoding;

    end.transferId = ReadU64(payload, 0);
    end.payloadHash = ReadU64(payload, 8);
    return SnapshotTransferError::None;
}

SnapshotChunkAssembler::SnapshotChunkAssembler(SnapshotTransferLimits limits) : limits(std::move(limits))
{
}

SnapshotTransferError SnapshotChunkAssembler::Begin(const SnapshotTransferManifest& newManifest)
{
    Reset();
    if (!SnapshotTransferCodec::IsManifestValid(newManifest, limits))
        return SnapshotTransferError::InvalidManifest;

    try
    {
        assembledPayload.assign(static_cast<std::size_t>(newManifest.totalBytes), '\0');
        receivedChunks.assign(newManifest.chunkCount, false);
    }
    catch (const std::bad_alloc&)
    {
        Reset();
        return SnapshotTransferError::StorageAllocationFailed;
    }

    manifest = newManifest;
    active = true;
    return SnapshotTransferError::None;
}

SnapshotTransferError SnapshotChunkAssembler::AddChunk(const SnapshotTransferChunk& chunk)
{
    if (!active)
        return SnapshotTransferError::UnexpectedTransfer;
    if (failed)
        return SnapshotTransferError::TransferAlreadyFailed;
    if (chunk.transferId != manifest.transferId)
        return SnapshotTransferError::UnexpectedTransfer;
    if (chunk.index >= manifest.chunkCount)
        return SnapshotTransferError::ChunkIndexOutOfRange;
    if (chunk.data.size() != ExpectedChunkBytes(chunk.index))
        return SnapshotTransferError::InvalidChunkSize;

    const std::size_t offset = static_cast<std::size_t>(chunk.index) * limits.maxChunkDataBytes;
    if (receivedChunks[chunk.index])
    {
        if (!std::equal(chunk.data.begin(), chunk.data.end(), assembledPayload.begin() + offset))
        {
            failed = true;
            return SnapshotTransferError::ConflictingDuplicateChunk;
        }
        return SnapshotTransferError::None;
    }

    std::copy(chunk.data.begin(), chunk.data.end(), assembledPayload.begin() + offset);
    receivedChunks[chunk.index] = true;
    ++receivedCount;
    return SnapshotTransferError::None;
}

SnapshotTransferError SnapshotChunkAssembler::Finish(const SnapshotTransferEnd& end)
{
    if (!active || end.transferId != manifest.transferId)
        return SnapshotTransferError::UnexpectedTransfer;
    if (failed)
        return SnapshotTransferError::TransferAlreadyFailed;
    if (end.payloadHash != manifest.payloadHash || receivedCount != receivedChunks.size())
        return receivedCount != receivedChunks.size() ? SnapshotTransferError::TransferIncomplete
                                                      : SnapshotTransferError::PayloadHashMismatch;
    if (SnapshotTransferCodec::ComputePayloadHash(assembledPayload) != manifest.payloadHash)
    {
        failed = true;
        return SnapshotTransferError::PayloadHashMismatch;
    }

    complete = true;
    return SnapshotTransferError::None;
}

std::vector<std::uint32_t> SnapshotChunkAssembler::GetMissingChunkIndices() const
{
    std::vector<std::uint32_t> missing;
    if (!active)
        return missing;

    missing.reserve(receivedChunks.size() - receivedCount);
    for (std::size_t index = 0; index < receivedChunks.size(); ++index)
        if (!receivedChunks[index])
            missing.push_back(static_cast<std::uint32_t>(index));
    return missing;
}

SnapshotTransferError SnapshotChunkAssembler::TakeCompletedPayload(std::string& payload)
{
    if (!complete)
        return SnapshotTransferError::TransferIncomplete;

    payload = std::move(assembledPayload);
    Reset();
    return SnapshotTransferError::None;
}

void SnapshotChunkAssembler::Reset()
{
    manifest = SnapshotTransferManifest{};
    assembledPayload.clear();
    receivedChunks.clear();
    receivedCount = 0;
    active = false;
    complete = false;
    failed = false;
}

std::size_t SnapshotChunkAssembler::ExpectedChunkBytes(std::uint32_t index) const
{
    const std::size_t offset = static_cast<std::size_t>(index) * limits.maxChunkDataBytes;
    const std::size_t total = static_cast<std::size_t>(manifest.totalBytes);
    return std::min(limits.maxChunkDataBytes, total - offset);
}
