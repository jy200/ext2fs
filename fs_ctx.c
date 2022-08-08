/**
 * CSC369 Assignment 1 - File system runtime context implementation.
 */

#include "fs_ctx.h"


bool fs_ctx_init(fs_ctx *fs, void *image, size_t size)
{
    fs->image = image;
    fs->size = size;

    // Runtime State
    fs->sb = (struct a1fs_superblock *)(image + A1FS_BLOCK_SIZE);
    if (fs->sb->magic != A1FS_MAGIC)
        return false;
    fs->ibitmap = (struct a1fs_ibitmap *)(image + A1FS_BLOCK_SIZE*2); //block 2
    fs->bbitmap = (struct a1fs_bbitmap *)(image + A1FS_BLOCK_SIZE*3);   //block 3
    fs->itable = (struct a1fs_inode *)(image + A1FS_BLOCK_SIZE*4);  //block 4
    fs->btable = (struct a1fs_dentry *)(image + A1FS_BLOCK_SIZE*fs->sb->block_table);
    return true;
}

void fs_ctx_destroy(fs_ctx *fs)
{
    (void)fs;
}
