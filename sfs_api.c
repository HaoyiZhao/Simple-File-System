
#include "sfs_api.h"
#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
#include <strings.h>
#include "disk_emu.h"
#define diskName "sfs_disk.disk"


/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)

#define FALSE 0
#define TRUE 1


//initialize all bits to high
uint8_t free_bit_map[BITMAP_ROW_SIZE] = { [0 ... BITMAP_ROW_SIZE - 1] = UINT8_MAX };

/***********************************************************************************
* 							Global variables/data structures
************************************************************************************/
file_descriptor fileDescriptorTable[max_inode_number];
directory_entry rootDirectory[max_inode_number];
inode_t inodeTable[max_inode_number]; 
superblock_t *superblock = NULL;
int nextFileCounter = 0;
int count = 0;
void *temp;

/**********************************************************************************
* 								Bitmap methods
***********************************************************************************/
void force_set_index(uint32_t index) {
    // Used to force indicies to used 
    // this is the opposite of rm_index. 
    uint32_t i = index / 8;

    // get which bit to set to used
    uint8_t bit = index % 8;

    // set bit to used
    USE_BIT(free_bit_map[i], bit);
}

uint32_t get_index() {
    uint32_t i = 0;

    // find the first section with a free bit
    // let's ignore overflow for now...
    while (free_bit_map[i] == 0) { i++; }

    // now, find the first free bit
    /*
        The ffs() function returns the position of the first (least
       significant) bit set in the word i.  The least significant bit is
       position 1 and the most significant position is, for example, 32 or
       64.  
    */
    // Since ffs has the lsb as 1, not 0. So we need to subtract
    uint8_t bit = ffs(free_bit_map[i]) - 1;

    // set the bit to used
    USE_BIT(free_bit_map[i], bit);

    //return which block we used
    return i*8 + bit;
}

void rm_index(uint32_t index) {

    // get index in array of which bit to free
    uint32_t i = index / 8;

    // get which bit to free
    uint8_t bit = index % 8;

    // free bit
    FREE_BIT(free_bit_map[i], bit);
}

/********************************************************************************
* 								Size calculation methods
*********************************************************************************/
int calculateNumberOfBlocksNeeded(size_t input) { // calculate how many blocks needed
    int result = (input + (BLOCK_SIZE-1))/BLOCK_SIZE;

    return result;
}

int calculateSizeNeeded(size_t input) { //calculate size to store in blocks
    int x = calculateNumberOfBlocksNeeded(input);
    int result = x * BLOCK_SIZE;
    return result;
}

/*********************************************************************************
*						Data structure initialization methods
**********************************************************************************/
void init_fdt(){
	for(int i=0;i<max_inode_number; i++){
		fileDescriptorTable[i].inodeIndex = -1;
	}
}

void init_int(){
	for(int i=0; i<max_inode_number; i++){
		inodeTable[i].size = -1;
		inodeTable[i].indirectPointer = -1;
	}
}

void init_super(){
	superblock = calloc(1, sizeof(superblock_t));
		superblock->magic = 0xACBD0005;
		superblock->block_size = BLOCK_SIZE;
		superblock->fs_size = number_of_blocks;
		superblock->inode_table_len = 100;
		superblock->root_dir_inode = 0;
}

void init_root(){
	for(int i=0; i<max_inode_number; i++){
		rootDirectory[i].num = -1;
	}
}

/*********************************************************************************
*								Helper Methods
**********************************************************************************/

//validates the filename given file name input
int validateFileName(char *input){ 
    int fileNameCounter = 0;
    while(input[fileNameCounter] != '.'){
        fileNameCounter++;
    }
    // checks if we have 16 or less chars before period, or if no other chars before period
    if (fileNameCounter>16 || fileNameCounter<1) { 
        return FALSE; // error
    }
    else {
        int extensionCounter=fileNameCounter+1;
        while(input[extensionCounter] != '\0'){
            extensionCounter++;
        }
        // check if we only have up to 3 chars after the period
        if(extensionCounter - (fileNameCounter+1) > 3 || extensionCounter - (fileNameCounter+1) < 1) 
            return FALSE; // error
        else {
            return TRUE;
        }

    }
}
//find file inode given filename
int findFileInode(char *filename){
	for(int i=0; i<max_inode_number;i++){
		if(strcmp(rootDirectory[i].name,filename) == 0){
			return rootDirectory[i].num;
		}
	}
	return -1;
}

