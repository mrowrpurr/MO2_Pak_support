#include "pak_reader.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <filesystem>
#include <map>

namespace pak {

namespace {
    // Helper function to read a boolean from a stream
    bool read_bool(std::ifstream& stream) {
        uint8_t value;
        stream.read(reinterpret_cast<char*>(&value), sizeof(value));
        if (value != 0 && value != 1) {
            throw PakException("Invalid boolean value: " + std::to_string(value));
        }
        return value == 1;
    }

    // Helper function to read a GUID (20 bytes) from a stream
    std::array<uint8_t, 20> read_guid(std::ifstream& stream) {
        std::array<uint8_t, 20> guid;
        stream.read(reinterpret_cast<char*>(guid.data()), guid.size());
        return guid;
    }

    // Helper function to read a string from a stream
    std::string read_string(std::ifstream& stream) {
        int32_t length;
        stream.read(reinterpret_cast<char*>(&length), sizeof(length));
        
        if (length < 0) {
            // UTF-16 string
            std::vector<uint16_t> chars(static_cast<size_t>(-length));
            stream.read(reinterpret_cast<char*>(chars.data()), chars.size() * sizeof(uint16_t));
            
            // Find null terminator
            size_t null_pos = 0;
            while (null_pos < chars.size() && chars[null_pos] != 0) {
                null_pos++;
            }
            
            // Convert to UTF-8
            std::string result;
            for (size_t i = 0; i < null_pos; ++i) {
                // Simple conversion for ASCII range
                if (chars[i] < 128) {
                    result.push_back(static_cast<char>(chars[i]));
                } else {
                    // For non-ASCII, just use a placeholder
                    result.push_back('?');
                }
            }
            return result;
        } else {
            // ASCII string
            std::vector<char> chars(static_cast<size_t>(length));
            stream.read(chars.data(), chars.size());
            
            // Find null terminator
            size_t null_pos = 0;
            while (null_pos < chars.size() && chars[null_pos] != 0) {
                null_pos++;
            }
            
            return std::string(chars.data(), null_pos);
        }
    }

    // Helper function to convert Version to VersionMajor
    VersionMajor version_to_major(Version version) {
        switch (version) {
            case Version::V0: return VersionMajor::Unknown;
            case Version::V1: return VersionMajor::Initial;
            case Version::V2: return VersionMajor::NoTimestamps;
            case Version::V3: return VersionMajor::CompressionEncryption;
            case Version::V4: return VersionMajor::IndexEncryption;
            case Version::V5: return VersionMajor::RelativeChunkOffsets;
            case Version::V6: return VersionMajor::DeleteRecords;
            case Version::V7: return VersionMajor::EncryptionKeyGuid;
            case Version::V8A:
            case Version::V8B: return VersionMajor::FNameBasedCompression;
            case Version::V9: return VersionMajor::FrozenIndex;
            case Version::V10: return VersionMajor::PathHashIndex;
            case Version::V11: return VersionMajor::Fnv64BugFix;
            default: return VersionMajor::Unknown;
        }
    }

    // Helper function to get the size of the footer based on the version
    int64_t get_footer_size(Version version) {
        // (magic + version): u32 + (offset + size): u64 + hash: [u8; 20]
        int64_t size = 4 + 4 + 8 + 8 + 20;
        
        if (version_to_major(version) >= VersionMajor::EncryptionKeyGuid) {
            // encryption uuid: u128
            size += 16;
        }
        
        if (version_to_major(version) >= VersionMajor::IndexEncryption) {
            // encrypted: bool
            size += 1;
        }
        
        if (version_to_major(version) == VersionMajor::FrozenIndex) {
            // frozen index: bool
            size += 1;
        }
        
        if (version >= Version::V8A) {
            // compression names: [[u8; 32]; 4]
            size += 32 * 4;
        }
        
        if (version >= Version::V8B) {
            // additional compression name
            size += 32;
        }
        
        return size;
    }

