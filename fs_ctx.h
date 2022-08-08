/**
 * CSC369 Assignment 1 - File system runtime context header file.
 */

#pragma once

#include <stddef.h>

#include "options.h"

#include "a1fs.h"

/**
 * Mounted file system runtime state - "fs context".
 */
typedef struct fs_ctx {
    /** Pointer to the start of the image. */
    void *image;
    /** Image size in bytes. */
    size_t size;

    struct a1fs_superblock *sb;
    struct a1fs_ibitmap *ibitmap;
    struct a1fs_bbitmap *bbitmap;
    struct a1fs_inode *itable;
    struct a1fs_dentry *btable;
} fs_ctx;

/**
 * Initialize file system context.
 *
 * @param fs     pointer to the context to initialize.
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @return       true on success; false on failure (e.g. invalid superblock).
 */
bool fs_ctx_init(fs_ctx *fs, void *image, size_t size);

/**
 * Destroy file system context.
 *
 * Must cleanup all the resources created in fs_ctx_init().
 */
void fs_ctx_destroy(fs_ctx *fs);
