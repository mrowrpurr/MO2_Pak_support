#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <optional>
#include <array>
#include <stdexcept>

// Define uint128_t since it's not standard
#ifdef _MSC_VER
struct uint128_t {
    uint64_t low;
    uint64_t high;
    
    bool operator==(const uint128_t& other) const {
        return low == other.low && high == other.high;
    }
};
#else
__extension__ typedef unsigned __int128 uint128_t;
#endif

namespace pak {

// Magic number that identifies a .pak file
constexpr uint32_t MAGIC = 0x5A6F12E1;

// Enum for the different versions of the .pak file format
enum class Version {
    V0,
    V1,
    V2,
    V3,
    V4,
    V5,
    V6,
    V7,
    V8A,
    V8B,
    V9,
    V10,
    V11
};

// Enum for the major version written to the pak file
enum class VersionMajor {
    Unknown,               // v0 unknown
    Initial,               // v1 initial specification
    NoTimestamps,          // v2 timestamps removed
    CompressionEncryption, // v3 compression and encryption support
    IndexEncryption,       // v4 index encryption support
    RelativeChunkOffsets,  // v5 offsets are relative to header
    DeleteRecords,         // v6 record deletion support
    EncryptionKeyGuid,     // v7 include key GUID
    FNameBasedCompression, // v8 compression names included
    FrozenIndex,           // v9 frozen index byte included
    PathHashIndex,         // v10
    Fnv64BugFix,           // v11
};

// Enum for compression methods
enum class Compression {
    Zlib,
    Gzip,
    Oodle,
    Zstd,
    LZ4
};

// Structure for a compression block
struct Block {
    uint64_t start;
    uint64_t end;
};

// Structure for a file entry in the pak
struct Entry {
    uint64_t offset;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    std::optional<uint32_t> compression_slot;
    std::optional<uint64_t> timestamp;
    std::array<uint8_t, 20> hash;
    std::optional<std::vector<Block>> blocks;
    uint8_t flags;
    uint32_t compression_block_size;

    bool is_encrypted() const {
        return (flags & 1) != 0;
    }

    bool is_deleted() const {
        return ((flags >> 1) & 1) != 0;
    }
};

// Structure for the footer of the pak file
struct Footer {
    std::optional<uint128_t> encryption_uuid;
    bool encrypted;
    uint32_t magic;
    Version version;
    VersionMajor version_major;
    uint64_t index_offset;
    uint64_t index_size;
    std::array<uint8_t, 20> hash;
    bool frozen;
    std::vector<std::optional<Compression>> compression;
};

// Class for reading .pak files
class PakReader {
public:
    // Constructor that takes a path to a .pak file
    explicit PakReader(const std::filesystem::path& path);
    
    // Destructor
    ~PakReader();
    
    // Get the version of the pak file
    Version version() const;
    
    // Get the mount point of the pak file
    std::string mount_point() const;
    
    // Get whether the index is encrypted
    bool encrypted_index() const;
    
    // Get the encryption GUID
    std::optional<uint128_t> encryption_guid() const;
    
    // Get a list of all files in the pak
    std::vector<std::string> files() const;
    
    // Get a list of all directories in the pak
    std::vector<std::string> directories() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Exception class for pak reader errors
class PakException : public std::runtime_error {
public:
    explicit PakException(const std::string& message);
};

} // namespace pak
