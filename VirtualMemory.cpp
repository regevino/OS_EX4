#include <cstddef>
#include <algorithm>
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define ROOT_TABLE_OFFSET (VIRTUAL_ADDRESS_WIDTH - (TABLES_DEPTH * OFFSET_WIDTH))

#define ROOT_TABLE_ADDRESS 0

void clearTable(uint64_t frameIndex)
{
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

bool isEmptyTable(uint64_t frameAddress)
{
    for (size_t i = 0; i < PAGE_SIZE; ++i)
    {
        word_t value;
        PMread(frameAddress + i, &value);
        if (value)
        {
            return false;
        }
    }
    return true;
}

uint64_t abs(uint64_t a, uint64_t b)
{
    return std::max(a, b) - std::min(a,b);
}

uint64_t cyclicDistance(uint64_t pageIndex, uint64_t currentPageIndex)
{
    return std::min((uint64_t )NUM_PAGES - abs(pageIndex, currentPageIndex), abs(pageIndex, currentPageIndex));
}

uint64_t evict(uint64_t victim, uint64_t newPage);

uint64_t
findFreeFrame(uint64_t pageIndex, uint64_t currentPageIndex = 0, uint64_t &&highestAddress = 0, uint64_t &&victim = 0,
              uint64_t frameAddress = 0, size_t depth = 0, size_t currentOffsetWidth = ROOT_TABLE_OFFSET)
{

    if (frameAddress > highestAddress)
    {
        highestAddress = frameAddress;
    }
    if (TABLES_DEPTH == depth)
    {
        if (cyclicDistance(pageIndex, currentPageIndex >> OFFSET_WIDTH) > cyclicDistance(pageIndex, victim))
        {
            victim = currentPageIndex >> OFFSET_WIDTH;
        }
        return 0;
    }
    if (frameAddress != 0 && depth < TABLES_DEPTH - 1 && isEmptyTable(frameAddress))
    {
        return frameAddress;
    }
//    if (depth == TABLES_DEPTH -1)
//    {
//        if (cyclicDistance(pageIndex, currentPageIndex) > cyclicDistance(pageIndex, victim))
//        {
//            victim = currentPageIndex;
//        }
//    }
    for (size_t index = 0; index < PAGE_SIZE; ++index)
    {
        word_t address;
        PMread(frameAddress + index, &address);
        if (address)
        {
            uint64_t result = findFreeFrame(pageIndex, (currentPageIndex + index) << currentOffsetWidth,
                                            std::move(highestAddress),
                                            std::move(victim), address, depth + 1, OFFSET_WIDTH);
            if (result)
            {
                if (result == address)
                {
                    PMwrite(frameAddress + index, 0);
                }
                return result;
            }
        }
    }
    if (depth == 0)
    {
        if (highestAddress + PAGE_SIZE < NUM_FRAMES * PAGE_SIZE)
        {
            return highestAddress + PAGE_SIZE;
        }
        return evict(victim, pageIndex);
    }
    return 0;
}

uint64_t
findFrame(uint64_t pageIndex, uint64_t currentPageIndex, bool eviction = false, size_t offset = ROOT_TABLE_OFFSET,
          uint64_t tableAddress = 0, size_t width = VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH)
{
    if (width == 0)
    {
        return tableAddress;
    }
    word_t entry;
    uint64_t tableIndex = currentPageIndex >> (width - offset);
    uint64_t mask = tableIndex << (width - offset);
    PMread(tableAddress + tableIndex, &entry);
    if (entry)
    {
        if (eviction && width == offset)
        {
            PMwrite(tableAddress + tableIndex, 0);
        }
        return findFrame(pageIndex, currentPageIndex ^ mask, eviction, OFFSET_WIDTH, entry, width - offset);
    }
    else
    {
        uint64_t highestAddress = tableAddress;
        uint64_t nextFrame = findFreeFrame(pageIndex, 0, std::move(highestAddress), pageIndex + 0);
        if (width == offset)
        {
            PMrestore(nextFrame / PAGE_SIZE, pageIndex);
        }
        else
        {
            clearTable(nextFrame / PAGE_SIZE);
        }
        uint64_t result = findFrame(pageIndex, currentPageIndex ^ mask, eviction, OFFSET_WIDTH, nextFrame,
                                    width - offset);
        PMwrite(tableAddress + tableIndex, nextFrame);
        return result;
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
    uint64_t address = findFrame(virtualAddress, virtualAddress);
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
    uint64_t address = findFrame(virtualAddress, virtualAddress);
    PMwrite(address + offset, value);
    return 0;
}

uint64_t evict(uint64_t victim, uint64_t newPage)
{
    uint64_t frameAddress = findFrame(victim, victim, true);
    uint64_t frameIndex = frameAddress / PAGE_SIZE;
    PMevict(frameIndex, victim);
//    PMrestore(frameIndex, newPage);
    return frameAddress;
}
