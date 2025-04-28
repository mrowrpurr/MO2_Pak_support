# UTOC Binary File Layout

This document describes the binary layout of Unreal Engine's `.utoc` (Unreal Table of Contents) files based on analysis of the retoc codebase.

## Overview

UTOC files are part of Unreal Engine's IoStore container system and contain metadata, directory information, and a table of contents that maps game assets to their storage locations in the corresponding `.ucas` file. The UTOC file serves as an index for the UCAS file, which contains the actual asset data.

### IoStore Container System

The IoStore container system was introduced in Unreal Engine 5 as a replacement for the older .pak file format. It consists of two main file types:

1. **UTOC (Unreal Table of Contents)** - Contains metadata, indices, and directory information
2. **UCAS (Unreal Content Asset Store)** - Contains the actual asset data (chunks)

This split design allows for more efficient asset loading and streaming, as the engine can quickly read the smaller UTOC file to locate assets without loading the entire container into memory.

### Relationship Between UTOC and UCAS

The UTOC file contains a mapping between chunk IDs and their locations in the UCAS file. When the engine needs to load an asset:

1. It looks up the chunk ID in the UTOC file
2. The UTOC file provides the offset and size of the chunk in the UCAS file
3. The engine reads the chunk data from the UCAS file at the specified offset
4. If the chunk is compressed, it uses the compression information from the UTOC file to decompress it

### UTOC Format Versions

The UTOC format has evolved over time, with several versions defined in the `EIoStoreTocVersion` enum:

- `Invalid` (0): Invalid version
- `Initial` (1): Initial version
- `DirectoryIndex` (2): Added directory index
- `PartitionSize` (3): Added partition size
- `PerfectHash` (4): Added perfect hash for chunk lookup
- `PerfectHashWithOverflow` (5): Added overflow for perfect hash
- `OnDemandMetaData` (6): Added on-demand metadata
- `RemovedOnDemandMetaData` (7): Removed on-demand metadata
- `ReplaceIoChunkHashWithIoHash` (8): Changed hash format

Each version may have slight differences in the binary layout, particularly in how chunk hashes are stored and how the directory index is structured.

## Binary Layout

Based on the retoc codebase, a .utoc file has the following structure:

### 1. TOC Header (`FIoStoreTocHeader`)

The header is a fixed-size structure (144 bytes) that contains:

- Magic: 16 bytes (`-==--==--==--==-`)
- Version: 1 byte (enum `EIoStoreTocVersion`)
- Reserved fields: 3 bytes
- TOC header size: 4 bytes (0x90 = 144)
- TOC entry count: 4 bytes
- TOC compressed block entry count: 4 bytes
- TOC compressed block entry size: 4 bytes
- Compression method name count: 4 bytes
- Compression method name length: 4 bytes
- Compression block size: 4 bytes
- Directory index size: 4 bytes
- Partition count: 4 bytes
- Container ID: 8 bytes
- Encryption key GUID: 16 bytes
- Container flags: 1 byte
- Reserved fields: 3 bytes
- TOC chunk perfect hash seeds count: 4 bytes
- Partition size: 8 bytes
- TOC chunks without perfect hash count: 4 bytes
- Reserved fields: 44 bytes

### 2. Chunk IDs (`FIoChunkId[]`)

An array of chunk IDs, each 12 bytes:
- 8 bytes: Chunk ID (often a package ID)
- 2 bytes: Chunk index
- 1 byte: Chunk type (enum `EIoChunkType`)
- 1 byte: Version information

### 3. Chunk Offsets and Lengths (`FIoOffsetAndLength[]`)

An array of offset and length pairs, each 10 bytes:
- 5 bytes: Offset into the uncompressed data
- 5 bytes: Length of the chunk

### 4. Hash Map

For perfect hashing, contains:
- Chunk perfect hash seeds: Array of 4-byte integers
- Chunk indices without perfect hash: Array of 4-byte integers

### 5. Compression Blocks (`FIoStoreTocCompressedBlockEntry[]`)

An array of compression block entries, each 12 bytes:
- 5 bytes: Offset into the .ucas file
- 3 bytes: Compressed size
- 3 bytes: Uncompressed size
- 1 byte: Compression method index

### 6. Compression Methods

An array of compression method names, each a fixed-length string (32 bytes).

### 7. Signatures (Optional)

If the container is signed (indicated by the container flags), contains:
- TOC signature size: 4 bytes
- TOC signature: Variable-length byte array
- Block signature: Variable-length byte array
- Chunk block signatures: Array of SHA1 hashes (20 bytes each)

### 8. Directory Index

A variable-length structure containing:
- Mount point: String
- Directory entries: Array of directory entries
- File entries: Array of file entries
- String table: Array of strings

### 9. Chunk Metadata (`FIoStoreTocEntryMeta[]`)

An array of chunk metadata entries, each containing:
- Chunk hash: 32 bytes (or 20 bytes + 3 bytes padding in newer versions)
- Flags: 1 byte

## Key Structures

### `FIoChunkId` (12 bytes)

