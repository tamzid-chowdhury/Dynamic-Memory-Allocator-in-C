/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

int getBlocksize(size_t size); //size after adding heading and padding for allignment
void initFreeLists();
int getFreeListIndex(int size, int numLists);
int fib(int n);
void insert_in_free_list(sf_block* sf_free_list_head, sf_block* freeBlock);
void remove_from_free_list(sf_block* blockToRemove);
sf_block* searchFreeListForBlock(sf_block* sf_free_list_head, int blocksize);
sf_block* allocateBlock(sf_block* foundFreeBlock, int blocksize);
int isValidToFree(void* ptr);
void coalesceAndInsert(sf_block* block);

sf_block* prologue;
sf_block* epilogue;

void *sf_malloc(size_t size) {
    if(size == 0)
        return NULL;

    int blocksize = getBlocksize(size);

    if(sf_mem_start() == sf_mem_end()){ //heap has not been initialized

        initFreeLists();
        sf_mem_grow();

        prologue = (sf_mem_start() + 48); //prologue starts after 48 bytes
        prologue->header = 64;
        prologue->header |= THIS_BLOCK_ALLOCATED;

        int freeSpaceAvailable = PAGE_SZ - (64 + 56 + 8); //prologue + padding + epilogue

        while(freeSpaceAvailable < blocksize){
            if(sf_mem_grow() == NULL){
                sf_block* freeBlock = (sf_block*) ((void*) prologue + 64);
                freeBlock->header = freeSpaceAvailable;
                freeBlock->header |= PREV_BLOCK_ALLOCATED;
                freeBlock->prev_footer = prologue->header;

                int index = getFreeListIndex(freeSpaceAvailable, NUM_FREE_LISTS);
                insert_in_free_list(&sf_free_list_heads[index], freeBlock);

                epilogue = (sf_mem_end() - 16);
                epilogue->header = 0;
                epilogue->header |= THIS_BLOCK_ALLOCATED;
                epilogue->prev_footer = freeBlock->header;

                sf_errno = ENOMEM;
                return NULL;
            }
            freeSpaceAvailable += PAGE_SZ;
        }

        sf_block* firstBlock = (sf_mem_start() + 48 + 64);
        firstBlock->header = blocksize;
        firstBlock->header |= THIS_BLOCK_ALLOCATED;
        firstBlock->header |= PREV_BLOCK_ALLOCATED;
        firstBlock->prev_footer = prologue->header;

        int freeSpaceAfterAllocation = freeSpaceAvailable - blocksize;


        if(freeSpaceAfterAllocation) {
            sf_block* freeBlock = (sf_block*) ((void*) firstBlock + blocksize);
            freeBlock->header= freeSpaceAfterAllocation;
            freeBlock->header |= PREV_BLOCK_ALLOCATED;
            freeBlock->prev_footer = firstBlock->header;

            epilogue = (sf_mem_end() - 16);
            epilogue->header = 0;
            epilogue->header |= THIS_BLOCK_ALLOCATED;
            epilogue->prev_footer = freeBlock->header;

            int index = getFreeListIndex(freeSpaceAfterAllocation, NUM_FREE_LISTS);
            insert_in_free_list(&sf_free_list_heads[index],freeBlock);


            return (void*) firstBlock+16;
        }

        else {
            epilogue = (sf_mem_end() - 16);
            epilogue->header = 0;
            epilogue->header |= THIS_BLOCK_ALLOCATED;
            epilogue->header |= PREV_BLOCK_ALLOCATED;
        }


        return (void*) firstBlock+16;

    }

    int startingIndex = getFreeListIndex(blocksize, NUM_FREE_LISTS);

    for(int i = startingIndex; i < NUM_FREE_LISTS; i++){
        sf_block* foundFreeBlock = NULL;
        foundFreeBlock = searchFreeListForBlock(&sf_free_list_heads[i],blocksize);
        if(foundFreeBlock != NULL){
            sf_block* freeBlockAllocated = allocateBlock(foundFreeBlock, blocksize);
            return (void*) freeBlockAllocated+16;
        }
    }


    while(sf_mem_grow() != NULL){

        sf_block* newBlock = epilogue;

        if((newBlock->header & PREV_BLOCK_ALLOCATED)){
            newBlock->header = PAGE_SZ;
            newBlock->header |= PREV_BLOCK_ALLOCATED;

            int index = getFreeListIndex((newBlock->header & ~0x3),NUM_FREE_LISTS);
            insert_in_free_list(&sf_free_list_heads[index],newBlock);


            epilogue = (sf_mem_end() - 16);
            epilogue->header = 0;
            epilogue->header |= THIS_BLOCK_ALLOCATED;
            epilogue->prev_footer = newBlock->header;
        }
        else{
            int prevBlocksize = newBlock->prev_footer & ~0x3;
            sf_block* prevBlock = (sf_block*) ((void*) newBlock - prevBlocksize);
            remove_from_free_list(prevBlock);

            prevBlock->header += PAGE_SZ;

            int index = getFreeListIndex((prevBlock->header & ~0x3),NUM_FREE_LISTS);
            insert_in_free_list(&sf_free_list_heads[index],prevBlock);


            epilogue = (sf_mem_end() - 16);
            epilogue->header = 0;
            epilogue->header |= THIS_BLOCK_ALLOCATED;
            epilogue->prev_footer = prevBlock->header;
        }

        for(int i = startingIndex; i < NUM_FREE_LISTS; i++){
            sf_block* foundFreeBlock = NULL;
            foundFreeBlock = searchFreeListForBlock(&sf_free_list_heads[i],blocksize);
            if(foundFreeBlock != NULL){
                sf_block* freeBlockAllocated = allocateBlock(foundFreeBlock, blocksize);
                return (void*) freeBlockAllocated+16;
            }
        }

    }

    sf_errno = ENOMEM;
    return NULL;

}

