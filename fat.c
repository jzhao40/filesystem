#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "math.h"
#include <stdlib.h>

#include "fs_util.h"

#define BLOCKSIZE 512 //Confirgurable Block Size 

struct Superblock sBlock;
int diskFD; //disk file descriptor
const int numdirectories = BLOCKSIZE/64;
struct Dir_entry directory[BLOCKSIZE/64]; //Data Block Buffer
int blockNum = 0;
int k = 0;
uint32_t* fattable;


//  struct dir_entry dir_block[]; //Block Size / total number of blocks

//helper function to write to block
int blockWrite(int fd, int blockNum, void* writeBlock){
	lseek(fd, blockNum*BLOCKSIZE, SEEK_SET);
	return write(fd, writeBlock, BLOCKSIZE);
}

int fatWrite(int fd, void* writeBlock){
	lseek(fd, BLOCKSIZE, SEEK_SET);
	return write(fd, writeBlock, sBlock.FATSize*BLOCKSIZE);
}

//helper function to read from block
int blockRead(int fd, int blockNum, void* readBlock){
	lseek(fd, blockNum*BLOCKSIZE, SEEK_SET);
	return read(fd, readBlock, BLOCKSIZE);
}

int fatRead(int fd, void* readBlock){
	lseek(fd, BLOCKSIZE, SEEK_SET);
	return read(fd, readBlock, sBlock.FATSize*BLOCKSIZE);
}
int get_next_entry_in_FAT(){
        int freeFATEntry = 0;
        //Looping through FAT  
        while(freeFATEntry != blockNum){
                if(fattable[freeFATEntry] == 0){
                        //Found Free Block at index countFAT
                        //Set the last block in the chain to be -2
                        fattable[freeFATEntry] = -2;
                        return freeFATEntry;
                } else{
                        freeFATEntry++;
                }
        }
        return freeFATEntry;
}

//helper function to parse a given path into a char* array
int get_path_args(const char* path, char* args[50]){
	const char p[2] = "/";
	char* token;
	char* tmp = strdup(path);
	token = strtok(tmp, p);
	int cnt = 0;
	while(token != NULL){
		args[cnt] = token;
		cnt++;
		token = strtok(NULL, p);
	}
	return cnt; 
}

int get_list_size(int start, int* last, int fat_entries[50]){
	int cnt = 0;
	int temp = start;
	fat_entries[cnt] = fattable[temp]; 
	while(fattable[temp] != -2 && (fat_entries[cnt] = fattable[temp])){
		temp = fattable[temp];
		cnt++;
	}
	*last = temp;
	return cnt+1;
}

int get_start_block(const char* path){
	int start_block;
	char* temp = strdup(path); //store the path
	char* path_args[50];
	int path_len = 0;
	path_len = get_path_args(temp, path_args);
	int root_block = sBlock.rootBlock;
	int list_size = 0;
	int j = -1;
	int last;
	int fat_entries[50];
	start_block = sBlock.rootBlock;
	int flag = 0;
	for(int i = 0; i < path_len; ++i){
		//get size of list in FAT for directory entry
		list_size = get_list_size(start_block, &last, fat_entries);
		flag = 0;
		for(int k = 0; k < list_size; k++){ //if not 0, more data blocks to scan
			if(i == 0){
				//read root block into directory buffer
				blockRead(diskFD, root_block, (void*) directory);
			}
			else{
				blockRead(diskFD, fat_entries[k], (void*) directory);
			}
			for(j = 0; j < numdirectories; ++j){
				//it's a match
				if(!strcmp(directory[j].fileName, path_args[i-1])){
					//is a valid directory entry
					if((directory[j].startBlock != 0) && (directory[j].flags == 1)){
					   	//memcpy(entry, &directory[j], sizeof(struct Dir_entry));
						start_block = directory[j].startBlock;
						flag = 1;
						break;
					}
					else 
						return -1;
				}
			}
			if(flag == 1)
			    break;
		}
	}
	//memcpy(entry, &directory[j], sizeof(struct Dir_entry));
	return start_block;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
    struct fuse_file_info *fi){
        
    int start = get_start_block(path);

	int last_pos;
	char* path_args[50];
	int cnt = get_path_args(path, path_args);
	
	int fat_entries[50];
	int list_size = get_list_size(start, &last_pos, fat_entries);
	int matching_pos = -1;
	int tmp_pos;
	int flag = 0;
    int i = 0;
    
	for(int j = 0; j < list_size; ++j){
		tmp_pos = fat_entries[j];
		blockRead(diskFD, tmp_pos, (void*) directory);
		while(directory[i].startBlock != 0){
		    filler(buf, directory[i].fileName, NULL, 0);
		    i++;
		}
// 		for(int i = 0; i < numdirectories; ++i){
// 			if(directory[i].startBlock == -2){
// 				matching_pos = i;
// 				flag = 1;
// 				break;
// 			}
// 		}

	}
    

    
    return 0;
}