//find next available file descriptor table slot
int findNextFreeFileDescriptor(){
	for(int i=0; i<max_inode_number; i++){
		if(fileDescriptorTable[i].inodeIndex == -1){
			return i;
		}
	}
	//return -1 if no more available file descriptor slots
	return -1;
}

//find next available root directory slot
int findNextFreeFileSlot(){
	for(int i=0; i<max_inode_number; i++){
		if(rootDirectory[i].num == -1){
			return i;
		}
	}
	//return -1 if no more root directory slots
	return -1;

}

//find next available inode, return -1 if no more slots in table
int findNextFreeInode(){
	for(int i=0; i<max_inode_number; i++){
		if(inodeTable[i].size == -1){
			return i;
		}
	}
	return -1;
}

int checkIfFileOpen(char *name){
	int inode = findFileInode(name);
	if(inode == -1){
		return -1;
	}
	for(int i=0; i<max_inode_number; i++){
		if(fileDescriptorTable[i].inodeIndex == inode){
			return i;
		}
	}
	return -1;
}

/*********************************************************************************
*									API methods
**********************************************************************************/
void mksfs(int fresh) {
	printf("Creating Simple File System\n");
	//initialize file descriptor table
	init_fdt();
	//if fresh file system
	if(fresh == 1){
		//remove previous filesystem and initialize fresh disk
		init_int();
		init_super();
		init_root();
		remove(diskName);
		init_fresh_disk(diskName,BLOCK_SIZE,number_of_blocks);

		temp = calloc(1,calculateSizeNeeded(sizeof(superblock_t)));
		count = write_blocks(superblock_index,1,temp);
		if(count < 0){
			printf("Error writing superblock\n");
			return;
		}
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}
		/*
		Initialize in-memory data structures and write to disk
		*/

		//initialize inode table
		init_int();
		inodeTable[0].size = sizeof(rootDirectory);
		inodeTable[0].data_ptrs[0] = root_directory_index;
		inodeTable[0].data_ptrs[1] = root_directory_index + 1;
		inodeTable[0].data_ptrs[2] = root_directory_index + 2;
		//create temporary buffer which will be used to write to disk
		temp = calloc(1,calculateSizeNeeded(sizeof(inodeTable)));
		memcpy(temp,inodeTable,sizeof(inodeTable));
		count = write_blocks(inode_table_index,calculateNumberOfBlocksNeeded(sizeof(inodeTable)),temp);
		if(count < 0){
			printf("Error writing inode table\n");
			return;
		}
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}

		//initialize directory table
		//create temporary buffer which will be used to write to disk
		temp = calloc(1,calculateSizeNeeded(sizeof(rootDirectory)));
		memcpy(temp,rootDirectory,sizeof(rootDirectory));
		count = write_blocks(root_directory_index,calculateNumberOfBlocksNeeded(sizeof(rootDirectory)),temp);
		if(count < 0){
			printf("Error writing directory table\n");
			return;
		}
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}

		fileDescriptorTable[0].inodeIndex = 0;
		fileDescriptorTable[0].inode = &inodeTable[0];

		//initialize bitmap, with first 13 bits allocated to superblock,inode table,directory table and bitmap itself
		for(uint32_t i=0; i<data_block_index; i++){
			force_set_index(i);
		}

		//flush bitmap to disk
		temp = calloc(1,calculateSizeNeeded(sizeof(free_bit_map)));
		memcpy(temp,free_bit_map,sizeof(free_bit_map));
		count = write_blocks(bitmap_index,calculateNumberOfBlocksNeeded(sizeof(free_bit_map)),temp);
		if(count < 0){
			printf("Error writing bitmap\n");
			return;
		}
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}
	}
	//if file system not fresh
	else{
		init_disk(diskName,BLOCK_SIZE,number_of_blocks);

		//read superblock
		temp = calloc(1,calculateSizeNeeded(sizeof(superblock_t)));
		int count = read_blocks(superblock_index,1,temp);
		if(count < 0){
			printf("Error reading superblock\n");
			return;
		}
		memcpy(superblock,temp,sizeof(superblock_t));
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}

		//read inode table
		temp = calloc(1,calculateSizeNeeded(sizeof(inodeTable)));
		count = read_blocks(inode_table_index,calculateNumberOfBlocksNeeded(sizeof(inodeTable)),temp);
		if(count < 0){
			printf("Error reading inode table\n");
			return;
		}
		memcpy(inodeTable,temp,sizeof(inodeTable));
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}

		//read root directory table
		temp = calloc(1,calculateSizeNeeded(sizeof(rootDirectory)));
		count = read_blocks(root_directory_index,calculateNumberOfBlocksNeeded(sizeof(rootDirectory)),temp);
		if(count < 0){
			printf("Error reading root directory table\n");
			return;
		}
		memcpy(rootDirectory,temp,sizeof(rootDirectory));
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}

		//read bitmap 
		temp = calloc(1,calculateSizeNeeded(sizeof(free_bit_map)));
		count = read_blocks(bitmap_index,calculateNumberOfBlocksNeeded(sizeof(free_bit_map)),temp);
		if(count < 0){
			printf("Error reading free bitmap\n");
			return;
		}
		memcpy(free_bit_map,temp,sizeof(free_bit_map));
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}
		fileDescriptorTable[0].inodeIndex = 0;
		fileDescriptorTable[0].inode = &inodeTable[0];
	}
}

