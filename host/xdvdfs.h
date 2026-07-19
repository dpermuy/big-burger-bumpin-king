#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

struct XdvdfsEntry
{
    uint32_t startSector = 0;
    uint32_t fileSize = 0;
    bool isDirectory = false;
};

class XdvdfsImage
{
public:
    bool Open(const std::string& isoPath);
    bool ResolvePath(const std::string& path, XdvdfsEntry& outEntry);
    void ReadBytes(const XdvdfsEntry& entry, uint64_t offsetInFile, uint32_t length, uint8_t* dest);

private:
    bool SearchDirectory(uint32_t tableSector, uint32_t tableSize, const std::string& name, XdvdfsEntry& outEntry);

    std::mutex fileMutex_;
    std::ifstream file_;
    uint32_t rootSector_ = 0;
    uint32_t rootSize_ = 0;
};

extern XdvdfsImage g_xdvdfsImage;