static void* fs_init(struct fuse_conn_info* conn)
{
    int k;
    /**DISK INITALIZATION**/

    //stores in struct, then writes s 
    struct Superblock *sBlock = calloc(1, BLOCKSIZE);
     
    sBlock->magicNumber = 0xfa19283e;
    sBlock->totalBlocks = blockNum;
    //calculating fat size 
    sBlock->FATSize = ceil(blockNum /(BLOCKSIZE/4)); 
    sBlock->blockSize = BLOCKSIZE;
    sBlock->rootBlock = sBlock->FATSize + 1;
    
    k = sBlock->rootBlock;
    //writting superblock on 1st block
    blockWrite(diskFD,0,sBlock);
    uint32_t* fatBuff = calloc(sBlock->FATSize, BLOCKSIZE);
    for(int i=0; i<sBlock->FATSize; i++){
        for(int j=0; j<(BLOCKSIZE/4); j++){
        int temp = i*(BLOCKSIZE/4)+j;
		//Making it -1 for unusable blocks(super and FAT) and 0 for the valid data blocks
		if(temp < k){
		    fatBuff[j] = -1;
		}
		else if(temp == k)
			fatBuff[j] = -2;
		else{
		    fatBuff[j] = 0;   
		}
	} 
        //k -= (BLOCKSIZE/4); //Make sure the rest of the entries are 0 
        blockWrite(diskFD,1+i,fatBuff);	
    }
    
    //struct Dir_entry *rootDir = calloc(BLOCKSIZE,sBLock->FATSize);
    //setting rootdir(first entry in block)
    blockRead(diskFD, k, (void*) directory);
    directory[0].flags = 0;
    directory[0].fileLength = 64;//in bytes, only contains itself 
    //file name is 24 byte
    char rootName[24] =  "/";
    strcpy(directory[0].fileName,rootName);
    
    directory[0].startBlock = k;

    blockWrite(diskFD, k, (void*) directory);
    /***END INIT DISK**/
    
    fattable = calloc(blockNum, 32);

    return NULL;
}


static int fs_getattr(const char *path, struct stat *stats) {
	//read FAT table from disk
	fatRead(diskFD, (void*) fattable);
	//read path into 
	//void* newBlock = calloc(sizeof(struct Dir_entry),1);
	//struct Dir_entry *entry = newBlock;
	int start = get_start_block(path);

	int last_pos;
	char* path_args[50];
	int cnt = get_path_args(path, path_args);
	int fat_entries[50];
	int list_size = get_list_size(start, &last_pos, fat_entries);
	int matching_pos = -1;
	int tmp_pos;
	int flag = 0;
	//int root_flag = 0;
	char* name;
	for(int j = 0; j < list_size; ++j){
		tmp_pos = fat_entries[j];
		blockRead(diskFD, tmp_pos, (void*) directory);
		for(int i = 0; i < numdirectories; ++i){
		    if(cnt < 2)
		        name = "/";
		    else
		        name = path_args[cnt-2];
	    	if(!strcmp(directory[i].fileName, name)){
		    	matching_pos = i;
			    flag = 1;
		    	break;
		    }
	    }
		if(flag == 1)
			break;
	}
	if(matching_pos == -1)
		return -ENOENT;
		
	blockRead(diskFD, directory[matching_pos].startBlock, (void*) directory);

		stats->st_ctime = directory[0].creationTime;
		stats->st_mtime = directory[0].modificationTime;
		stats->st_atime = directory[0].accessTime;

		stats->st_dev = 0;
		stats->st_ino = 0;
		stats->st_nlink = 1;
		stats->st_uid = getuid();
		stats->st_gid = getgid();

		if((directory[0].flags & 1) == 1)
			stats->st_mode = 0040777;
		else
			stats->st_mode = 0100777;

		stats->st_blksize = sBlock.blockSize;
		stats->st_blocks = sBlock.totalBlocks;
		stats->st_size = sBlock.totalBlocks * BLOCKSIZE;
	
	//free(newBlock);
	return 0;
}