Identifies a specific chunk in the container:
- 8 bytes: Chunk ID (often a package ID)
- 2 bytes: Chunk index
- 1 byte: Chunk type (enum `EIoChunkType`)
- 1 byte: Version information

#### Chunk Types (`EIoChunkType`)

The chunk type field in `FIoChunkId` indicates what kind of data the chunk contains:

| Value | Name                  | Description                                      |
|-------|----------------------|--------------------------------------------------|
| 0     | Invalid              | Invalid chunk type                               |
| 1     | ExportBundleData     | Main asset data (UE5)                            |
| 2     | BulkData             | Bulk data (.ubulk)                               |
| 3     | OptionalBulkData     | Optional bulk data (.uptnl)                      |
| 4     | MemoryMappedBulkData | Memory-mapped bulk data (.m.ubulk)               |
| 5     | ScriptObjects        | Script objects data                              |
| 6     | ContainerHeader      | Container header information                     |
| 7     | ExternalFile         | External file reference                          |
| 8     | ShaderCodeLibrary    | Shader code library                              |
| 9     | ShaderCode           | Individual shader code                           |
| 10    | PackageStoreEntry    | Package store entry                              |
| 11    | DerivedData          | Derived data                                     |
| 12    | EditorDerivedData    | Editor-specific derived data                     |
| 13    | PackageResource      | Package resource                                 |

Note: In UE4, the chunk type values are different:
- 1: InstallManifest
- 2: ExportBundleData
- 3: BulkData
- etc.

### `FIoOffsetAndLength` (10 bytes)

Stores the offset and length of a chunk:
- 5 bytes: Offset
- 5 bytes: Length

### `FIoStoreTocCompressedBlockEntry` (12 bytes)

Describes a compressed block in the .ucas file:
- 5 bytes: Offset
- 3 bytes: Compressed size
- 3 bytes: Uncompressed size
- 1 byte: Compression method index

### `FIoStoreTocEntryMeta`

Contains metadata for a chunk:
- Chunk hash: 32 bytes (or 20 bytes + 3 bytes padding in newer versions)
- Flags: 1 byte

#### Entry Meta Flags (`FIoStoreTocEntryMetaFlags`)

| Bit | Flag          | Description                                      |
|-----|--------------|--------------------------------------------------|
| 0   | Compressed   | Chunk is compressed                              |
| 1   | MemoryMapped | Chunk is memory-mapped                           |

### Container Flags (`EIoContainerFlags`)

The container flags in the TOC header indicate various properties of the container:

| Bit | Flag       | Description                                      |
|-----|-----------|--------------------------------------------------|
| 0   | Compressed | Container uses compression                       |
| 1   | Encrypted  | Container is encrypted                           |
| 2   | Signed     | Container is signed                              |
| 3   | Indexed    | Container has a directory index                  |

### `FIoDirectoryIndexResource`

A hierarchical structure that maps file paths to chunk indices:
- Mount point: String
- Directory entries: Array of directory entries
- File entries: Array of file entries
- String table: Array of strings

## How retoc Uses UTOC Files

The retoc tool uses the information in UTOC files to:

1. **Extract Assets**: By reading the chunk IDs, offsets, and lengths, retoc can extract individual assets from the UCAS file.

2. **Convert Between Formats**: retoc can convert between Zen assets (IoStore format) and Legacy assets (.pak format) by reading the asset data from the UCAS file and transforming it.

3. **List Container Contents**: Using the directory index, retoc can list all files contained in an IoStore container.

4. **Extract Manifests**: retoc can extract manifest information from the container header and directory index.

5. **Handle Compression and Encryption**: Using the compression blocks and methods information, retoc can decompress chunks. If the container is encrypted, retoc can decrypt it using the provided AES key.

## Reading Process

The retoc tool reads a .utoc file as follows:

1. Read the header
2. Read the chunk IDs
3. Read the chunk offsets and lengths
4. Read the hash map
5. Read the compression blocks
6. Read the compression methods
7. Read the signatures (if present)
8. Read and decrypt the directory index (if encrypted)
9. Read the chunk metadata

Once the .utoc file is parsed, it can be used to locate and extract chunks from the corresponding .ucas file.

## Diagram

### Overall Structure

```
+----------------------------------------------+
|                 UTOC File                    |
+----------------------------------------------+
| Offset | Size  | Description                 |
|--------|-------|----------------------------|
| 0x0000 | 0x90  | TOC Header                 |
| 0x0090 | var   | Chunk IDs                  |
|        |       | (TOC entry count * 12)     |
|--------|-------|----------------------------|
| var    | var   | Chunk Offsets and Lengths  |
|        |       | (TOC entry count * 10)     |
|--------|-------|----------------------------|
| var    | var   | Hash Map                   |
|        |       | - Perfect hash seeds       |
|        |       | - Indices without hash     |
|--------|-------|----------------------------|
| var    | var   | Compression Blocks         |
|        |       | (compressed block count*12)|
|--------|-------|----------------------------|
| var    | var   | Compression Methods        |
|        |       | (method count * 32)        |
|--------|-------|----------------------------|
| var    | var   | Signatures (Optional)      |
|--------|-------|----------------------------|
| var    | var   | Directory Index            |
|        |       | (directory index size)     |
|--------|-------|----------------------------|
| var    | var   | Chunk Metadata             |
|        |       | (TOC entry count * var)    |
+----------------------------------------------+
```