void sf_free(void *pp) {

    if(!isValidToFree(pp)){
        abort();
    }

    sf_block* allocatedBlock = (sf_block*) ((void*)pp - 16);
    allocatedBlock->header -= 1; //get rid of allocated bit

    int allocatedBlockSize = allocatedBlock->header & ~0x3;

    sf_block* nextBlock = (sf_block*) ((void*)allocatedBlock + allocatedBlockSize);
    nextBlock->prev_footer = allocatedBlock->header;
    nextBlock->header -= (nextBlock->header & PREV_BLOCK_ALLOCATED);

    coalesceAndInsert(allocatedBlock);

    return;
}

void *sf_realloc(void *pp, size_t rsize) {

    if(!isValidToFree(pp)){
        sf_errno = EINVAL;
        abort();
    }

    if(rsize == 0){
        sf_free(pp);
        return NULL;
    }

    sf_block* allocatedBlock = (sf_block*) ((void*)pp - 16);

    int allocatedBlockSize = allocatedBlock->header & ~0x3;

    int newBlockSize = getBlocksize(rsize);

    if(allocatedBlockSize == newBlockSize)
        return pp;

    if(allocatedBlockSize < newBlockSize){
        void* newBlock;
        if((newBlock = sf_malloc(rsize)) == NULL){
            return NULL;
        }
        memcpy(newBlock, pp, allocatedBlockSize-8);
        sf_free(pp);
        return newBlock;
    }

    if(allocatedBlockSize > newBlockSize){ //splitting without splitner
        int remainder = allocatedBlockSize - newBlockSize;
        allocatedBlock->header -= remainder;

        sf_block* freeBlockAfterSplit = (sf_block*) ((void*)allocatedBlock + (allocatedBlock->header & ~0x3));

        freeBlockAfterSplit->header = remainder;
        freeBlockAfterSplit->header |= PREV_BLOCK_ALLOCATED;

        sf_block* nextBlockAfterSplit = (sf_block*) ((void*)freeBlockAfterSplit + remainder);


        if((nextBlockAfterSplit->header & THIS_BLOCK_ALLOCATED)){ //next block allocated so no caolescing
            nextBlockAfterSplit->prev_footer = freeBlockAfterSplit->header;
            int index = getFreeListIndex((freeBlockAfterSplit->header & ~0x3),NUM_FREE_LISTS);
            insert_in_free_list(&sf_free_list_heads[index],freeBlockAfterSplit);
            return (void*) allocatedBlock+16;
        }
        else{
           int nextBlocksize = nextBlockAfterSplit->header & ~0x3;
           remove_from_free_list(nextBlockAfterSplit);
           freeBlockAfterSplit->header += nextBlocksize;
           int index = getFreeListIndex((freeBlockAfterSplit->header & ~0x3),NUM_FREE_LISTS);
           insert_in_free_list(&sf_free_list_heads[index],freeBlockAfterSplit);
           sf_block* nextnextBlock = (sf_block*) ((void*)freeBlockAfterSplit + (freeBlockAfterSplit->header & ~0x3));
           nextnextBlock->prev_footer = freeBlockAfterSplit->header;
           return (void*) allocatedBlock+16;
        }
    }


    return NULL;
}

