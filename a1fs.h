/**
 * CSC369 Assignment 1 - a1fs types, constants, and data structures header file.
 */

#pragma once

#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>


/**
 * a1fs block size in bytes. You are not allowed to change this value.
 *
 * The block size is the unit of space allocation. Each file (and directory)
 * must occupy an integral number of blocks. Each of the file systems metadata
 * partitions, e.g. superblock, inode/block bitmaps, inode table (but not an
 * individual inode) must also occupy an integral number of blocks.
 */
#define A1FS_BLOCK_SIZE 4096

/** Block number (block pointer) type. */
typedef uint32_t a1fs_blk_t;

/** Inode number type. */
typedef uint32_t a1fs_ino_t;


/** Magic value that can be used to identify an a1fs image. */
#define A1FS_MAGIC 0xC5C369A1C5C369A1ul

/** a1fs superblock. */
typedef struct a1fs_superblock {
    /** Must match A1FS_MAGIC. */
    uint64_t magic;
    /** File system size in bytes. */
    uint64_t size;

    //TODO: add necessary fields
    unsigned int inode_count; /** Number of inodes*/
    unsigned int block_count; /** Number of blocks*/
    unsigned int used_block_count;    /* Reserved blocks count */
    unsigned int used_inode_count;  /* Reserved inode count*/
    unsigned int inode_bitmap;      /* Inodes bitmap block */
    unsigned int block_bitmap;      /* Blocks bitmap block */
    unsigned int inode_table;       /* Start of inodes table block */
    unsigned int block_table;       /* Start of data table block */
} a1fs_superblock;

// Superblock must fit into a single block
static_assert(sizeof(a1fs_superblock) <= A1FS_BLOCK_SIZE,
              "superblock is too large");

/** Extent - a contiguous range of blocks. */
typedef struct a1fs_extent {
    /** Starting block of the extent. */
    a1fs_blk_t start;
    /** Number of blocks in the extent. */
    a1fs_blk_t count;

} a1fs_extent;

/** a1fs inode. */
typedef struct a1fs_inode {
    /** File mode. */
    mode_t mode;

    /**
     * Reference count (number of hard links).
     *
     * Each file is referenced by its parent directory. Each directory is
     * referenced by its parent directory, itself (via "."), and each
     * subdirectory (via ".."). The "parent directory" of the root directory is
     * the root directory itself.
     */
    uint32_t links;

    /** File size in bytes. */
    uint64_t size;

    /**
     * Last modification timestamp.
     *
     * Must be updated when the file (or directory) is created, written to, or
     * its size changes. Use the clock_gettime() function from time.h with the
     * CLOCK_REALTIME clock; see "man 3 clock_gettime" for details.
     */
    struct timespec mtime;


    //CUSTOM. Altogether, each inode is 256 bytes.
    struct a1fs_extent extent[12];  //direct extents
    unsigned int extent_count;  //EXTENT COUNT; used to check if below cap of 512 extents
    unsigned int indirect;  //block index of indirect block. 0 = no indirect block
    unsigned int block_count;   //how many data block allocated. Includes indirect block.
    unsigned int num;   //inode index. 0 for root.
    unsigned int parent_num;    //parent inode index
    unsigned int empty; //Basically an entry count for directories. 0 represents empty, >0 not empty
    char padding[100];
} a1fs_inode;


/* Our structs  */

// STRUCTURE for indirect extent block
typedef struct a1fs_indirect_ext {
    struct a1fs_extent extent[500]; //500 extents in this block
    char padding [96];  //full 4096 bytes
} a1fs_indirect_ext;

// BITMAP STRUCTURES.
// CHAR ARRAYS of 0s and 1s (4096 bytes)
typedef struct a1fs_ibitmap {
    char map[A1FS_BLOCK_SIZE];
} a1fs_ibitmap;

typedef struct a1fs_bbitmap {
    char map[A1FS_BLOCK_SIZE];
} a1fs_bbitmap;


// A single block must fit an integral number of inodes
static_assert(A1FS_BLOCK_SIZE % sizeof(a1fs_inode) == 0, "invalid inode size");


/** Maximum file name (path component) length. Includes the null terminator. */
#define A1FS_NAME_MAX 252

/** Maximum file path length. Includes the null terminator. */
#define A1FS_PATH_MAX PATH_MAX

/** Fixed size directory entry structure. */
typedef struct a1fs_dentry {
    /** Inode number. */
    a1fs_ino_t ino;
    /** File name. A null-terminated string. */
    char name[A1FS_NAME_MAX];

} a1fs_dentry;

static_assert(sizeof(a1fs_dentry) == 256, "invalid dentry size");
