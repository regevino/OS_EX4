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
 * @brief Finds a free frame for a given page, according to the priorities specified in the exercise
 * description.
 * @param pageIndex Index of the page we are finding a frame for (or we are findong a frame for a
 * table that will index this page).
 * @param currentPageIndex Current path we took through the tree to the current frame.
 * @param highestAddress The highest address we have encountered so far.
 * @param victim The best victim we have encountered so far, in case we need to evict.
 * @param ignoreFrame A frame that may hold an empty table, but we should ignore that table because
 * it is part of the path to the page we are currently accessing.
 * @param frameAddress The address of the frame we are currently looking at.
 * @param depth The depth we are at in the table tree. starts at 0 and goes up till TREE_DEPTH.
 * @return the address of the free frame. shouldn't ever return 0.
 */
uint64_t findFreeFrame(uint64_t pageIndex, uint64_t currentPageIndex = 0,
                       uint64_t &&highestAddress = 0,
                       uint64_t &&victim = 0, uint64_t ignoreFrame = 0, uint64_t frameAddress = 0,
                       size_t depth = 0)
{

    if (frameAddress > highestAddress)
    {
    	// If needed, update the highest address.
        highestAddress = frameAddress;
    }
    if (TABLES_DEPTH == depth)
    {
    	// we've reached a page, so check its cyclic distance from our page, and update victim if
    	// this page is a better candidate.
    	currentPageIndex = currentPageIndex >> OFFSET_WIDTH;
        if (cyclicDistance(pageIndex, currentPageIndex) >
            cyclicDistance(pageIndex, victim))
        {
            victim = currentPageIndex;
        }
        return 0;
    }
    if (frameAddress != 0 && frameAddress != ignoreFrame && depth < TABLES_DEPTH &&
        isEmptyTable(frameAddress))
    {
    	// We've found an empty table, so return its address as a free frame.
        return frameAddress;
    }
    // Iterate through the entries in the current table and search for a free frame.
    for (size_t index = 0; index < PAGE_SIZE; ++index)
    {
        word_t address;
        PMread(frameAddress + index, &address);
        if (address)
        {
        	// This entry pints somewhere, so continune searching.
            uint64_t result = findFreeFrame(pageIndex, (currentPageIndex + index) << OFFSET_WIDTH,
                                            std::move(highestAddress), std::move(victim),
                                            ignoreFrame, address * PAGE_SIZE, depth + 1);
            if (result)
            {
            	// A table containing all zeros was found, so return it.
                if (result == (unsigned long long) address * PAGE_SIZE)
                {
                	// The frame we just returned from was an empty table, so remove it from this
                	// table.
                    PMwrite(frameAddress + index, 0);
                }
                return result;
            }
            // Nothing found here, keep searching.
        }
    }
    if (depth == 0)
    {
    	// We've iterated through the entire tree and no free frame was found, so either find the
    	// next unused frame or evict a page.
        if (highestAddress + PAGE_SIZE < NUM_FRAMES * PAGE_SIZE)
        {
            return highestAddress + PAGE_SIZE;
        }
        return evict(victim);
    }
    return 0;
}

/**
 * @brief Find the frame that holds the page we are trying to access. If there is no such frame,
 * page in and return the new frame.
 * @param pageIndex Index of the page we are trying to access.
 * @param currentPageIndex The remaining digits of the pageIndex.
 * @param eviction Whether or not we are evicting the frame we are searching for.
 * @param offset The width of the current offset we are dealing with.
 * @param tableAddress The address of the frame we are currently at.
 * @param width The remaining width of the pageIndex we still need to follow.
 * @return The (possibly newly allocated) frame that pageIndex resides in.
 */
uint64_t findFrame(uint64_t pageIndex, uint64_t currentPageIndex, bool eviction = false,
                   size_t offset = ROOT_TABLE_OFFSET, uint64_t tableAddress = 0,
                   size_t width = VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH)
{

    if (width == 0)
    {
    	// If we have reached the page, return the address.
        return tableAddress;
    }
    // Find the current index.
    word_t entry;
    uint64_t tableIndex = currentPageIndex >> (width - offset);
    uint64_t mask = tableIndex << (width - offset);
    PMread(tableAddress + tableIndex, &entry);
    if (entry)
    {
    	// This entry exists, so follow it.
        if (eviction && width == offset)
        {
        	// If we are trying to evict this page, and the address in this entry is the address of
        	// the page, we should reset the entry.
            PMwrite(tableAddress + tableIndex, 0);
        }
        return findFrame(pageIndex, currentPageIndex ^ mask, eviction, OFFSET_WIDTH,
                         entry * PAGE_SIZE, width - offset);
    }
    else
    {
    	// There is no frame holding the next table (or the page), so we need to create it.
        uint64_t nextFrame = findFreeFrame(pageIndex, 0, 0, pageIndex + 0,
                                           tableAddress);
        if (width == offset)
        {
        	// The next frame should hold the page, so restore it from disk.
            PMrestore(nextFrame / PAGE_SIZE, pageIndex);
        }
        else
        {
        	// The next frame will hold a table, so clear it.
            clearTable(nextFrame / PAGE_SIZE);
        }
        // Add the next frame to this table entry, and then follow it.
        PMwrite(tableAddress + tableIndex, nextFrame / PAGE_SIZE);
        uint64_t result = findFrame(pageIndex, currentPageIndex ^ mask, eviction, OFFSET_WIDTH,
                                    nextFrame, width - offset);
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
