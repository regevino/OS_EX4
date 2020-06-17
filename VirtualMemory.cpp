#include <cstddef>
#include <algorithm>
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define ROOT_TABLE_OFFSET (VIRTUAL_ADDRESS_WIDTH - (TABLES_DEPTH * OFFSET_WIDTH))

#define ROOT_TABLE_ADDRESS 0

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

uint64_t cyclicDistance(uint64_t pageIndex,uint64_t currentPageIndex)
{
	return std::min(NUM_PAGES - std::abs((long long)(pageIndex- currentPageIndex)), std::abs((long long)(pageIndex- currentPageIndex)));
}

uint64_t evict(uint64_t victim, uint64_t newPage);

uint64_t restorePage(uint64_t pageIndex, uint64_t currentPageIndex = 0, uint64_t tableAddress=0, size_t&& highestAddress = 0, uint64_t&&victim = 0)
{
	if (tableAddress > highestAddress)
	{
		highestAddress = tableAddress;
	}
	if (cyclicDistance(pageIndex, currentPageIndex) > cyclicDistance(pageIndex, victim))
	{
		victim = currentPageIndex;
	}
	if (isEmptyTable(tableAddress))
	{
		// Only zero on highest level of recursion, if root table is empty.
		return tableAddress;
	}
	for (size_t index = 0; index < PAGE_SIZE; ++index)
	{
		word_t address;
		PMread(tableAddress + index, &address);
		uint64_t result = restorePage(pageIndex,currentPageIndex + index , address, std::move(highestAddress), std::move(victim));
		if (result)
		{
			return address;
		}
	}
	if (highestAddress + PAGE_SIZE < NUM_FRAMES * PAGE_SIZE)
	{
		return highestAddress + PAGE_SIZE;
	}
	return evict(victim, pageIndex);
}

uint64_t findFrame(uint64_t pageIndex, size_t offset=ROOT_TABLE_OFFSET, uint64_t tableAddress=0, size_t width=VIRTUAL_ADDRESS_WIDTH,
				   bool eviction = false)
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
    	if (eviction && width == offset)
		{
    		PMwrite(tableAddress + tableIndex, 0);
		}
        return findFrame(pageIndex, OFFSET_WIDTH, entry, width - offset, eviction);
    }
    else
    {
    	uint64_t nextFrame = restorePage(pageIndex ^ (!mask));
    	uint64_t result = findFrame(pageIndex, OFFSET_WIDTH, nextFrame, width - offset, eviction);
        PMwrite(tableAddress + tableIndex, nextFrame);
        return result;
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
    uint64_t address = findFrame(virtualAddress);
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
    uint64_t address = findFrame(virtualAddress);
    PMwrite(address + offset, value);
    return 0;
}
uint64_t evict(uint64_t victim, uint64_t newPage)
{
	uint64_t frameAddress = findFrame(victim);
	uint64_t frameIndex = frameAddress / NUM_FRAMES;
	PMevict(frameAddress, victim);
	PMrestore(frameIndex, newPage);
	return frameAddress;
}