int getBlocksize(size_t size) {
    int sizewithheader = size + 8; //header is 8 bytes
    int padding = 64 - (sizewithheader % 64); //how much to add to become a multiple of 64
    return sizewithheader + padding;
}

void initFreeLists(){
    //initialize free list by assigning next and prev feilds of sentinal nodes to themselves
    for(int i = 0; i < NUM_FREE_LISTS; i++){
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }
}

int getFreeListIndex(int size, int numLists){
    if(size == 1*64){
        return 0;
    }

    if(size == 2*64){
        return 1;
    }

    if(size == 3*64){
        return 2;
    }

    for(int i = 3; i < numLists-1; i++){
        if(size > fib(i)*64 && size <= fib(i+1)*64){
            return i;
        }
    }
    return numLists-1;
}

int fib(int n){
    if(n == 0)
        return 1;
    else if(n == 1)
        return 1;
    else return (fib(n-1)+fib(n-2));
}

void insert_in_free_list(sf_block* sf_free_list_head, sf_block* freeBlock){
    //if the free list is empty
    if(sf_free_list_head->body.links.next == sf_free_list_head && sf_free_list_head->body.links.prev == sf_free_list_head){
        sf_free_list_head->body.links.next = freeBlock;
        freeBlock->body.links.prev = sf_free_list_head;
        freeBlock->body.links.next = sf_free_list_head;
        sf_free_list_head->body.links.prev = freeBlock;
    }
    else{
        sf_free_list_head->body.links.next->body.links.prev = freeBlock;
        freeBlock->body.links.next = sf_free_list_head->body.links.next;
        freeBlock->body.links.prev = sf_free_list_head;
        sf_free_list_head->body.links.next = freeBlock;
    }

}

void remove_from_free_list(sf_block* blockToRemove){
    blockToRemove->body.links.next->body.links.prev = blockToRemove->body.links.prev;
    blockToRemove->body.links.prev->body.links.next = blockToRemove->body.links.next;
    blockToRemove->body.links.next = NULL;
    blockToRemove->body.links.prev = NULL;

}

sf_block* searchFreeListForBlock(sf_block* sf_free_list_head, int blocksize){
    sf_block* cursor = sf_free_list_head;
    while((cursor = cursor->body.links.next) != sf_free_list_head){
        int cursorBlocksize = cursor->header & ~0x3;
        if(cursorBlocksize >= blocksize){
            cursor->body.links.next->body.links.prev = cursor->body.links.prev;
            cursor->body.links.prev->body.links.next = cursor->body.links.next;
            cursor->body.links.next = NULL;
            cursor->body.links.prev = NULL;

            return cursor;
        }
    }
    return NULL;
}