    // Helper function to extract the directory part of a path
    std::string get_directory(const std::string& path) {
        size_t pos = path.find_last_of('/');
        if (pos == std::string::npos) {
            return "";
        }
        return path.substr(0, pos);
    }
}

// Implementation of the PakException class
PakException::PakException(const std::string& message)
    : std::runtime_error(message) {}

// Implementation of the PakReader class
class PakReader::Impl {
public:
    explicit Impl(const std::filesystem::path& path)
        : path_(path), stream_(path.string(), std::ios::binary) {
        if (!stream_) {
            throw PakException("Failed to open file: " + path.string());
        }
        
        std::cout << "File size: " << std::filesystem::file_size(path) << " bytes" << std::endl;
        
        // Try to read the pak file with different versions
        for (int v = static_cast<int>(Version::V11); v >= static_cast<int>(Version::V1); --v) {
            try {
                Version version = static_cast<Version>(v);
                std::cout << "Trying version: " << v << std::endl;
                read_footer(version);
                std::cout << "Footer read successfully. Index offset: " << footer_.index_offset 
                          << ", Index size: " << footer_.index_size << std::endl;
                read_index();
                std::cout << "Index read successfully. Found " << entries_.size() << " entries." << std::endl;
                return;
            } catch (const std::exception& e) {
                std::cout << "Failed with version " << v << ": " << e.what() << std::endl;
                // Try the next version
                stream_.clear();
                stream_.seekg(0);
            }
        }
        
        throw PakException("Failed to read pak file: " + path.string());
    }
    
    Version version() const {
        return footer_.version;
    }
    
    std::string mount_point() const {
        return mount_point_;
    }
    
    bool encrypted_index() const {
        return footer_.encrypted;
    }
    
    std::optional<uint128_t> encryption_guid() const {
        return footer_.encryption_uuid;
    }
    
    std::vector<std::string> files() const {
        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const auto& [path, _] : entries_) {
            result.push_back(path);
        }
        return result;
    }
    
    std::vector<std::string> directories() const {
        std::unordered_set<std::string> dirs;
        for (const auto& [path, _] : entries_) {
            std::string dir = get_directory(path);
            while (!dir.empty()) {
                dirs.insert(dir);
                dir = get_directory(dir);
            }
        }
        
        std::vector<std::string> result(dirs.begin(), dirs.end());
        std::sort(result.begin(), result.end());
        return result;
    }
    
private:
    std::filesystem::path path_;
    std::ifstream stream_;
    Footer footer_;
    std::string mount_point_;
    std::map<std::string, Entry> entries_;
    
    void read_footer(Version version) {
        // Seek to the end of the file minus the footer size
        stream_.seekg(-get_footer_size(version), std::ios::end);
        
        // Read the footer
        if (version_to_major(version) >= VersionMajor::EncryptionKeyGuid) {
            uint128_t uuid;
            stream_.read(reinterpret_cast<char*>(&uuid), sizeof(uuid));
            footer_.encryption_uuid = uuid;
        } else {
            footer_.encryption_uuid = std::nullopt;
        }
        
        if (version_to_major(version) >= VersionMajor::IndexEncryption) {
            footer_.encrypted = read_bool(stream_);
        } else {
            footer_.encrypted = false;
        }
        
        uint32_t magic;
        stream_.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != MAGIC) {
            throw PakException("Invalid magic number: " + std::to_string(magic));
        }
        footer_.magic = magic;
        
        uint32_t version_major;
        stream_.read(reinterpret_cast<char*>(&version_major), sizeof(version_major));
        footer_.version_major = static_cast<VersionMajor>(version_major);
        
        if (version_to_major(version) != footer_.version_major) {
            throw PakException("Version mismatch");
        }
        
        stream_.read(reinterpret_cast<char*>(&footer_.index_offset), sizeof(footer_.index_offset));
        stream_.read(reinterpret_cast<char*>(&footer_.index_size), sizeof(footer_.index_size));
        footer_.hash = read_guid(stream_);
        
        if (version_to_major(version) == VersionMajor::FrozenIndex) {
            footer_.frozen = read_bool(stream_);
        } else {
            footer_.frozen = false;
        }
        
        // Read compression methods
        size_t compression_count = 0;
        if (version < Version::V8A) {
            compression_count = 0;
        } else if (version < Version::V8B) {
            compression_count = 4;
        } else {
            compression_count = 5;
        }
        
