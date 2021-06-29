#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <algorithm>

#define OFFSET(virtualAddress) (virtualAddress & (PAGE_SIZE - 1))

/**
 * Struct for tree data
 */
typedef struct TreeData
{
    word_t maxIndex;
    uint64_t freeAddress;
    uint64_t freePhysicalAddress;
    uint64_t mostRemotePage;
    uint64_t mostRemotePhysical;
    word_t mostRemoteFrame;
    int weights[TABLES_DEPTH];
    int maxweight;
} TreeData;

/*
 * Clears table at index
 */
void clearTable(uint64_t frameIndex)
{
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

/*
 * Initialize the virtual memory
 */
void VMinitialize()
{
    clearTable(0);
}

/*
 * Split page number to tags
 */
void splitToTags(uint64_t pageNum, uint64_t *tags)
{
    for (int i = TABLES_DEPTH - 1; i >= 0; i--)
    {
        tags[i] = pageNum & (PAGE_SIZE - 1);
        pageNum >>= OFFSET_WIDTH;
    }
}

/*
* Swap with hard drive memory
*/
void UpdateSwap(TreeData &data, uint64_t currentPage, word_t physicalAddress)
{
    int newWeight = 0;
    for (int i = 0; i < TABLES_DEPTH; i++)
    {
        newWeight += data.weights[i];
    }
    if (newWeight > data.maxweight)
    {
        data.maxweight = newWeight;
        data.mostRemotePage = currentPage;
        PMread(physicalAddress, &data.mostRemoteFrame);
        data.mostRemotePhysical = physicalAddress;
    }
}

/**
 * check for braking point in recursive DFS
 */
void checkBreakingPoint(int depth, uint64_t currentPage,TreeData &treeData,word_t physicalAddress)
{
    if (depth == TABLES_DEPTH)
    {
        UpdateSwap(treeData, currentPage, physicalAddress);
        return;
    }
}

/**
 * sets the address to the bew free address find by DFS, only if not all rows where diffrent from 0
 * @param rowsNotEmpty
 * @param frame
 * @param notEvict
 * @param treeData
 * @param address
 * @param physicalAddress
 */
void setFreeAddress(bool rowsNotEmpty,word_t frame, word_t notEvict,TreeData &treeData,word_t address,
        word_t physicalAddress)
{
    if (!rowsNotEmpty && frame != notEvict)
    {
        treeData.freePhysicalAddress = physicalAddress;
        treeData.freeAddress = address;
    }
}


/*
 * Dfs algorithm
*/
void DFS(word_t address, word_t physicalAddress, word_t notEvict, uint64_t pageSwap, uint64_t currentPage,
         int depth, TreeData &treeData)
{
    if (treeData.maxIndex < address)
    {
        treeData.maxIndex = address;
    }
    checkBreakingPoint(depth, currentPage, treeData, physicalAddress);
    word_t subTree;
    word_t frame;
    uint64_t physical;
    bool rowsNotEmpty = false;
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        physical = address * PAGE_SIZE + i;
        PMread(physical, &subTree);
        if (subTree != 0)
        {
            if (currentPage % 2 == 0)
            {
                treeData.weights[depth] = WEIGHT_EVEN;
            }
            else
            {
                treeData.weights[depth] = WEIGHT_ODD;
            }
            rowsNotEmpty = true;
            DFS(subTree, notEvict, depth + 1, physical, pageSwap,
                (currentPage << OFFSET_WIDTH) + i, treeData);
        }
    }
    PMread(physicalAddress, &frame);
    setFreeAddress(rowsNotEmpty, frame, notEvict, treeData, address, physicalAddress);
}

/*
 * Get next free frame
 */
uint64_t getNextFrame(uint64_t pageNum, word_t notEvict)
{
    TreeData data = {};
    DFS(0, 0, notEvict, pageNum, 0, 0, data);
    if (data.freeAddress != 0)
    {
        PMwrite(data.freePhysicalAddress, 0);
        return data.freeAddress;
    }
    if (data.maxIndex < NUM_FRAMES -1)
    {
        data.maxIndex++;
        return data.maxIndex;
    }
    PMevict(data.mostRemoteFrame, data.mostRemotePage);
    PMwrite(data.mostRemotePhysical, 0);
    return data.mostRemoteFrame;
}

/*
 * Converts virtual address to physical address
 */
uint64_t getPhysicalAddress(uint64_t virtualAddress)
{
    word_t address = 0;
    word_t notEvict = 0;
    uint64_t frame = 0;
    uint64_t pageNum = virtualAddress >> OFFSET_WIDTH;
    uint64_t tags[TABLES_DEPTH];
    splitToTags(pageNum, tags);
    for (int i = 0; i < TABLES_DEPTH; i++)
    {
        uint64_t physicalAddress = address * PAGE_SIZE + tags[i];
        PMread(physicalAddress, &address);
        if (address == 0)
        {
            frame = getNextFrame(pageNum, notEvict);
            if (i == TABLES_DEPTH - 1)
            {
                PMrestore(frame, pageNum);
            }
            else
            {
                clearTable(frame);
            }
            PMwrite(physicalAddress, frame);
            address = frame;
        }
        notEvict = address;
    }
    return address * PAGE_SIZE + OFFSET(virtualAddress);
}

/*
 * Reads a word from the given virtual address and puts its content in *value.
 */
int VMread(uint64_t virtualAddress, word_t *value)
{
    if (virtualAddress < VIRTUAL_MEMORY_SIZE)
    {
        uint64_t physicalAddress = getPhysicalAddress(virtualAddress);
        PMread(physicalAddress, value);
        return 1;
    }
    return 0;
}

/*
 * Writes a word to the given virtual address
 */
int VMwrite(uint64_t virtualAddress, word_t value)
{
    if (virtualAddress < VIRTUAL_MEMORY_SIZE)
    {
        uint64_t physicalAddress = getPhysicalAddress(virtualAddress);
        PMwrite(physicalAddress, value);
        return 1;
    }
    return 0;
}