//found a free block now allocate it but make sure to split if there is enough for another free block
sf_block* allocateBlock(sf_block* foundFreeBlock, int blocksize){

    int foundBlockSize = foundFreeBlock->header & ~0X3;
    int remainder = foundBlockSize - blocksize;

    if(remainder == 0){
        foundFreeBlock->header |= THIS_BLOCK_ALLOCATED;
        sf_block* blockAfter = (sf_block*) ((void*)foundFreeBlock + foundBlockSize);
        blockAfter->header |= PREV_BLOCK_ALLOCATED;
        return foundFreeBlock;
    }
    else{
        foundFreeBlock->header -= remainder;
        foundFreeBlock->header |= THIS_BLOCK_ALLOCATED;

        sf_block* freeBlockAfterSplit = (sf_block*) ((void*)foundFreeBlock + blocksize);

        freeBlockAfterSplit->header = remainder;
        freeBlockAfterSplit->header |= PREV_BLOCK_ALLOCATED;

        sf_block* nextBlockAfterSplit = (sf_block*) ((void*)freeBlockAfterSplit + remainder);


        if((nextBlockAfterSplit->header & THIS_BLOCK_ALLOCATED)){ //next block allocated so no caolescing
            nextBlockAfterSplit->prev_footer = freeBlockAfterSplit->header;
            int index = getFreeListIndex((freeBlockAfterSplit->header & ~0x3),NUM_FREE_LISTS);
            insert_in_free_list(&sf_free_list_heads[index],freeBlockAfterSplit);
            return foundFreeBlock;
        }
        else{

           int nextBlocksize = nextBlockAfterSplit->header & ~0x3;
           remove_from_free_list(nextBlockAfterSplit);
           freeBlockAfterSplit->header += nextBlocksize;
           int index = getFreeListIndex((freeBlockAfterSplit->header & ~0x3),NUM_FREE_LISTS);
           insert_in_free_list(&sf_free_list_heads[index],freeBlockAfterSplit);
           sf_block* nextnextBlock = (sf_block*) ((void*)freeBlockAfterSplit + (freeBlockAfterSplit->header & ~0x3));
           nextnextBlock->prev_footer = freeBlockAfterSplit->header;
           return foundFreeBlock;
        }




    }
    return NULL;
}

int isValidToFree(void* ptr){

    if(ptr == NULL) //null ptr cant be freed
        return 0;

    if((uintptr_t) ptr % 64 != 0) //check if ptr is 64 based alligned
        return 0;


    sf_block* allocatedBlock = (sf_block*) ((void*)ptr - 16);


    if((allocatedBlock->header & THIS_BLOCK_ALLOCATED) == 0){
        return 0; //block not allocated
    }


    sf_block* headerAddress = (sf_block*) ((void*) allocatedBlock + 8);
    sf_block* footerAddress = (sf_block*) ((void*) allocatedBlock + (allocatedBlock->header & ~0x3));

    if((headerAddress < ((sf_block*) ((void*)sf_mem_start() + 120))) || (footerAddress > ((sf_block*) ((void*)sf_mem_end() - 24))))
        return 0; //if out of bounds

    if((allocatedBlock->header & PREV_BLOCK_ALLOCATED) == 0){
        int prevBlocksize = (allocatedBlock->prev_footer & ~0x3);
        sf_block* prevBlock = (sf_block*) ((void*)allocatedBlock - prevBlocksize);
        if((prevBlock->header & THIS_BLOCK_ALLOCATED) != 0)
            return 0;
    }

    return 1;
}

void coalesceAndInsert(sf_block* block){

    int blocksize = (block->header & ~0x3);

    sf_block* nextBlock = (sf_block*) ((void*) block + blocksize);

    int nextBlocksize = (nextBlock->header & ~0x3);

    if((nextBlock->header & THIS_BLOCK_ALLOCATED) == 0){ //next block is free so we should coalesce
        remove_from_free_list(nextBlock);
        blocksize += nextBlocksize;
        block->header += nextBlocksize;
        sf_block* blockAfterNext = (sf_block*) ((void*) block + blocksize);
        blockAfterNext->prev_footer = block->header;
        nextBlock = blockAfterNext;
    }

    if((block->header & PREV_BLOCK_ALLOCATED) == 0){
        int prevBlocksize = (block->prev_footer & ~0x3);
        sf_block* prevBlock = (sf_block*) ((void*) block - prevBlocksize);
        remove_from_free_list(prevBlock);
        prevBlock->header += blocksize;
        block = prevBlock;
        nextBlock->prev_footer = block->header;
    }

    int index = getFreeListIndex((block->header & ~0x3),NUM_FREE_LISTS);
    insert_in_free_list(&sf_free_list_heads[index],block);

}