        footer_.compression.resize(compression_count);
        for (size_t i = 0; i < compression_count; ++i) {
            char name[32] = {0};
            stream_.read(name, sizeof(name));
            
            // Convert to string and trim null bytes
            std::string compression_name;
            for (size_t j = 0; j < sizeof(name); ++j) {
                if (name[j] == 0) break;
                compression_name.push_back(name[j]);
            }
            
            if (compression_name.empty()) {
                footer_.compression[i] = std::nullopt;
            } else if (compression_name == "Zlib") {
                footer_.compression[i] = Compression::Zlib;
            } else if (compression_name == "Gzip") {
                footer_.compression[i] = Compression::Gzip;
            } else if (compression_name == "Oodle") {
                footer_.compression[i] = Compression::Oodle;
            } else if (compression_name == "Zstd") {
                footer_.compression[i] = Compression::Zstd;
            } else if (compression_name == "LZ4") {
                footer_.compression[i] = Compression::LZ4;
            } else {
                footer_.compression[i] = std::nullopt;
            }
        }
        
        // Add default compression methods for older versions
        if (version_to_major(version) < VersionMajor::FNameBasedCompression) {
            footer_.compression.push_back(Compression::Zlib);
            footer_.compression.push_back(Compression::Gzip);
            footer_.compression.push_back(Compression::Oodle);
        }
        