int sfs_getnextfilename(char *fname){
	while(1){
		if(rootDirectory[nextFileCounter].num != -1){
			// probably wrong line of code here
			snprintf(fname,21,rootDirectory[nextFileCounter].name);
			return rootDirectory[nextFileCounter++].num;
		}
		else{
			nextFileCounter++;
		}
		if(nextFileCounter == 100){
				nextFileCounter = 0;
				return 0;
		}
		
	}
}

int sfs_getfilesize(const char* path){
	//validate file name
	if(validateFileName(path) == FALSE){
		return -1;
	}
	//if file exists, return file size obtained from inode
	int inodeNumber = findFileInode(path);
	if(inodeNumber != -1){
		return inodeTable[inodeNumber].size;
	}
	//file doesn't exist, return -1 for error
	else{
		return -1;
	}
}
int sfs_fopen(char *name){
	//check to see if valid file name
	if(validateFileName(name) == FALSE){
		// printf("invalid file name %s; can't be opened\n",name);
		return -1;
	}
	//check to see if file exists
	int inodeNumber = findFileInode(name);

	//If file is already open, just set rwptr to size of file and return
	int fdIndex = checkIfFileOpen(name); 
	if(fdIndex >= 0){
		fileDescriptorTable[fdIndex].rwptr = inodeTable[inodeNumber].size;
		return fdIndex;
	}
	
	//if file is not open, or does not exist, find next available file descriptor slot
	int nextFileID = findNextFreeFileDescriptor();
	//if fd table full, just return -1
	if(nextFileID == -1){
		// printf("No more file descriptor table slots,can't open file %s\n",name);
		return -1;
	}
	//if file doesn't exist then create file and flush to disk, otherwise just open and set file descriptor entry fields; 
	if(inodeNumber == -1){
		int rootID = findNextFreeFileSlot();
		//if root directory table full, return -1
		if(rootID == -1){
			// printf("No more root directory slots, can't open file %s\n",name);
			return -1;
		}
		snprintf(rootDirectory[rootID].name,21,name);
		//if inode table full, return -1 (num of directory entry will still be -1 so can be reused)
		inodeNumber = findNextFreeInode();
		if(inodeNumber == -1){
			// printf("No more inode table slots, can't open file %s\n",name);
			return -1;
		}
		rootDirectory[rootID].num = inodeNumber;
		inodeTable[inodeNumber].size = 0;

		//write updated root directory and inode table to disk
		temp = calloc(1,calculateSizeNeeded(sizeof(inodeTable)));
		memcpy(temp,rootDirectory,sizeof(inodeTable));
		count = write_blocks(inode_table_index,calculateNumberOfBlocksNeeded(sizeof(inodeTable)),temp);
		if(count < 0){
			printf("Error writing inode table\n");
			// return;
		}

		if(temp != NULL){
			free(temp);
			temp = NULL;
		}

		temp = calloc(1,calculateSizeNeeded(sizeof(rootDirectory)));
		memcpy(temp,rootDirectory,sizeof(rootDirectory));
		count = write_blocks(root_directory_index,calculateNumberOfBlocksNeeded(sizeof(rootDirectory)),temp);
		if(count < 0){
			// printf("Error writing root directory\n");
			return -1;
		}
		if(temp != NULL){
			free(temp);
			temp = NULL;
		}
	}
	fileDescriptorTable[nextFileID].inodeIndex = inodeNumber;
	fileDescriptorTable[nextFileID].inode = &inodeTable[inodeNumber];
	fileDescriptorTable[nextFileID].rwptr = inodeTable[inodeNumber].size;
	return nextFileID;

}
int sfs_fclose(int fileID) {
	if(fileDescriptorTable[fileID].inodeIndex == -1 || fileID > 99 || fileID < 0){
		return -1;
	}
	else{
		fileDescriptorTable[fileID].inodeIndex = -1;
	}
	return 0;
}