### TOC Header Detail (144 bytes)

```
+----------------------------------------------+
|               TOC Header                     |
+----------------------------------------------+
| Offset | Size | Description                  |
|--------|------|------------------------------|
| 0x00   | 0x10 | Magic ("-==--==--==--==-")   |
| 0x10   | 0x01 | Version (EIoStoreTocVersion) |
| 0x11   | 0x03 | Reserved                     |
| 0x14   | 0x04 | TOC header size (0x90)       |
| 0x18   | 0x04 | TOC entry count              |
| 0x1C   | 0x04 | TOC compressed block count   |
| 0x20   | 0x04 | TOC compressed block size    |
| 0x24   | 0x04 | Compression method count     |
| 0x28   | 0x04 | Compression method length    |
| 0x2C   | 0x04 | Compression block size       |
| 0x30   | 0x04 | Directory index size         |
| 0x34   | 0x04 | Partition count              |
| 0x38   | 0x08 | Container ID                 |
| 0x40   | 0x10 | Encryption key GUID          |
| 0x50   | 0x01 | Container flags              |
| 0x51   | 0x03 | Reserved                     |
| 0x54   | 0x04 | Perfect hash seeds count     |
| 0x58   | 0x08 | Partition size               |
| 0x60   | 0x04 | Chunks without hash count    |
| 0x64   | 0x04 | Reserved                     |
| 0x68   | 0x28 | Reserved                     |
+----------------------------------------------+
```

### FIoChunkId Detail (12 bytes)

```
+----------------------------------------------+
|               FIoChunkId                     |
+----------------------------------------------+
| Offset | Size | Description                  |
|--------|------|------------------------------|
| 0x00   | 0x08 | Chunk ID (Package ID)        |
| 0x08   | 0x02 | Chunk index                  |
| 0x0A   | 0x01 | Chunk type                   |
| 0x0B   | 0x01 | Version information          |
+----------------------------------------------+
```

### FIoOffsetAndLength Detail (10 bytes)

```
+----------------------------------------------+
|           FIoOffsetAndLength                 |
+----------------------------------------------+
| Offset | Size | Description                  |
|--------|------|------------------------------|
| 0x00   | 0x05 | Offset                       |
| 0x05   | 0x05 | Length                       |
+----------------------------------------------+
```

### FIoStoreTocCompressedBlockEntry Detail (12 bytes)

```
+----------------------------------------------+
|      FIoStoreTocCompressedBlockEntry         |
+----------------------------------------------+
| Offset | Size | Description                  |
|--------|------|------------------------------|
| 0x00   | 0x05 | Offset in .ucas file         |
| 0x05   | 0x03 | Compressed size              |
| 0x08   | 0x03 | Uncompressed size            |
| 0x0B   | 0x01 | Compression method index     |
+----------------------------------------------+
```

### FIoStoreTocEntryMeta Detail

```
+----------------------------------------------+
|           FIoStoreTocEntryMeta               |
+----------------------------------------------+
| Offset | Size | Description                  |
|--------|------|------------------------------|
| 0x00   | 0x20 | Chunk hash (32 bytes)        |
|        |      | or 20 bytes + 3 bytes pad    |
| var    | 0x01 | Flags                        |
+----------------------------------------------+
```

### Directory Index Structure

```
+----------------------------------------------+
|           Directory Index                    |
+----------------------------------------------+
| Component      | Description                 |
|----------------|----------------------------|
| Mount point    | String                     |
| Directory      | Array of directory entries |
| entries        |                            |
| File entries   | Array of file entries      |
| String table   | Array of strings           |
+----------------------------------------------+
```

## Conclusion

Understanding the UTOC file format is essential for working with Unreal Engine 5 games, particularly for modding, asset extraction, and custom content creation. The IoStore container system represents a significant evolution from the older .pak file format, offering improved performance and more efficient asset streaming.

Key takeaways about the UTOC format:

1. **Two-File System**: The IoStore container system splits asset storage into two files - .utoc for metadata and .ucas for the actual asset data.

2. **Hierarchical Structure**: The UTOC file contains a hierarchical directory structure that maps file paths to chunk IDs, making it easy to locate specific assets.

3. **Compression and Encryption**: The format supports both compression and encryption, with the UTOC file containing all the information needed to decompress and decrypt chunks.

4. **Version Evolution**: The format has evolved over time, with newer versions adding features like perfect hashing and changing how chunk hashes are stored.

5. **Chunk Types**: Different chunk types are used for different kinds of data, such as main asset data, bulk data, shader code, and container headers.

Tools like retoc play a crucial role in the modding ecosystem by providing the ability to extract, convert, and repack assets from IoStore containers. By understanding the binary layout of UTOC files, developers can create more efficient tools for working with Unreal Engine assets.
