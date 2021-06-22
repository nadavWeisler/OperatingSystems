#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <algorithm>

#define ABS(a, b) (std::max(a,b) - std::min(a,b))
#define OFFSET(virtualAddress) (virtualAddress & (PAGE_SIZE-1))

uint64_t findFreeFrame(uint64_t virtualAddress, word_t forbiddenToEvict);


/**
 * struct that holds data needed for tree traversing
 */
typedef struct Info
{
    word_t maxIndex;
    uint64_t emptyAddress;
    uint64_t emptyPhysical;
    word_t furthestFrame;
    uint64_t furthestPage;
    uint64_t furthestPhysical;
    int weights[TABLES_DEPTH];
    int maxweight;
} Info;

void UpdateSwap(Info &data, uint64_t currentPage, word_t pyAddress);


/**
 * clears table at index
 * @param frameIndex table index
 */
void clearTable(uint64_t frameIndex)
{
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}
/**
 * Initialize the virtual memory
 */
void VMinitialize()
{
    clearTable(0);
}

/**
 * splitting page number into tags
 * @param pageNumber original page number
 * @param tags array to update tags in
 */
void createTags(uint64_t pageNumber, uint64_t *tags)
{

    for (int i = TABLES_DEPTH - 1; i >= 0; --i)
    {
        tags[i] = pageNumber & (PAGE_SIZE - 1);
        pageNumber >>= OFFSET_WIDTH;
    }
}

/**
 * converts virtual address to physical address
 * @param virtualAddress virtual address
 * @return physical address
 */
uint64_t getPhysicalAddress(uint64_t virtualAddress)
{
    uint64_t tags[TABLES_DEPTH];
    uint64_t pageNumber = virtualAddress >> OFFSET_WIDTH;
    createTags(pageNumber, tags);
    word_t address = 0, forbiddenToEvict = 0;
    uint64_t frame = 0;

    for (int i = 0; i < TABLES_DEPTH; ++i)
    {
        uint64_t physicalAddress = address * PAGE_SIZE + tags[i];
        PMread(physicalAddress, &address);
        if (address == 0)
        {
            frame = findFreeFrame(pageNumber, forbiddenToEvict);
            i != TABLES_DEPTH - 1 ? clearTable(frame) : PMrestore(frame, pageNumber);
            PMwrite(physicalAddress, frame);
            address = frame;

        }
        forbiddenToEvict = address;


    }
    return address * PAGE_SIZE + OFFSET(virtualAddress);;

}

/**
 * reads a word from the given virtual address and puts its content in *value.
 * @param virtualAddress virtual address to read from
 * @param value word to be updated with read value
 * @return 1 if succeeded, 0 else
 */
int VMread(uint64_t virtualAddress, word_t *value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    uint64_t physicalAddress = getPhysicalAddress(virtualAddress);
    PMread(physicalAddress, value);
    return 1;
}

/**
 * writes a word to the given virtual address
 * @param virtualAddress virtual address to write to
 * @param value word to be written
 * @return 1 if succeeded, 0 else
 */
int VMwrite(uint64_t virtualAddress, word_t value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    uint64_t physicalAddress = getPhysicalAddress(virtualAddress);
    PMwrite(physicalAddress, value);
    return 1;
}

/**
 * recursive dfs algorithm to traverse tree in order to find free frame
 * @param address root address
 * @param forbiddenToEvict frame that can't be evicted
 * @param depth current depth
 * @param physicalAddress parent address
 * @param pageSwap virtual address
 * @param currentPage current page
 * @param info struct to be updated
 */
void DFS(word_t address, word_t forbiddenToEvict, int depth,
         word_t physicalAddress, uint64_t pageSwap, uint64_t currentPage, Info &info)
{
    info.maxIndex = address > info.maxIndex ? address : info.maxIndex;
    if (depth == TABLES_DEPTH)
    {
        UpdateSwap(info, currentPage, physicalAddress);
        return;
    }
    word_t child, frame;
    uint64_t physical;
    bool isEmpty = true; //all rows of frame is 0
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        physical = address * PAGE_SIZE + i;
        PMread(physical, &child);
        if (child != 0)
        {
            if (currentPage % 2 == 0)
            {
                info.weights[depth] = WEIGHT_EVEN;
            }
            else
            {
                info.weights[depth] = WEIGHT_ODD;
            }
            isEmpty = false;
            DFS(child, forbiddenToEvict, depth + 1, physical, pageSwap,
                (currentPage << OFFSET_WIDTH) + i, info);
        }

    }
    PMread(physicalAddress, &frame);
    if (isEmpty && frame != forbiddenToEvict)
    {
        info.emptyAddress = address;
        info.emptyPhysical = physicalAddress;

    }
}

/**
 * updates info struct with furthest frame data
 * @param physicalAddress physical address of the frame
 * @param pageSwap virtual address
 * @param currentPage current furthest
 * @param info struct to be updated
 */
//void
//updateFurthestData(word_t physicalAddress, uint64_t pageSwap, uint64_t currentPage, Info &info)
//{
//    uint64_t absPageDist = ABS(pageSwap, currentPage);
//    uint64_t distance = std::min((uint64_t) NUM_PAGES - absPageDist, absPageDist);
//    if (distance > info.maxDistance)
//    {
//        info.maxDistance = distance;
//        info.furthestPage = currentPage;
//        PMread(physicalAddress, &info.furthestFrame);
//        info.furthestPhysical = physicalAddress;
//    }
//}

void UpdateSwap(Info &data, uint64_t currentPage, word_t pyAddress) {
    int newWeight = 0;
    for (int i = 0; i < TABLES_DEPTH; i++) {
        newWeight += data.weights[i];
    }
    if (newWeight > data.maxweight) {
        data.maxweight = newWeight;
        data.furthestPage = currentPage;
        PMread(pyAddress, &data.furthestFrame);
        data.furthestPhysical = pyAddress;
    }
}
/**
 * finds free frame
 * @param pageNumber virtual address, without offset
 * @param forbiddenToEvict address that can't be evicted
 * @return
 */
uint64_t findFreeFrame(uint64_t pageNumber, word_t forbiddenToEvict)
{
    Info info = {};
    DFS(0, forbiddenToEvict, 0, 0, pageNumber, 0, info);
    if (info.emptyAddress != 0)
    {
        //case no 1 - empty cell
        PMwrite(info.emptyPhysical, 0);
        return info.emptyAddress;
    }
    if (info.maxIndex + 1 < NUM_FRAMES)
    {
        //case no 2 - unused
        return info.maxIndex + 1;
    }
    //case no 3 - evicting furthest frame
    PMevict(info.furthestFrame, info.furthestPage);
    PMwrite(info.furthestPhysical, 0);
    return info.furthestFrame;
}