int sfs_fread(int fileID, char *buf, int length) {
	int bytesRead = 0;
	int bytesToRead = length;
	int inodeInd = fileDescriptorTable[fileID].inodeIndex;
	int indirectTable[256]; 
	for(int i=0;i<256;i++){
		indirectTable[i] = 0;
	}
	if(inodeInd == -1){
		// printf("File with fileID %i is not open, can't read\n",fileID);
		return -1;
	}
	if(length < 1 ){
		// printf("Invalid length specified (Length too small or too big)\n");
		return -1;
	}
	//if specified length > size of file, change bytes to read to be the difference between size and read write pointer
 	if(fileDescriptorTable[fileID].rwptr + length > inodeTable[inodeInd].size){
 		bytesToRead = inodeTable[inodeInd].size - fileDescriptorTable[fileID].rwptr;
 	}
	while(bytesToRead > 0){
		int currentReadLength = 0;
		int dataPointerIndex = fileDescriptorTable[fileID].rwptr/BLOCK_SIZE;
		//if we only need to read from direct data pointer block currently
		if(dataPointerIndex < 12){
			temp = calloc(1,BLOCK_SIZE);
			count = read_blocks(inodeTable[inodeInd].data_ptrs[dataPointerIndex],1,temp);
			if(count < 0){
				// printf("Error reading data\n");
				return -1;
			}
			
			//append available data from current block to buffer based on number of bytes read, and current rwptr in current block
			currentReadLength = BLOCK_SIZE - (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE);
			if(currentReadLength > bytesToRead){
				currentReadLength = bytesToRead;
			}
			memcpy(buf + bytesRead,temp + (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE),currentReadLength);
			if(temp != NULL){
				free(temp);
				temp = NULL;
			}
			//increment rwptr and update temp variables
			fileDescriptorTable[fileID].rwptr += currentReadLength;
			bytesToRead -= currentReadLength;
			bytesRead += currentReadLength;	
			dataPointerIndex++;	
		}
		//if we need to read from indirect pointer block
		else{
			//read indirect pointer block into stack
			temp = calloc(1,sizeof(indirectTable));
			count = read_blocks(inodeTable[inodeInd].indirectPointer,1,temp);
			if(count < 0){
				// printf("Error reading indirect pointer table\n");
				return -1;
			}
			memcpy(indirectTable,temp,sizeof(indirectTable));
			if(temp != NULL){
				free(temp);
				temp = NULL;
			}

			//read data block pointed to by indirect table index,
			temp = calloc(1,BLOCK_SIZE);
			count = read_blocks(indirectTable[dataPointerIndex-12],1,temp);
			if(count < 0){
				// printf("Error reading indirect pointer data\n");
				return -1;
			}
			currentReadLength = BLOCK_SIZE - (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE);
			if(currentReadLength > bytesToRead){
				currentReadLength = bytesToRead;
			}
			memcpy(buf + bytesRead,temp + (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE),currentReadLength);
			if(temp != NULL){
				free(temp);
				temp = NULL;
			}

			fileDescriptorTable[fileID].rwptr += currentReadLength;
			bytesToRead -= currentReadLength;
			bytesRead += currentReadLength;	
			dataPointerIndex++;	
		}
	}
	
	return bytesRead;

}