static int fs_mkdir(const char *path, mode_t mode){
	//read FAT table from disk
	fatRead(diskFD, (void*) fattable);

	int position = get_start_block(path);//get parent startBlock
	int tmp_pos = position;
	char* path_args[50];//string array to hold path
	int path_len = get_path_args(path, path_args);//length of path, up to parent
	int free = -1;

	//find free block in the FAT for new directory
	free = get_next_entry_in_FAT();
	int last_pos;
	//gets directory's list size and last block in the list
	int fat_entries[50];
	int list_size = get_list_size(position, &last_pos, fat_entries);
	int free_dir = -1;
	//find free spot in parent directory block
	int flag = 0;
	for(int j = 0; j < list_size; ++j){
		tmp_pos = fat_entries[j];
		blockRead(diskFD, tmp_pos, (void*) directory);
		for(int i = 0; i < numdirectories; ++i){
		//found free entry
			if(directory[i].startBlock == 0){
				free_dir = i;
				printf("freedir is at: %d\n", i);
				flag = 1;
				break;
			}
		}
		if(flag == 1)
			break;
	}
    
	//no free entry in directory block, need to add a data block to its list
	int free_next = -1;
	if(free_dir == -1){
		free_next = get_next_entry_in_FAT();//find new data block in FAT
		if(free_next == 0){ //no more free entries in FAT, err out
			printf("no more free entries in the FAT\n");
			return -ENOENT;
		}
		//update the FAT
		fattable[last_pos] = free_next;
		fattable[free_next] = -2;
		free_dir = 0; //free entry in directory block is first slot, which is 0
	}

	//new data block, need to read in and change position to reflect so can write 
	//back to correct data block
	if(free_dir == 0){
		blockRead(diskFD, free_next, (void*) directory);
		tmp_pos = free_next;
	}

	//write to parent directory
	char* entry_name = path_args[path_len-1];
	printf("the name of the new directory is: %s\n", entry_name);
	strcpy(directory[free_dir].fileName, entry_name);
	directory[free_dir].creationTime = time(NULL);
	directory[free_dir].modificationTime = time(NULL);
	directory[free_dir].accessTime = time(NULL);
	directory[free_dir].fileLength = 64;
	directory[free_dir].startBlock = free;
	directory[free_dir].flags = 1;

	//write block back for parent
	blockWrite(diskFD, tmp_pos, (void*) directory);

	//update parent's start block for file length
	blockRead(diskFD, position, (void*) directory);
	int old_file_length = directory[0].fileLength;
	directory[0].fileLength = old_file_length + 64;
	blockWrite(diskFD, position, (void*) directory);

	//read new data block for newly created directory
	blockRead(diskFD, free, (void*) directory);
	strcpy(directory[0].fileName, entry_name);
	directory[0].creationTime = time(NULL);
	directory[0].modificationTime = time(NULL);
	directory[0].accessTime = time(NULL);
	directory[0].fileLength = 64;
	directory[0].startBlock = free;
	directory[0].flags = 1;

	//write new directory block back
	blockWrite(diskFD, free, (void*) directory);

	//write FAT table back to disk
	fatWrite(diskFD, (void*) fattable);

	return 0;
}

