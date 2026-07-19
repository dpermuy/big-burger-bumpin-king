#include "xdvdfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

XdvdfsImage g_xdvdfsImage;

namespace
{
    constexpr uint64_t kSectorSize = 2048;
    constexpr uint64_t kVolumeDescriptorOffset = 0x10000;
    constexpr char kMagic[20] = { 'M','I','C','R','O','S','O','F','T','*','X','B','O','X','*','M','E','D','I','A' };

    std::string ToLower(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return result;
    }

    std::vector<std::string> SplitPath(const std::string& path)
    {
        std::vector<std::string> parts;
        std::string current;
        for (char c : path)
        {
            if (c == '\\' || c == '/')
            {
                if (!current.empty())
                {
                    parts.push_back(current);
                    current.clear();
                }
            }
            else
            {
                current.push_back(c);
            }
        }
        if (!current.empty())
        {
            parts.push_back(current);
        }
        return parts;
    }
}

bool XdvdfsImage::Open(const std::string& isoPath)
{
    file_.open(isoPath, std::ios::binary);
    if (!file_.is_open())
    {
        return false;
    }

    file_.seekg((std::streamoff)kVolumeDescriptorOffset);
    char magic[20];
    file_.read(magic, 20);
    if (std::memcmp(magic, kMagic, 20) != 0)
    {
        return false;
    }

    uint32_t rootSector = 0, rootSize = 0;
    file_.read(reinterpret_cast<char*>(&rootSector), 4);
    file_.read(reinterpret_cast<char*>(&rootSize), 4);
    rootSector_ = rootSector;
    rootSize_ = rootSize;

    return true;
}

bool XdvdfsImage::SearchDirectory(uint32_t tableSector, uint32_t tableSize, const std::string& name, XdvdfsEntry& outEntry)
{
    std::vector<uint8_t> table(tableSize);
    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        file_.seekg((std::streamoff)((uint64_t)tableSector * kSectorSize));
        file_.read(reinterpret_cast<char*>(table.data()), tableSize);
    }

    std::string targetLower = ToLower(name);
    uint32_t entryOffset = 0; // BST root is the entry at offset 0 of the table

    while (true)
    {
        // Header is 14 bytes (left:u16, right:u16, start_sector:u32, file_size:u32,
        // attributes:u8, name_length:u8); bail out if that alone doesn't fit.
        if ((uint64_t)entryOffset + 14 > tableSize)
        {
            return false;
        }

        uint16_t leftOffset, rightOffset;
        uint32_t startSector, fileSize;
        uint8_t attributes, nameLength;

        std::memcpy(&leftOffset, &table[entryOffset + 0], 2);
        std::memcpy(&rightOffset, &table[entryOffset + 2], 2);
        std::memcpy(&startSector, &table[entryOffset + 4], 4);
        std::memcpy(&fileSize, &table[entryOffset + 8], 4);
        attributes = table[entryOffset + 12];
        nameLength = table[entryOffset + 13];

        // Confirmed via real captured directory bytes: inter-entry padding is 0xFF-filled.
        // An empty directory table (or a read that landed entirely in padding) shows up
        // as an all-0xFF "entry" -- nameLength 0xFF is not a real name length.
        if (nameLength == 0xFF)
        {
            return false;
        }

        if ((uint64_t)entryOffset + 14 + nameLength > tableSize)
        {
            return false;
        }

        std::string entryName(reinterpret_cast<char*>(&table[entryOffset + 14]), nameLength);
        std::string entryNameLower = ToLower(entryName);

        if (targetLower == entryNameLower)
        {
            outEntry.startSector = startSector;
            outEntry.fileSize = fileSize;
            outEntry.isDirectory = (attributes & 0x10) != 0;
            return true;
        }

        if (targetLower < entryNameLower)
        {
            // Confirmed via real captured data (a genuine right_offset=0 on a non-root
            // entry): 0 is also a "no child" sentinel alongside 0xFFFF, not a pointer
            // back to the root entry at offset 0 -- treating 0 as a real offset here
            // caused an infinite loop cycling back to the root.
            if (leftOffset == 0xFFFF || leftOffset == 0) return false;
            entryOffset = (uint32_t)leftOffset * 4;
        }
        else
        {
            if (rightOffset == 0xFFFF || rightOffset == 0) return false;
            entryOffset = (uint32_t)rightOffset * 4;
        }
    }
}

bool XdvdfsImage::ResolvePath(const std::string& path, XdvdfsEntry& outEntry)
{
    std::vector<std::string> parts = SplitPath(path);
    if (parts.empty())
    {
        return false;
    }

    uint32_t tableSector = rootSector_;
    uint32_t tableSize = rootSize_;
    XdvdfsEntry current{};

    for (size_t i = 0; i < parts.size(); i++)
    {
        if (!SearchDirectory(tableSector, tableSize, parts[i], current))
        {
            return false;
        }

        if (i + 1 < parts.size())
        {
            if (!current.isDirectory)
            {
                return false; // path continues past a plain file
            }
            tableSector = current.startSector;
            tableSize = current.fileSize;
        }
    }

    outEntry = current;
    return true;
}

void XdvdfsImage::ReadBytes(const XdvdfsEntry& entry, uint64_t offsetInFile, uint32_t length, uint8_t* dest)
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    file_.seekg((std::streamoff)((uint64_t)entry.startSector * kSectorSize + offsetInFile));
    file_.read(reinterpret_cast<char*>(dest), length);
}