int sfs_fwrite(int fileID, const char *buf, int length) {
	int bytesWritten = 0;
	int bytesToWrite = length;
	int inodeInd = fileDescriptorTable[fileID].inodeIndex;
	int indirectTable[256];
	for(int i=0;i<256;i++){
		indirectTable[i] = 0;
	}
	if(inodeInd == -1){
		// printf("File with fileID %i is not open,can't write\n",fileID);
		return -1;
	}
	if(length < 1 || fileDescriptorTable[fileID].rwptr + length > (BLOCK_SIZE-13)*number_of_blocks){
		// printf("Invalid length specified\n");
		return -1;
	}	
	
	while(bytesToWrite > 0){
		int dataPointerIndex = fileDescriptorTable[fileID].rwptr/BLOCK_SIZE; 
		int currentWriteLength = 0;
		//if we only need to write to direct data pointer block currently
		if(dataPointerIndex < 12){
			//if a new data block is needed, set direct pointer and set bitmap
			if((dataPointerIndex+1) > calculateNumberOfBlocksNeeded(inodeTable[inodeInd].size)){
				// printf("F1");
				inodeTable[inodeInd].data_ptrs[dataPointerIndex] = get_index();
			}
			temp = calloc(1,BLOCK_SIZE);
			count = read_blocks(inodeTable[inodeInd].data_ptrs[dataPointerIndex],1,temp);
			if(count < 0){
				// printf("Error reading data\n");
				return -1;
			}
			//append available data from current block to buffer based on number of bytes read, and current rwptr in current block
			currentWriteLength = BLOCK_SIZE - (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE);
			//if current write length > bytesToWrite, truncate currentWriteLength to bytesToWrite
			if(currentWriteLength > bytesToWrite){
				currentWriteLength = bytesToWrite;
			}
			//copy changed section of block to temp buffer and then flush to disk
		
			memcpy(temp + (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE),buf + bytesWritten,currentWriteLength);
			count = write_blocks(inodeTable[inodeInd].data_ptrs[dataPointerIndex],1,temp);
			if(count < 0){
				// printf("Error writing data\n");
				return -1;
			}
			//increment rwptr and update variables 
			fileDescriptorTable[fileID].rwptr += currentWriteLength;
			//increase size of file in inode if rwptr is now greater than file size
			if(fileDescriptorTable[fileID].rwptr > inodeTable[inodeInd].size){
				inodeTable[inodeInd].size = fileDescriptorTable[fileID].rwptr;
			}
			bytesToWrite -= currentWriteLength;
			bytesWritten += currentWriteLength;	
			dataPointerIndex++;	
			free(temp);
		}
		//if we must write to indirect pointer currently
		else{
			if(dataPointerIndex > 267){
				// printf("All indirect pointer blocks taken\n");
				return bytesWritten;
			}
			//if indirect pointer not yet set, find free block for indirect pointer table and set in bitmap
			if(inodeTable[inodeInd].indirectPointer == -1){
				inodeTable[inodeInd].indirectPointer = get_index();

				//initialize new indirect pointer table block
				for(int i=0; i<256; i++){
					indirectTable[i] = -1;
				}
			}
			//else read from disk
			else{
				temp = calloc(1,BLOCK_SIZE);
				count = read_blocks(inodeTable[inodeInd].indirectPointer,1,temp);
				if(count < 0){
					// printf("Error reading indirect pointer data\n");
				}
				memcpy(indirectTable,temp,256);
				free(temp);
			}
			//if a new indirect data block is needed, set indirect table to block index and set bitmap
			if((dataPointerIndex+1) > calculateNumberOfBlocksNeeded(inodeTable[inodeInd].size)){
				indirectTable[dataPointerIndex-12] = get_index();
			}
			 temp = calloc(1,BLOCK_SIZE);
			count = read_blocks(indirectTable[dataPointerIndex-12],1,temp);
			if(count < 0){
				// printf("Error reading indirect pointer data\n");
				return -1;
			}
			//append available data from current block to buffer based on number of bytes read, and current rwptr in current block
			currentWriteLength = BLOCK_SIZE - (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE);
			//if current write length > bytesToWrite, truncate currentWriteLength to bytesToWrite
			if(currentWriteLength > bytesToWrite){
				currentWriteLength = bytesToWrite;
			}

			//copy changed section of block to temp buffer and then flush to disk
			memcpy(temp + (fileDescriptorTable[fileID].rwptr % BLOCK_SIZE),buf + bytesWritten,currentWriteLength);
			count = write_blocks(indirectTable[dataPointerIndex-12],1,temp);
			if(count < 0){
					// printf("Error writing indirect pointer data\n");
					return -1;
			}
			//increment rwptr and update variables 
			fileDescriptorTable[fileID].rwptr += currentWriteLength;
			//increase size of file in inode if rwptr is now greater than file size
			if(fileDescriptorTable[fileID].rwptr > inodeTable[inodeInd].size){
				inodeTable[inodeInd].size = fileDescriptorTable[fileID].rwptr;
			}
			bytesToWrite -= currentWriteLength;
			bytesWritten += currentWriteLength;	
			dataPointerIndex++;	
			if(temp != NULL){
				free(temp);
				temp = NULL;
			}
			//flush indirect pointer table to disk
			count = write_blocks(inodeTable[inodeInd].indirectPointer,1,indirectTable);
			if(count < 0){
				// printf("Error writing indirect table\n");
				return -1;
			}
		}

	}
	//flush memory data structures to disk including indirectTable if indirect pointers were used

	//inode table
	temp = calloc(1,calculateSizeNeeded(sizeof(inodeTable)));
	memcpy(temp,inodeTable,sizeof(inodeTable));
	count = write_blocks(inode_table_index,calculateNumberOfBlocksNeeded(sizeof(inodeTable)),temp);
	if(count < 0){
		// printf("Error writing inodeTable\n");
		return -1;
	}
	if(temp != NULL){
		free(temp);
		temp = NULL;
	}

	//bitmap
	temp = calloc(1,calculateSizeNeeded(sizeof(free_bit_map)));
	memcpy(temp,free_bit_map,sizeof(free_bit_map));
	count = write_blocks(bitmap_index,1,temp);
	if(count < 0){
		fprintf(stderr,"Error writing free bit map");
		return -1;
	}
	free(temp);

	return bytesWritten;
}