static int fs_unlink(const char *path){

	struct Dir_entry *entry = calloc(sizeof(struct Dir_entry), 1);
	int start = get_start_block(path); //assumes a function to find the starting block given a path

	//not a valid file
	if((entry->flags & 1) != 0 ){
		return -ENOENT; 
	}

	blockRead(diskFD, start, (void*) directory);
	fatRead(diskFD, (void*) fattable);
    int last = 0;
    int fat_entry[50];
	int blockLength = get_list_size(start,&last,fat_entry); //assumes a function that finds number of blocks needed for the file

	int current = entry->startBlock;
	int next = 0;
	
	for (int i = 0; i < blockLength; ++i){
		next = fattable[current];
		fattable[current] = 0;
		directory[current].startBlock = 0;
		current = next;
	}

	blockWrite(diskFD, start, (void*)directory);
	fatWrite(diskFD, (void*) fattable);

	free(entry);
	return 0;
}

static int fs_rmdir(const char *path){
	//read FAT table from disk
	fatRead(diskFD, (void*) fattable);

	int offset;
	offset = 0;
	//struct Dir_entry *dir = calloc(sizeof(struct Dir_entry),1);
	int blockIndex = get_start_block(path);
    char* path_args[50];//string array to hold path
	int path_len = get_path_args(path, path_args);//length of path, up to parent
	int matching_pos = -1;
	int tmp_pos = blockIndex;
	int last_pos;
	int fat_entries[50];
	//gets directory's list size and last block in the list
	int list_size = get_list_size(blockIndex, &last_pos, fat_entries);
    //grabbing position in dir block
	int flag = 0;
	for(int j = 0; j < list_size; ++j){
		tmp_pos = fat_entries[j];
		blockRead(diskFD, tmp_pos, (void*) directory);
		for(int i = 0; i < numdirectories; ++i){
		//found matching directory entry
			if(!strcmp(directory[i].fileName, path_args[path_len])){
				matching_pos = i;
				flag = 1;
				break;
			}
		}
		if(flag == 1)
			break;
	}
	if(matching_pos == -1){
		printf("directory to be deleted doesn't exist\n");
		return -ENOENT;
	}

 	int deletionStart = directory[matching_pos].startBlock; //startBlock for child to be deleted

    blockRead(diskFD, deletionStart, (void*) directory);
    int k = 1;
    //making sure its empty
    while(directory[k].startBlock == 0){
        if(k == numdirectories){
            break;
        }
        k += 1;
    }
    if (k != numdirectories){
        //There was a dir entry inside the dir block, so dir not empty
        printf("directory not empty, cannot delete\n");
        return -ENOENT;
    }
    //go into FAT to free blocks
    list_size = get_list_size(deletionStart, &last_pos, fat_entries);
    for(int i = 0; i < list_size; i++){
    	fattable[fat_entries[i]] = 0;
    }

    //set startblock to 0 to mark as invalid   
    blockRead(diskFD, tmp_pos, (void*) directory);
    if(directory[matching_pos].flags != 1) //not a directory
    	return -ENOENT;
    directory[matching_pos].startBlock = 0;
    blockWrite(diskFD, tmp_pos, (void*) directory);

    //write fattable back
    fatWrite(diskFD, (void*) fattable);

	return 0;
}

