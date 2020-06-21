#include <cstddef>
#include <algorithm>
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define ROOT_TABLE_OFFSET (VIRTUAL_ADDRESS_WIDTH - (TABLES_DEPTH * OFFSET_WIDTH))

/**
 * @brief Evicts a page from its frame.
 * @param victim the page to evict.
 * @return the address of the free frame.
 */
uint64_t evict(uint64_t victim);

/**
 * @brief Clears the page table.
 * @param frameIndex the index from which to start clearing the table.
 */
void clearTable(uint64_t frameIndex)
{
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

/**
 * @brief Checks if a page's frame is empty.
 * @param frameAddress the address of the corresponding frame in the physical memory.
 * @return true iff there are no values in the page table.
 */
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

/**
 * @brief Calculates the distance between two given page addresses.
 * @param a the first page address.
 * @param b the second page address.
 * @return the distance between the page addresses.
 */
uint64_t abs(uint64_t a, uint64_t b)
{
    return std::max(a, b) - std::min(a, b);
}

/**
 * @brief Calculates the cyclical distance between two pages.
 * @param pageIndex the index of the first page.
 * @param currentPageIndex the index of the second page.
 * @return the cyclical distance between the pages.
 */
uint64_t cyclicDistance(uint64_t pageIndex, uint64_t currentPageIndex)
{
    return std::min((uint64_t) NUM_PAGES - abs(pageIndex, currentPageIndex),
                    abs(pageIndex, currentPageIndex));
}

/**
 * @brief Finds a free frame for a given page.
 * @param pageIndex
 * @param currentPageIndex
 * @param highestAddress
 * @param victim
 * @param ignoreFrame
 * @param frameAddress
 * @param depth
 * @return the address of the free frame, 0 if .
 */
uint64_t findFreeFrame(uint64_t pageIndex, uint64_t currentPageIndex = 0,
                       uint64_t &&highestAddress = 0,
                       uint64_t &&victim = 0, uint64_t ignoreFrame = 0, uint64_t frameAddress = 0,
                       size_t depth = 0)
{

    if (frameAddress > highestAddress)
    {
        highestAddress = frameAddress;
    }
    if (TABLES_DEPTH == depth)
    {
        if (cyclicDistance(pageIndex, currentPageIndex >> OFFSET_WIDTH) >
            cyclicDistance(pageIndex, victim))
        {
            victim = currentPageIndex >> OFFSET_WIDTH;
        }
        return 0;
    }
    if (frameAddress != 0 && frameAddress != ignoreFrame && depth < TABLES_DEPTH &&
        isEmptyTable(frameAddress))
    {
        return frameAddress;
    }
    for (size_t index = 0; index < PAGE_SIZE; ++index)
    {
        word_t address;
        PMread(frameAddress + index, &address);
        if (address)
        {
            uint64_t result = findFreeFrame(pageIndex, (currentPageIndex + index) << OFFSET_WIDTH,
                                            std::move(highestAddress), std::move(victim),
                                            ignoreFrame, address * PAGE_SIZE, depth + 1);
            if (result)
            {
                if (result == (unsigned long long) address * PAGE_SIZE)
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
        return evict(victim);
    }
    return 0;
}

/**
 * @brief
 * @param pageIndex
 * @param currentPageIndex
 * @param eviction
 * @param offset
 * @param tableAddress
 * @param width
 * @param highestAddress
 * @return
 */
uint64_t findFrame(uint64_t pageIndex, uint64_t currentPageIndex, bool eviction = false,
                   size_t offset = ROOT_TABLE_OFFSET, uint64_t tableAddress = 0,
                   size_t width = VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH,
                   uint64_t &&highestAddress = 0)
{
    if (tableAddress > highestAddress)
    {
        highestAddress = tableAddress;
    }
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
        return findFrame(pageIndex, currentPageIndex ^ mask, eviction, OFFSET_WIDTH,
                         entry * PAGE_SIZE, width - offset, std::move(highestAddress));
    }
    else
    {
        uint64_t nextFrame = findFreeFrame(pageIndex, 0, std::move(highestAddress), pageIndex + 0,
                                           tableAddress);
        if (width == offset)
        {
            PMrestore(nextFrame / PAGE_SIZE, pageIndex);
        }
        else
        {
            clearTable(nextFrame / PAGE_SIZE);
        }
        PMwrite(tableAddress + tableIndex, nextFrame / PAGE_SIZE);
        uint64_t result = findFrame(pageIndex, currentPageIndex ^ mask, eviction, OFFSET_WIDTH,
                                    nextFrame, width - offset, std::move(highestAddress));
        return result;
    }
}

/**
 * Initializes the virtual memory
 */
void VMinitialize()
{
    clearTable(0);
}

/** Reads a word from the given virtual address and puts its content in *value.
 *
 *  returns 1 on success.
 *  returns 0 on failure (if the address cannot be mapped to a physical
 *  address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t *value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        //TODO
        return 0;
    }
    uint64_t offset = 1LL;
    for (int i = 1; i < OFFSET_WIDTH; ++i)
    {
        offset = offset << 1UL;
        offset++;
    }
    offset = offset & virtualAddress;
    virtualAddress = virtualAddress >> OFFSET_WIDTH;
    uint64_t address = findFrame(virtualAddress, virtualAddress);
    PMread(address + offset, value);
    return 1;
}

/** Writes a word to the given virtual address
 *
 *  returns 1 on success.
 *  returns 0 on failure (if the address cannot be mapped to a physical
 *  address for any reason)
 */
int VMwrite(uint64_t virtualAddress, word_t value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        //TODO
        return 0;
    }
    uint64_t offset = 1LL;
    for (int i = 1; i < OFFSET_WIDTH; ++i)
    {
        offset = offset << 1UL;
        offset++;
    }
    offset = offset & virtualAddress;
    virtualAddress = virtualAddress >> OFFSET_WIDTH;
    uint64_t address = findFrame(virtualAddress, virtualAddress);
    PMwrite(address + offset, value);
    return 1;
}

uint64_t evict(uint64_t victim)
{
    uint64_t frameAddress = findFrame(victim, victim, true);
    uint64_t frameIndex = frameAddress / PAGE_SIZE;
    PMevict(frameIndex, victim);
    return frameAddress;
}
