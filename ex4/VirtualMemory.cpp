#include "VirtualMemory.h"
#include "PhysicalMemory.h"


void clearTable(uint64_t frameIndex) {
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

void VMinitialize() {
    clearTable(0);
}

/**
 * trnslates the virtual memory to physical
 * @param virtualAddress
 * @return physical memoery.
 */
unit64_t getPhysicalAddress(uint64_t virtualAddress)
{
    uint64_t tags[TABLES_DEPTH]; // creat tags
    uint64_t page = virtualAddress >> OFFSET_WIDTH; // find the page part of the address
    uint64_t tags[TABLES_DEPTH]; // creat tags
    for (int i = TABLES_DEPTH - 1; i >= 0; --i)
    {
        tags[i] = page & (PAGE_SIZE - 1);
        page >>= OFFSET_WIDTH;
    }
}

/* reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t* value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    unit64_t p_address = // add find adrress function.
    PMread(p_address, value);
    return 1;
}

/* writes a word to the given virtual address
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    unit64_t p_address =; // add find adrress function.
    PMwrite(p_address, value);
    return 1;
}