static int fs_open(const char *path, struct fuse_file_info *fi) {

	int start = get_start_block(path);
	int start_block = -1;
	int fat_entries[50];
	char* path_args[50];
	int path_len = get_path_args(path, path_args);
	int last_pos;
	int list_size = get_list_size(start, &last_pos, fat_entries);
	//find spot in parent directory blocks
	int flag = 0;
	int matching_pos = -1;
	int tmp_pos = 0;
	for(int j = 0; j < list_size; ++j){
		tmp_pos = fat_entries[j];
		blockRead(diskFD, tmp_pos, (void*) directory);
		for(int i = 0; i < numdirectories; ++i){
		//found matching
			if(directory[i].fileName == path_args[path_len]){
				matching_pos = i;
				start_block = directory[i].startBlock;
				flag = 1;
				break;
			}
		}
		if(flag == 1)
			break;
	}
	if(matching_pos == -1){
	    printf("file doesn't exist, cannot be opened\n");
	    return -ENOENT;
	}
	if (start <= 0)
		return -ENOENT;

	return start_block;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
    int blockIndex,i,offsetDir,curr,prev,blockNumber;
        //read FAT table from disk
	fatRead(diskFD, (void*) fattable);
    //Block index of parrent  
    blockIndex = get_start_block(path);
    //reading into a directory block buffer
    // Directory now contains the parent block directory
    if(blockRead(diskFD,blockIndex,(void*) directory) == -1){
        
	    return -ENOENT;
    }
	int fat_entries[50];
        int last_pos;
        //gets directory's list size and last block in the list
        int list_size = get_list_size(blockIndex, &last_pos, fat_entries);
        int free_dir = -1;
        //find free spot in parent directory block
        int flag = 0;
        int tmp_pos = 0;
        for(int j = 0; j < list_size; ++j){
                tmp_pos = fat_entries[j];
                blockRead(diskFD, tmp_pos, (void*) directory);
                for(int i = 0; i < numdirectories; ++i){
                //found free entry
                        if(directory[i].startBlock == 0){
                                free_dir = i;
                                flag = 1;
                                break;
                        }
                }
                if(flag == 1)
                        break;
        }



    blockNumber = 0;
    //if we can fit inside one block
    if(directory[offsetDir].fileLength <= BLOCKSIZE){
        blockRead(diskFD,directory[offsetDir].startBlock,(void*)buf);
        return directory[offsetDir].fileLength; 
    } else {
	    //Adjusting FAT table to point to linked blocks 
        curr = fattable[directory[offsetDir].startBlock];
	    blockRead(diskFD,directory[offsetDir].startBlock,(void*)buf);
	    //reading blocks in until we hit EOF in fat
	    while(curr != -2){
	        prev = curr;
	        curr = fattable[curr];
	        //blockNum makes sure we are reading all blocks until EOF 
	        blockRead(diskFD,fattable[prev],(void*)buf + (blockNumber * BLOCKSIZE));
	        blockNumber += 1;
	    }
	    //write last blck in
	    blockRead(diskFD,fattable[curr],(void*)buf + (blockNumber * BLOCKSIZE));
    }
    return 0;

}

static int fs_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int i,curr,prev,sizeBuff,diff;
	sizeBuff = size;
	//read FAT table from disk
	fatRead(diskFD, (void*) fattable);
	int blockIndex = get_start_block(path);
	if(blockIndex == -1){
	  
	    return -ENOENT;
	}
	if (blockRead(diskFD, blockIndex, (void*)directory) == -1){

	    return -ENOENT;
        }
        int fat_entries[50];
        int last_pos;
        //gets directory's list size and last block in the list
        int list_size = get_list_size(blockIndex, &last_pos, fat_entries);
        int free_dir = -1;
        //find free spot in parent directory block
        int flag = 0;
        int tmp_pos = 0;
        for(int j = 0; j < list_size; ++j){
                tmp_pos = fat_entries[j];
                blockRead(diskFD, tmp_pos, (void*) directory);
                for(int i = 0; i < numdirectories; ++i){
                //found free entry
                        if(directory[i].startBlock == 0){
                                free_dir = i;
                                flag = 1;
                                break;
                        }
                }
                if(flag == 1)
                        break;
        }
        offset = free_dir;
	int blocksNeed = 1;
        //If its less than the block size we store in one block
	if(size <= BLOCKSIZE){
	    return blockWrite(diskFD,directory[offset].startBlock,(void*) buf);
	}//else we mark the next block in FAT(to point to the next), write to as many blocks as needed
        else{
            //pointing to next block
	    while(1){
                curr = fattable[directory[offset].startBlock];
                //write returns number of bytes written so we subtract until we have written all bytes
                diff = blockWrite(diskFD,fattable[prev],(void*)buf + (blocksNeed*BLOCKSIZE) );
                blocksNeed += 1;
                sizeBuff -= diff;
                if(sizeBuff < 0){
                        break;
                }
	    }
	}
	return 0;
}