        footer_.version = version;
    }
    
    void read_index() {
        // Seek to the index offset
        stream_.seekg(footer_.index_offset);
        
        // If the index is encrypted, we can't read it without a key
        if (footer_.encrypted) {
            throw PakException("Index is encrypted, decryption not supported");
        }
        
        // Read the mount point
        mount_point_ = read_string(stream_);
        
        // Read the number of entries
        uint32_t entry_count;
        stream_.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
        
        // Handle different index formats based on version
        if (footer_.version_major >= VersionMajor::PathHashIndex) {
            // V10+ format with path hash index
            uint64_t path_hash_seed;
            stream_.read(reinterpret_cast<char*>(&path_hash_seed), sizeof(path_hash_seed));
            
            // Skip path hash index if present
            uint32_t has_path_hash_index;
            stream_.read(reinterpret_cast<char*>(&has_path_hash_index), sizeof(has_path_hash_index));
            if (has_path_hash_index != 0) {
                uint64_t path_hash_index_offset, path_hash_index_size;
                stream_.read(reinterpret_cast<char*>(&path_hash_index_offset), sizeof(path_hash_index_offset));
                stream_.read(reinterpret_cast<char*>(&path_hash_index_size), sizeof(path_hash_index_size));
                
                // Skip hash
                stream_.seekg(20, std::ios::cur);
            }
            
            // Read full directory index if present
            uint32_t has_full_directory_index;
            stream_.read(reinterpret_cast<char*>(&has_full_directory_index), sizeof(has_full_directory_index));
            if (has_full_directory_index != 0) {
                uint64_t full_directory_index_offset, full_directory_index_size;
                stream_.read(reinterpret_cast<char*>(&full_directory_index_offset), sizeof(full_directory_index_offset));
                stream_.read(reinterpret_cast<char*>(&full_directory_index_size), sizeof(full_directory_index_size));
                
                // Skip hash
                stream_.seekg(20, std::ios::cur);
                
                // Save current position
                auto current_pos = stream_.tellg();
                
                // Seek to full directory index
                stream_.seekg(full_directory_index_offset);
                
                // Read directory count
                uint32_t dir_count;
                stream_.read(reinterpret_cast<char*>(&dir_count), sizeof(dir_count));
                
                // Read directories and files
                for (uint32_t i = 0; i < dir_count; ++i) {
                    std::string dir_name = read_string(stream_);
                    
                    uint32_t file_count;
                    stream_.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
                    
                    for (uint32_t j = 0; j < file_count; ++j) {
                        std::string file_name = read_string(stream_);
                        uint32_t encoded_offset;
                        stream_.read(reinterpret_cast<char*>(&encoded_offset), sizeof(encoded_offset));
                        
                        // Skip invalid offsets
                        if (encoded_offset == 0x80000000) {
                            continue;
                        }
                        
                        // Construct full path
                        std::string path = dir_name;
                        if (!path.empty() && path.back() != '/') {
                            path += '/';
                        }
                        path += file_name;
                        
                        // Strip leading slash if present
                        if (!path.empty() && path.front() == '/') {
                            path = path.substr(1);
                        }
                        
                        // Create a dummy entry for now
                        Entry entry;
                        entry.offset = 0;
                        entry.compressed_size = 0;
                        entry.uncompressed_size = 0;
                        entry.flags = 0;
                        entry.compression_block_size = 0;
                        
                        entries_[path] = entry;
                    }
                }
                
                // Restore position
                stream_.seekg(current_pos);
            }
        } else {
            // Pre-V10 format with simple index
            for (uint32_t i = 0; i < entry_count; ++i) {
                std::string path = read_string(stream_);
                Entry entry = read_entry();
                entries_[path] = entry;
            }
        }
    }
    
    Entry read_entry() {
        Entry entry;
        
        // Read basic fields
        stream_.read(reinterpret_cast<char*>(&entry.offset), sizeof(entry.offset));
        stream_.read(reinterpret_cast<char*>(&entry.compressed_size), sizeof(entry.compressed_size));
        stream_.read(reinterpret_cast<char*>(&entry.uncompressed_size), sizeof(entry.uncompressed_size));
        
        // Read compression slot
        if (footer_.version == Version::V8A) {
            uint8_t compression = 0;
            stream_.read(reinterpret_cast<char*>(&compression), sizeof(compression));
            entry.compression_slot = compression == 0 ? std::nullopt : std::optional<uint32_t>(compression - 1);
        } else {
            uint32_t compression = 0;
            stream_.read(reinterpret_cast<char*>(&compression), sizeof(compression));
            entry.compression_slot = compression == 0 ? std::nullopt : std::optional<uint32_t>(compression - 1);
        }
        
        // Read timestamp if present
        if (footer_.version_major == VersionMajor::Initial) {
            uint64_t timestamp;
            stream_.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
            entry.timestamp = timestamp;
        } else {
            entry.timestamp = std::nullopt;
        }
        
        // Read hash
        entry.hash = read_guid(stream_);
        
        // Read blocks if present
        if (footer_.version_major >= VersionMajor::CompressionEncryption && entry.compression_slot.has_value()) {
            uint32_t block_count;
            stream_.read(reinterpret_cast<char*>(&block_count), sizeof(block_count));
            
            std::vector<Block> blocks;
            blocks.reserve(block_count);
            
            for (uint32_t i = 0; i < block_count; ++i) {
                Block block;
                stream_.read(reinterpret_cast<char*>(&block.start), sizeof(block.start));
                stream_.read(reinterpret_cast<char*>(&block.end), sizeof(block.end));
                blocks.push_back(block);
            }
            
            entry.blocks = blocks;
        } else {
            entry.blocks = std::nullopt;
        }
        
        // Read flags and compression block size if present
        if (footer_.version_major >= VersionMajor::CompressionEncryption) {
            stream_.read(reinterpret_cast<char*>(&entry.flags), sizeof(entry.flags));
            stream_.read(reinterpret_cast<char*>(&entry.compression_block_size), sizeof(entry.compression_block_size));
        } else {
            entry.flags = 0;
            entry.compression_block_size = 0;
        }
        
        return entry;
    }
};

// Implementation of the PakReader class methods
PakReader::PakReader(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>(path)) {}

PakReader::~PakReader() = default;

Version PakReader::version() const {
    return impl_->version();
}

std::string PakReader::mount_point() const {
    return impl_->mount_point();
}

bool PakReader::encrypted_index() const {
    return impl_->encrypted_index();
}

std::optional<uint128_t> PakReader::encryption_guid() const {
    return impl_->encryption_guid();
}

std::vector<std::string> PakReader::files() const {
    return impl_->files();
}

std::vector<std::string> PakReader::directories() const {
    return impl_->directories();
}

} // namespace pak
