#ifndef _INCLUDE_SFS_API_H_
#define _INCLUDE_SFS_API_H_

#include <stdint.h>

#define MAX_FILE_NAME 21
#define MAX_EXTENSION_NAME 3
#define NUM_BLOCKS 1024  //maximum number of data blocks on the disk.
#define BITMAP_ROW_SIZE (NUM_BLOCKS/8) // this essentially mimcs the number of rows we have in the bitmap. we will have 128 rows. 
#define max_inode_number 100
#define BLOCK_SIZE 1024
#define number_of_blocks 1024
#define superblock_index 0
#define inode_table_index 1
// should be 3 blocks max (20 for name, 4 for inode number, *100 inodes)
#define root_directory_index 9
#define bitmap_index 12
#define data_block_index 13
#define SIZE 128

typedef struct superblock_t{
    uint64_t magic;
    uint64_t block_size;
    uint64_t fs_size;
    uint64_t inode_table_len;
    uint64_t root_dir_inode;
} superblock_t;

typedef struct inode_t {
    unsigned int mode;
    unsigned int link_cnt;
    unsigned int uid;
    unsigned int gid;
    unsigned int size;
    unsigned int data_ptrs[12];
    unsigned int indirectPointer; // points to a data block that points to other data blocks (Single indirect)
} inode_t;

/*
 * inodeIndex    which inode this entry describes
 * inode  pointer towards the inode in the inode table
 *rwptr    where in the file to start   
 */
typedef struct file_descriptor {
    uint64_t inodeIndex;
    inode_t* inode; // 
    uint64_t rwptr;
} file_descriptor;


typedef struct directory_entry{
    int num; // represents the inode number of the entry. 
    char name[MAX_FILE_NAME]; // represents the name of the entry. 
}directory_entry;

void mksfs(int fresh);
int sfs_getnextfilename(char *fname);
int sfs_getfilesize(const char* path);
int sfs_fopen(char *name);
int sfs_fclose(int fileID);
int sfs_fread(int fileID, char *buf, int length);
int sfs_fwrite(int fileID, const char *buf, int length);
int sfs_fseek(int fileID, int loc);
int sfs_remove(char *file);

#endif //_INCLUDE_SFS_API_H_