//modified version of create function
static int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi){
	//read FAT table from disk
	fatRead(diskFD, (void*) fattable);

	int position = get_start_block(path);//get parent startBlock
	int tmp_pos = position;
	char* path_args[50];//string array to hold path
	int path_len = get_path_args(path, path_args);//length of path, up to parent
	int free = -1;

	//find free block in the FAT for new directory
	free = get_next_entry_in_FAT();
	int last_pos;
	int fat_entries[50];
	//gets directory's list size and last block in the list
	int list_size = get_list_size(position, &last_pos, fat_entries);
	int free_dir = -1;

	//find free spot in parent directory block
	int flag = 0;
	for(int j = 0; j < list_size; ++j){
		tmp_pos = fat_entries[j];
		blockRead(diskFD, tmp_pos, (void*) directory);
		for(int i = 0; i < numdirectories; ++i){
		//found free entry
			if(directory[i].startBlock == 0){
				free_dir = i;
				flag = 1;
				break;
			}
		}
		if(flag == 1)
			break;
	}

	//no free entry in directory block, need to add a data block to its list
	int free_next = -1;
	if(free_dir == -1){
		free_next = get_next_entry_in_FAT();//find new data block in FAT
		if(free_next == -1){ //no more free entries in FAT, err out
			printf("no more free entries in the FAT\n");
			return -ENOENT;
		}
		//update the FAT
		fattable[last_pos] = free_next;
		fattable[free_next] = -2;
		free_dir = 0; //free entry in directory block is first slot, which is 0
	}

	//new data block, need to read in and change position to reflect so can write 
	//back to correct data block
	if(free_dir == 0){
		blockRead(diskFD, free_next, (void*) directory);
		tmp_pos = free_next;
	}

	//write to parent directory
	char* entry_name = path_args[path_len];
	printf("the name of the new file is: %s", entry_name);
	strcpy(directory[free_dir].fileName, entry_name);
	directory[free_dir].creationTime = time(NULL);
	directory[free_dir].modificationTime = time(NULL);
	directory[free_dir].accessTime = time(NULL);
	directory[free_dir].fileLength = 0;
	directory[free_dir].startBlock = free;
	directory[free_dir].flags = 1;

	//write block back for parent
	blockWrite(diskFD, tmp_pos, (void*) directory);

	//update parent's start block for file length
	blockRead(diskFD, position, (void*) directory);
	int old_file_length = directory[0].fileLength;
	directory[0].fileLength = old_file_length + 64;
	blockWrite(diskFD, position, (void*) directory);

	//read new data block for newly created directory
	blockRead(diskFD, free, (void*) directory);
	strcpy(directory[0].fileName, entry_name);
	directory[0].creationTime = time(NULL);
	directory[0].modificationTime = time(NULL);
	directory[0].accessTime = time(NULL);
	directory[0].fileLength = 0;
	directory[0].startBlock = free;
	directory[0].flags = 1;

	//write new directory block back
	blockWrite(diskFD, free, (void*) directory);

	//write FAT table back to disk
	fatWrite(diskFD, (void*) fattable);

	return 0;
}

static struct fuse_operations fs_oper = {
        .getattr        = fs_getattr,
        .mkdir          = fs_mkdir,
        .rmdir          = fs_rmdir,
        .unlink         = fs_unlink,
        .open           = fs_open,
        .read           = fs_read,
        .write          = fs_write,
        .create         = fs_create,
        .init           = fs_init,
        .readdir        = fs_readdir,
};

int main(int argc, char *argv[])
{
	char command[200] = {0};
        sprintf(command, "./mount.sh %s %s", argv[1], argv[2]);
        //system(command);
        char* file = "test";
        int i;
        sscanf(argv[2],"%d",&i);
        blockNum = 10;
        if ((diskFD = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0777)) < 0){
                perror("failed to open file\n");
                return -1;
        }
        return fuse_main(argc, argv, &fs_oper, NULL);
}