int sfs_fseek(int fileID, int loc) {
	if(loc < 0){
		// printf("Invalid location\n");
		return -1;
	}
	int inodeInd = fileDescriptorTable[fileID].inodeIndex;
	if(inodeInd == -1){
		// printf("File with fileID %i is not open, can't seek\n",fileID);
		return -1;
	}
	if(inodeTable[inodeInd].size < loc){
		// printf("Can't seek to byte location greater than file size\n");
		return -1;	
	}
	//return 0 on success
	fileDescriptorTable[fileID].rwptr = loc;
	return 0;

}
int sfs_remove(char *file) {
	// remove in-memory data structures then flush to disk
	int inodeNumber = findFileInode(file);
	if(inodeNumber == -1){
		// fprintf(stderr,"File %s, does not exist",file);
		return -1;
	}
	for(int i=0;i<100;i++){
		if(fileDescriptorTable[i].inodeIndex == inodeNumber){
			sfs_fclose(i);
			break;
		}
	}
	inodeTable[inodeNumber].size = -1;
	if(inodeTable[inodeNumber].indirectPointer != -1){
		rm_index(inodeTable[inodeNumber].indirectPointer);
	}
	inodeTable[inodeNumber].indirectPointer = -1;
	for(int i=0;i<12;i++){
		if(inodeTable[inodeNumber].indirectPointer != -1){
			rm_index(inodeTable[inodeNumber].data_ptrs[i]);
		}
		inodeTable[inodeNumber].data_ptrs[i] = -1;
	}
	for(int i=0; i<max_inode_number;i++){
		if(strcmp(rootDirectory[i].name,file) == 0){
			rootDirectory[i].num = -1;
			break;
		}
	}

	//flush to disk
	//inode table
	temp = calloc(1,calculateSizeNeeded(sizeof(inodeTable)));
	memcpy(temp,inodeTable,sizeof(inodeTable));
	count = write_blocks(inode_table_index,calculateNumberOfBlocksNeeded(sizeof(inodeTable)),temp);
	if(count < 0){
		// printf("Error writing inodeTable\n");
		return -1;
	}
	free(temp);

	//bitmap
	temp = calloc(1,calculateSizeNeeded(sizeof(free_bit_map)));
	memcpy(temp,free_bit_map,sizeof(free_bit_map));
	count = write_blocks(bitmap_index,1,temp);
	if(count < 0){
		// fprintf(stderr,"Error writing free bit map");
		return -1;
	}
	free(temp);

	//root directory
	temp = calloc(1,calculateSizeNeeded(sizeof(rootDirectory)));
	memcpy(temp,rootDirectory,sizeof(rootDirectory));
	count = write_blocks(root_directory_index,calculateNumberOfBlocksNeeded(sizeof(rootDirectory)),temp);
	if(count < 0){
		// printf("Error writing directory table\n");
		return -1;
	}
	free(temp);
	return 0;
}

