#include "VirtualMemory.h"
#include "PhysicalMemory.h"

using namespace std;

typedef struct TreeData {
    word_t maxFrameIndex;
    word_t swapFrame;
    uint64_t swapPage;
    uint64_t swapPAddress;
    uint64_t freeAddress;
    uint64_t freePAddress;
    int max_weight;

} TreeData;


void clearTable(uint64_t frameIndex) {
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

void UpdateSwap(TreeData &data, int weights[TABLES_DEPTH], uint64_t currentPage, word_t pyAddress) {
    int newWeight = 0;
    for (int  i = 0; i < TABLES_DEPTH; i++)
    {
        newWeight += weights[i];
    }
    if (newWeight > data.max_weight)
    {
        data.max_weight = newWeight;
        data.swapPage = currentPage;
        PMread(pyAddress, &data.swapFrame);
        data.swapPAddress = pyAddress;
    }
}

/**
 * runs dfs to find empty frame
 * @param currentAddress - the address for thr root frame (start with 0)
 * @param notEvict the frame that cant be evicted (the root frame)
 * @param depth current depth of the tree
 * @param pAddress the parents address
 * @param page current page
 * @param data struct with data for traversing the tree
 */
void DFS(word_t currentAddress, word_t notEvict, int depth, word_t pAddress, uint64_t page,
         TreeData &data, int weights[TABLES_DEPTH]) {
    // if current address is bigger update maxFrameIndex // todo: should break maybe?
    if (currentAddress > data.maxFrameIndex) {
        data.maxFrameIndex = currentAddress;
    }
    // breaking point for dfs, also updates the swapping info if necessary
    if (depth == TABLES_DEPTH) {
        UpdateSwap(data, weights, page, pAddress);
        return;
    }
    word_t subTree; // set sutree for dfs
    bool allRowsEmpty = true; // if all rows remain empty dont have o go eny deeper.
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        uint64_t currentPyAddress = currentAddress * PAGE_SIZE + i; // update physical address
        PMread(currentPyAddress, &subTree); // update subtree (virtual address)
        // todo using this we should update the weight
        if (subTree != 0) {
            if (page % 2 == 0)
            {
                weights[depth] = WEIGHT_EVEN;
            } else
            {
                weights[depth] = WEIGHT_ODD;
            }
            allRowsEmpty = false; // set all rows empty to false so the dfs will go on
            // call dfs on child
            DFS(subTree, notEvict, depth + 1, currentPyAddress,
                (page << (unsigned  int) OFFSET_WIDTH) + i, data, weights);
        }
    }
    word_t frame;
    PMread(pAddress, &frame);
    if (allRowsEmpty && frame != notEvict) {
        data.freeAddress = currentAddress;
        data.freePAddress = pAddress;
    }
}

uint64_t getNewFrame(word_t notEvict) {
    TreeData data = {};
    int weights[TABLES_DEPTH];
    DFS(0, notEvict, 0, 0, 0, data, weights);
    //Case 1: do not evict, remove reference
    if (data.freeAddress != 0) {
        PMwrite(data.freePAddress, 0); // remove link from parents
        return data.freeAddress;
    }
    //Case 2: Unused frame:
    if (data.maxFrameIndex < NUM_FRAMES - 1) { // this frame is unused and we can use ut.
        return data.maxFrameIndex + 1;
    }
    if (data.maxFrameIndex >= NUM_FRAMES - 1) { // no frame is unused and we need to evict the chosen frame.
        // need to find what page to swap.
        PMevict(data.swapFrame, data.swapPage);
        PMwrite(data.swapPAddress, 0);
        return data.swapFrame;
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
uint64_t getPhysicalAddress(uint64_t virtualAddress) {
    uint64_t tags[TABLES_DEPTH]; // create tags
    uint64_t page = virtualAddress >> OFFSET_WIDTH; // find the page part of the address
    word_t currentAddress = 0;
    word_t notEvict = 0;
    uint64_t frame = 0;
    for (int i = TABLES_DEPTH - 1; i >= 0; i--) {
        tags[i] = page & (PAGE_SIZE - 1);
        page >>= OFFSET_WIDTH;
    }
    for (int i = 0; i < TABLES_DEPTH; i++) {
        uint64_t pAddress = currentAddress * PAGE_SIZE + tags[i];
        PMread(pAddress, &currentAddress); //First translate
        //address not in ram and need to create, so Find free frame
        if (currentAddress == 0) {
            frame = getNewFrame(notEvict); // update frame
            if (i == TABLES_DEPTH - 1) {
                PMrestore(frame, page);
            } else {
                clearTable(frame);
            }
            PMwrite(pAddress, frame);
        }
        notEvict = currentAddress;
    }
    return currentAddress * PAGE_SIZE + (virtualAddress & (PAGE_SIZE - 1));
}

/* reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t *value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t p_address = getPhysicalAddress(virtualAddress); // add find adrress function.
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
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t p_address = getPhysicalAddress(virtualAddress); // add find adrress function.
    PMwrite(p_address, value);
    return 1;
}
