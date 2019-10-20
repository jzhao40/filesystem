#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct Superblock {
         uint32_t magicNumber; //0xfa19283e
         uint32_t totalBlocks; //N
         uint32_t FATSize; // k = (N / 128) for 512B, N/2048 for 8KB
         uint32_t blockSize; // 512B or 8KB
         uint32_t rootBlock; // k + 1
};


struct Dir_entry {
         char fileName[24]; //File name (null-terminated string, up to 24 bytes long including the null)
         int64_t creationTime; //Creation time (64-bit integer)
         int64_t modificationTime; //Modification time (64-bit integer)
         int64_t accessTime; //Access time (64-bit integer)
         uint32_t fileLength; //File length in bytes
         int32_t startBlock;
         uint32_t flags;
         uint32_t unused;
};