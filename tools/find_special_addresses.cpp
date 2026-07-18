#include <cstdio>
#include <cstring>
#include <file.h>
#include <image.h>

struct Pattern
{
    const char* name;
    uint8_t bytes[8];
    size_t length;
};

static const Pattern patterns[] = {
    { "restgprlr_14_address", { 0xe9, 0xc1, 0xff, 0x68 }, 4 },
    { "savegprlr_14_address", { 0xf9, 0xc1, 0xff, 0x68 }, 4 },
    { "restfpr_14_address",   { 0xc9, 0xcc, 0xff, 0x70 }, 4 },
    { "savefpr_14_address",   { 0xd9, 0xcc, 0xff, 0x70 }, 4 },
    { "restvmx_14_address",   { 0x39, 0x60, 0xfe, 0xe0, 0x7d, 0xcb, 0x60, 0xce }, 8 },
    { "savevmx_14_address",   { 0x39, 0x60, 0xfe, 0xe0, 0x7d, 0xcb, 0x61, 0xce }, 8 },
    { "restvmx_64_address",   { 0x39, 0x60, 0xfc, 0x00, 0x10, 0x0b, 0x60, 0xcb }, 8 },
    { "savevmx_64_address",   { 0x39, 0x60, 0xfc, 0x00, 0x10, 0x0b, 0x61, 0xcb }, 8 },
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage: FindSpecialAddresses [input XEX file path]\n");
        return 1;
    }

    const auto file = LoadFile(argv[1]);
    if (file.empty())
    {
        printf("Failed to load file: %s\n", argv[1]);
        return 1;
    }

    auto image = Image::ParseImage(file.data(), file.size());

    for (const auto& pattern : patterns)
    {
        bool found = false;
        for (const auto& section : image.sections)
        {
            if (!(section.flags & SectionFlags_Code))
                continue;

            for (size_t i = 0; i + pattern.length <= section.size; i++)
            {
                if (memcmp(section.data + i, pattern.bytes, pattern.length) == 0)
                {
                    printf("%s = 0x%zX\n", pattern.name, section.base + i);
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
        {
            printf("# %s NOT FOUND\n", pattern.name);
        }
    }

    return 0;
}
