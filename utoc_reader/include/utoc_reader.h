#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <filesystem>
#include <optional>

namespace utoc {

// Forward declarations
class UtocReader;

// Enumerations
enum class EIoStoreTocVersion : uint8_t {
    Invalid = 0,
    Initial = 1,
    DirectoryIndex = 2,
    PartitionSize = 3,
    PerfectHash = 4,
    PerfectHashWithOverflow = 5,
    OnDemandMetaData = 6,
    RemovedOnDemandMetaData = 7,
    ReplaceIoChunkHashWithIoHash = 8
};

enum class EIoChunkType : uint8_t {
    Invalid = 0,
    ExportBundleData = 1,
    BulkData = 2,
    OptionalBulkData = 3,
    MemoryMappedBulkData = 4,
    ScriptObjects = 5,
    ContainerHeader = 6,
    ExternalFile = 7,
    ShaderCodeLibrary = 8,
    ShaderCode = 9,
    PackageStoreEntry = 10,
    DerivedData = 11,
    EditorDerivedData = 12,
    PackageResource = 13
};

enum class EIoContainerFlags : uint8_t {
    None = 0,
    Compressed = 1 << 0,
    Encrypted = 1 << 1,
    Signed = 1 << 2,
    Indexed = 1 << 3
};

inline EIoContainerFlags operator|(EIoContainerFlags a, EIoContainerFlags b) {
    return static_cast<EIoContainerFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline EIoContainerFlags operator&(EIoContainerFlags a, EIoContainerFlags b) {
    return static_cast<EIoContainerFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool operator!(EIoContainerFlags a) {
    return static_cast<uint8_t>(a) == 0;
}

// Structures
struct FIoChunkId {
    uint8_t id[12];

    uint64_t GetChunkId() const;
    uint16_t GetChunkIndex() const;
    EIoChunkType GetChunkType() const;
    bool HasVersionInfo() const;
};

struct FIoOffsetAndLength {
    uint8_t data[10];

    uint64_t GetOffset() const;
    uint64_t GetLength() const;
};

struct FIoChunkHash {
    uint8_t hash[32];
};

struct FIoStoreTocEntryMetaFlags {
    static constexpr uint8_t Compressed = 1 << 0;
    static constexpr uint8_t MemoryMapped = 1 << 1;
};

struct FIoStoreTocEntryMeta {
    FIoChunkHash chunk_hash;
    uint8_t flags;

    bool IsCompressed() const { return (flags & FIoStoreTocEntryMetaFlags::Compressed) != 0; }
    bool IsMemoryMapped() const { return (flags & FIoStoreTocEntryMetaFlags::MemoryMapped) != 0; }
};

struct FIoStoreTocCompressedBlockEntry {
    uint8_t data[12];

    uint64_t GetOffset() const;
    uint32_t GetCompressedSize() const;
    uint32_t GetUncompressedSize() const;
    uint8_t GetCompressionMethodIndex() const;
};

struct FIoDirectoryIndexEntry {
    std::optional<uint32_t> name;
    std::optional<uint32_t> first_child_entry;
    std::optional<uint32_t> next_sibling_entry;
    std::optional<uint32_t> first_file_entry;
};

struct FIoFileIndexEntry {
    uint32_t name;
    std::optional<uint32_t> next_file_entry;
    uint32_t user_data;
};

struct FIoDirectoryIndexResource {
    std::string mount_point;
    std::vector<FIoDirectoryIndexEntry> directory_entries;
    std::vector<FIoFileIndexEntry> file_entries;
    std::vector<std::string> string_table;

    // Helper function to get all file paths
    std::vector<std::string> GetAllFilePaths() const;
};

struct FIoStoreTocHeader {
    static constexpr uint8_t MAGIC[16] = {'-', '=', '=', '-', '-', '=', '=', '-', '-', '=', '=', '-', '-', '=', '=', '-'};
    
    uint8_t toc_magic[16];
    EIoStoreTocVersion version;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t toc_header_size;
    uint32_t toc_entry_count;
    uint32_t toc_compressed_block_entry_count;
    uint32_t toc_compressed_block_entry_size;
    uint32_t compression_method_name_count;
    uint32_t compression_method_name_length;
    uint32_t compression_block_size;
    uint32_t directory_index_size;
    uint32_t partition_count;
    uint64_t container_id;
    uint8_t encryption_key_guid[16];
    EIoContainerFlags container_flags;
    uint8_t reserved3;
    uint16_t reserved4;
    uint32_t toc_chunk_perfect_hash_seeds_count;
    uint64_t partition_size;
    uint32_t toc_chunks_without_perfect_hash_count;
    uint32_t reserved7;
    uint64_t reserved8[5];

    bool IsValid() const;
    bool IsEncrypted() const { return (container_flags & EIoContainerFlags::Encrypted) != EIoContainerFlags::None; }
    bool IsSigned() const { return (container_flags & EIoContainerFlags::Signed) != EIoContainerFlags::None; }
    bool IsIndexed() const { return (container_flags & EIoContainerFlags::Indexed) != EIoContainerFlags::None; }
    bool IsCompressed() const { return (container_flags & EIoContainerFlags::Compressed) != EIoContainerFlags::None; }
};

// Main UTOC reader class
class UtocReader {
public:
    UtocReader() = default;
    ~UtocReader() = default;

    // Disable copy
    UtocReader(const UtocReader&) = delete;
    UtocReader& operator=(const UtocReader&) = delete;

    // Enable move
    UtocReader(UtocReader&&) = default;
    UtocReader& operator=(UtocReader&&) = default;

    // Open a UTOC file
    bool Open(const std::filesystem::path& path);

    // Get the directory index
    const FIoDirectoryIndexResource& GetDirectoryIndex() const { return directory_index_; }

    // Get all file paths in the UTOC
    std::vector<std::string> GetAllFilePaths() const;

    // Get the TOC header
    const FIoStoreTocHeader& GetHeader() const { return header_; }

private:
    // Parse the directory index
    bool ParseDirectoryIndex(const std::vector<uint8_t>& data);

    // Read optional value
    template<typename T>
    std::optional<T> ReadOptional(const uint8_t* data, size_t& offset);

    // Read string
    std::string ReadString(const uint8_t* data, size_t& offset);

    FIoStoreTocHeader header_;
    std::vector<FIoChunkId> chunk_ids_;
    std::vector<FIoOffsetAndLength> chunk_offset_lengths_;
    std::vector<int32_t> chunk_perfect_hash_seeds_;
    std::vector<int32_t> chunk_indices_without_perfect_hash_;
    std::vector<FIoStoreTocCompressedBlockEntry> compression_blocks_;
    std::vector<std::string> compression_methods_;
    std::vector<FIoStoreTocEntryMeta> chunk_metas_;
    FIoDirectoryIndexResource directory_index_;
    std::unordered_map<uint32_t, std::string> file_map_;
};

} // namespace utoc
