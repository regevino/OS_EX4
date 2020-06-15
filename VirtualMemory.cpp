#include <cstddef>
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define ROOT_TABLE_OFFSET (VIRTUAL_ADDRESS_WIDTH - (TABLES_DEPTH * OFFSET_WIDTH))

#define ROOT_TABLE_ADDRESS 0

uint64_t findFrame(uint64_t pageIndex, size_t offset, uint64_t tableAddress, size_t width)
{
    if (width == 0)
    {
        return tableAddress;
    }
    word_t entry;
    uint64_t tableIndex = pageIndex >> width;
    uint64_t mask = tableIndex << width;
    PMread(tableAddress + tableIndex, &entry);
    if (entry)
    {
        return findFrame(pageIndex ^ mask, OFFSET_WIDTH, entry, width - offset);
    }
    else
    {
        //TODO
    }
}

void clearTable(uint64_t frameIndex)
{
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

void VMinitialize()
{
    clearTable(0);
}


int VMread(uint64_t virtualAddress, word_t *value)
{
    uint64_t offset = 1LL;
    for (int i = 0; i < OFFSET_WIDTH; ++i)
    {
        offset = offset & virtualAddress;
        offset = offset << 1UL;
    }
    virtualAddress = virtualAddress >> OFFSET_WIDTH;
    uint64_t address = findFrame(virtualAddress, ROOT_TABLE_OFFSET, ROOT_TABLE_ADDRESS,
                                 VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH);
    PMread(address + offset, value);
    return 0;
}


int VMwrite(uint64_t virtualAddress, word_t value)
{
    uint64_t offset = 1LL;
    for (int i = 0; i < OFFSET_WIDTH; ++i)
    {
        offset = offset & virtualAddress;
        offset = offset << 1UL;
    }
    virtualAddress = virtualAddress >> OFFSET_WIDTH;
    uint64_t address = findFrame(virtualAddress, ROOT_TABLE_OFFSET, ROOT_TABLE_ADDRESS,
                                 VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH);
    PMwrite(address + offset, value);
    return 0;
}
