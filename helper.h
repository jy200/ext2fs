// HELPER FUNCTION SAND STRUCTS

#include "a1fs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stddef.h>
#include "options.h"
#include "a1fs.c"

// STRUCTURE for indirect extent block
typedef struct a1fs_indirect_ext {
    struct a1fs_extent extent[500]; //500 extents in this block
    char padding [96];  //full 4096 bytes
} a1fs_indirect_ext;

// BITMAP STRUCTURES.
// CHAR ARRAYS of 0s and 1s (4096 bytes)
typedef struct a1fs_ibitmap {   //inode bitmap
    char map[A1FS_BLOCK_SIZE];
} a1fs_ibitmap;

typedef struct a1fs_bbitmap {   //block bitmap
    char map[A1FS_BLOCK_SIZE];
} a1fs_bbitmap;

/* Returns the inode number for the element at the end of the path
 * if it exists.
 * Possible errors include:
 *   - The path is not an absolute path: -1
 *   - An element on the path cannot be found: -1
 *   - component is not directory: -2
 */
int path_lookup(const char *path);

/* Returns an index of a free inode.
 * First available inode if it exists.
 * If there is any error, return -1.
 * possible error: no free inode
 */
int first_inode(const struct a1fs_ibitmap *ibitmap);

/* Returns an index of a free block. Either next to block
 * or the first available block if it exists.
 * If there is any error, return -1.
 * possible error: no free block
 */
int first_block(const struct a1fs_bbitmap *bbitmap, int block_num);