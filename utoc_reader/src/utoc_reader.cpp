#include "utoc_reader.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stack>
#include <functional>

namespace utoc {

// FIoChunkId methods
uint64_t FIoChunkId::GetChunkId() const {
    uint64_t result = 0;
    std::memcpy(&result, id, sizeof(uint64_t));
    return result;
}

uint16_t FIoChunkId::GetChunkIndex() const {
    uint16_t result = 0;
    std::memcpy(&result, id + 8, sizeof(uint16_t));
    return result;
}

EIoChunkType FIoChunkId::GetChunkType() const {
    return static_cast<EIoChunkType>(id[10] & 0x3F);
}

bool FIoChunkId::HasVersionInfo() const {
    return (id[11] & (1 << 6)) != 0;
}

// FIoOffsetAndLength methods
uint64_t FIoOffsetAndLength::GetOffset() const {
    uint64_t result = 0;
    std::memcpy(&result, data, 5);
    return result;
}

uint64_t FIoOffsetAndLength::GetLength() const {
    uint64_t result = 0;
    std::memcpy(&result, data + 5, 5);
    return result;
}

// FIoStoreTocCompressedBlockEntry methods
uint64_t FIoStoreTocCompressedBlockEntry::GetOffset() const {
    uint64_t result = 0;
    std::memcpy(&result, data, 5);
    return result;
}

uint32_t FIoStoreTocCompressedBlockEntry::GetCompressedSize() const {
    uint32_t result = 0;
    std::memcpy(&result, data + 5, 3);
    return result;
}

uint32_t FIoStoreTocCompressedBlockEntry::GetUncompressedSize() const {
    uint32_t result = 0;
    std::memcpy(&result, data + 8, 3);
    return result;
}

uint8_t FIoStoreTocCompressedBlockEntry::GetCompressionMethodIndex() const {
    return data[11];
}

// FIoStoreTocHeader methods
bool FIoStoreTocHeader::IsValid() const {
    return std::memcmp(toc_magic, MAGIC, sizeof(MAGIC)) == 0;
}

// Helper function to read a value from a byte array
template<typename T>
T ReadValue(const uint8_t* data, size_t& offset) {
    T value;
    std::memcpy(&value, data + offset, sizeof(T));
    offset += sizeof(T);
    return value;
}

// Read optional value
template<typename T>
std::optional<T> UtocReader::ReadOptional(const uint8_t* data, size_t& offset) {
    uint32_t value = ReadValue<uint32_t>(data, offset);
    if (value == UINT32_MAX) {
        return std::nullopt;
    }
    return static_cast<T>(value);
}

// Read string
std::string UtocReader::ReadString(const uint8_t* data, size_t& offset) {
    int32_t length = ReadValue<int32_t>(data, offset);
    
    if (length == 0) {
        return "";
    }
    
    std::string result;
    if (length < 0) {
        // UTF-16 string
        length = -length;
        std::u16string utf16;
        utf16.reserve(length);
        
        for (int32_t i = 0; i < length; ++i) {
            char16_t c = ReadValue<char16_t>(data, offset);
            if (c == 0) break;
            utf16.push_back(c);
        }
        
        // Skip null terminator if we didn't read it
        if (utf16.size() < static_cast<size_t>(length)) {
            offset += (length - utf16.size()) * sizeof(char16_t);
        }
        
        // Convert UTF-16 to UTF-8 (simplified)
        for (char16_t c : utf16) {
            if (c < 0x80) {
                result.push_back(static_cast<char>(c));
            } else if (c < 0x800) {
                result.push_back(static_cast<char>(0xC0 | (c >> 6)));
                result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xE0 | (c >> 12)));
                result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
    } else {
        // ASCII string
        result.resize(length);
        std::memcpy(result.data(), data + offset, length);
        offset += length;
        
        // Remove null terminator if present
        size_t nullPos = result.find('\0');
        if (nullPos != std::string::npos) {
            result.resize(nullPos);
        }
    }
    
    return result;
}

bool UtocReader::Open(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << path.string() << std::endl;
        return false;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read the entire file
    std::vector<uint8_t> fileData(fileSize);
    file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    
    // Parse the header
    size_t offset = 0;
    std::memcpy(&header_, fileData.data(), sizeof(FIoStoreTocHeader));
    offset += sizeof(FIoStoreTocHeader);
    
    if (!header_.IsValid()) {
        std::cerr << "Invalid TOC header" << std::endl;
        return false;
    }
    
    // Read chunk IDs
    chunk_ids_.resize(header_.toc_entry_count);
    std::memcpy(chunk_ids_.data(), fileData.data() + offset, header_.toc_entry_count * sizeof(FIoChunkId));
    offset += header_.toc_entry_count * sizeof(FIoChunkId);
    
    // Read chunk offsets and lengths
    chunk_offset_lengths_.resize(header_.toc_entry_count);
    std::memcpy(chunk_offset_lengths_.data(), fileData.data() + offset, header_.toc_entry_count * sizeof(FIoOffsetAndLength));
    offset += header_.toc_entry_count * sizeof(FIoOffsetAndLength);
    
    // Read hash map
    if (header_.version >= EIoStoreTocVersion::PerfectHashWithOverflow) {
        chunk_perfect_hash_seeds_.resize(header_.toc_chunk_perfect_hash_seeds_count);
        std::memcpy(chunk_perfect_hash_seeds_.data(), fileData.data() + offset, header_.toc_chunk_perfect_hash_seeds_count * sizeof(int32_t));
        offset += header_.toc_chunk_perfect_hash_seeds_count * sizeof(int32_t);
        
        chunk_indices_without_perfect_hash_.resize(header_.toc_chunks_without_perfect_hash_count);
        std::memcpy(chunk_indices_without_perfect_hash_.data(), fileData.data() + offset, header_.toc_chunks_without_perfect_hash_count * sizeof(int32_t));
        offset += header_.toc_chunks_without_perfect_hash_count * sizeof(int32_t);
    } else if (header_.version >= EIoStoreTocVersion::PerfectHash) {
        chunk_perfect_hash_seeds_.resize(header_.toc_chunk_perfect_hash_seeds_count);
        std::memcpy(chunk_perfect_hash_seeds_.data(), fileData.data() + offset, header_.toc_chunk_perfect_hash_seeds_count * sizeof(int32_t));
        offset += header_.toc_chunk_perfect_hash_seeds_count * sizeof(int32_t);
    }
    
    // Read compression blocks
    compression_blocks_.resize(header_.toc_compressed_block_entry_count);
    std::memcpy(compression_blocks_.data(), fileData.data() + offset, header_.toc_compressed_block_entry_count * sizeof(FIoStoreTocCompressedBlockEntry));
    offset += header_.toc_compressed_block_entry_count * sizeof(FIoStoreTocCompressedBlockEntry);
    
    // Read compression methods
    compression_methods_.resize(header_.compression_method_name_count);
    for (uint32_t i = 0; i < header_.compression_method_name_count; ++i) {
        char methodName[32] = {0};
        std::memcpy(reinterpret_cast<void*>(methodName), fileData.data() + offset, header_.compression_method_name_length);
        compression_methods_[i] = methodName;
        offset += header_.compression_method_name_length;
    }
    
    // Skip signatures if present
    if (header_.IsEncrypted()) {
        std::cerr << "Encrypted TOC files are not supported" << std::endl;
        return false;
    }
    
    if (header_.IsSigned()) {
        uint32_t signatureSize = ReadValue<uint32_t>(fileData.data(), offset);
        offset += signatureSize * 2 + 4; // Skip TOC signature, block signature, and size
        offset += header_.toc_compressed_block_entry_count * 20; // Skip chunk block signatures (SHA1 hashes)
    }
    
    // Read directory index
    if (header_.IsIndexed() && header_.directory_index_size > 0) {
        std::vector<uint8_t> directoryData(header_.directory_index_size);
        std::memcpy(directoryData.data(), fileData.data() + offset, header_.directory_index_size);
        offset += header_.directory_index_size;
        
        if (!ParseDirectoryIndex(directoryData)) {
            std::cerr << "Failed to parse directory index" << std::endl;
            return false;
        }
    }
    
    // Read chunk metadata
    chunk_metas_.resize(header_.toc_entry_count);
    for (uint32_t i = 0; i < header_.toc_entry_count; ++i) {
        if (header_.version >= EIoStoreTocVersion::ReplaceIoChunkHashWithIoHash) {
            std::memcpy(chunk_metas_[i].chunk_hash.hash, fileData.data() + offset, 20);
            offset += 20;
            chunk_metas_[i].flags = ReadValue<uint8_t>(fileData.data(), offset);
            offset += 4; // 1 byte flags + 3 bytes padding
        } else {
            std::memcpy(&chunk_metas_[i].chunk_hash, fileData.data() + offset, sizeof(FIoChunkHash));
            offset += sizeof(FIoChunkHash);
            chunk_metas_[i].flags = ReadValue<uint8_t>(fileData.data(), offset);
            offset += 1;
        }
    }
    
    return true;
}

bool UtocReader::ParseDirectoryIndex(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    
    // Read mount point
    directory_index_.mount_point = ReadString(data.data(), offset);
    
    // Read directory entries
    uint32_t directoryEntryCount = ReadValue<uint32_t>(data.data(), offset);
    directory_index_.directory_entries.resize(directoryEntryCount);
    
    for (uint32_t i = 0; i < directoryEntryCount; ++i) {
        directory_index_.directory_entries[i].name = ReadOptional<uint32_t>(data.data(), offset);
        directory_index_.directory_entries[i].first_child_entry = ReadOptional<uint32_t>(data.data(), offset);
        directory_index_.directory_entries[i].next_sibling_entry = ReadOptional<uint32_t>(data.data(), offset);
        directory_index_.directory_entries[i].first_file_entry = ReadOptional<uint32_t>(data.data(), offset);
    }
    
    // Read file entries
    uint32_t fileEntryCount = ReadValue<uint32_t>(data.data(), offset);
    directory_index_.file_entries.resize(fileEntryCount);
    
    for (uint32_t i = 0; i < fileEntryCount; ++i) {
        directory_index_.file_entries[i].name = ReadValue<uint32_t>(data.data(), offset);
        directory_index_.file_entries[i].next_file_entry = ReadOptional<uint32_t>(data.data(), offset);
        directory_index_.file_entries[i].user_data = ReadValue<uint32_t>(data.data(), offset);
        
        // Map user_data (chunk index) to file path for quick lookup
        file_map_[directory_index_.file_entries[i].user_data] = "";
    }
    
    // Read string table
    uint32_t stringCount = ReadValue<uint32_t>(data.data(), offset);
    directory_index_.string_table.resize(stringCount);
    
    for (uint32_t i = 0; i < stringCount; ++i) {
        directory_index_.string_table[i] = ReadString(data.data(), offset);
    }
    
    return true;
}

std::vector<std::string> FIoDirectoryIndexResource::GetAllFilePaths() const {
    std::vector<std::string> result;
    
    // Helper function to recursively traverse the directory structure
    std::function<void(uint32_t, std::vector<std::string>&)> traverseDirectory = 
        [this, &traverseDirectory, &result](uint32_t dirIndex, std::vector<std::string>& path) {
            const auto& dir = directory_entries[dirIndex];
            
            // Add directory name to path if it has one
            if (dir.name && dir.name.has_value()) {
                path.push_back(string_table[dir.name.value()]);
            }
            
            // Process files in this directory
            auto fileIndex = dir.first_file_entry;
            while (fileIndex && fileIndex.has_value()) {
                const auto& file = file_entries[fileIndex.value()];
                
                // Add file name to path
                path.push_back(string_table[file.name]);
                
                // Construct full path
                std::string fullPath = mount_point;
                for (const auto& segment : path) {
                    if (!fullPath.empty() && fullPath.back() != '/') {
                        fullPath += '/';
                    }
                    fullPath += segment;
                }
                
                result.push_back(fullPath);
                
                // Remove file name from path
                path.pop_back();
                
                // Move to next file
                fileIndex = file.next_file_entry;
            }
            
            // Process child directories
            auto childIndex = dir.first_child_entry;
            while (childIndex && childIndex.has_value()) {
                traverseDirectory(childIndex.value(), path);
                childIndex = directory_entries[childIndex.value()].next_sibling_entry;
            }
            
            // Remove directory name from path if we added it
            if (dir.name && dir.name.has_value()) {
                path.pop_back();
            }
        };
    
    std::vector<std::string> path;
    if (!directory_entries.empty()) {
        traverseDirectory(0, path);
    }
    
    return result;
}

std::vector<std::string> UtocReader::GetAllFilePaths() const {
    return directory_index_.GetAllFilePaths();
}

} // namespace utoc
