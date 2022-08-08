/**
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


static fs_ctx *get_fs(void);

/* Returns the inode number for the element at the end of the path
 * if it exists.
 * Possible errors include:
 *   - The path is not an absolute path: -1
 *   - An element on the path cannot be found: -1
 *   - component is not directory: -2
 */
int path_lookup(const char *path) {
    fs_ctx *fs = get_fs();
    if(path[0] != '/') {
        fprintf(stderr, "Not an absolute path\n");
        return -1;
    }
    else if (!strcmp(path, "/")){
        return 0;  //root inode
    }

    char *path2 = strdup(path);
    char *p = strtok(path2, "/");
    int match = 0;

    struct a1fs_inode *block_inode = fs->itable;    //root inode
    struct a1fs_dentry *entry = fs->btable;     //root block
    while (p != NULL){
       // if (strlen(p) >= A1FS_NAME_MAX) return -3;
        //if (S_ISREG(block_inode->mode)) return -2;
        for (int e=0; e < 12; e++){ //check all 12 direct extent pointers
            int start = block_inode->extent[e].start, count = block_inode->extent[e].count;
            if (count == 0){
                continue;
            }
            for (int f = 0; f < count; f++){
                for (int i =0; i< 16; i++){ //check all 16 entries
                    entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                    if (!strcmp(p, entry->name)){
                        block_inode = (struct a1fs_inode *) (fs->image + A1FS_BLOCK_SIZE*4 + sizeof(struct a1fs_inode) * entry->ino);   //entry->ino = inode number
                        match = 1;
                        break;
                    }else{
                        match = 0;  //need match because we need to match several times (more than one iteration in the while loop)
                    }
                }
                if (match ==1)
                    break;
            }
            if (match == 1)
                break;
        }
        if (!match){    //if no match with direct pointers, check indirect
            if (block_inode->indirect != 0){   //check if there's an indirect block
                struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + block_inode->indirect));
                for (int e=0; e < 500; e++){    //check up to max 512 extents
                    int start = indirect->extent[e].start, count = indirect->extent[e].count;
                    if (count ==0)
                        continue;
                    for (int f = 0; f < count; f++){
                        for (int i =0; i< 16; i++){
                            entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                            if (!strcmp(p, entry->name)){
                                block_inode = (struct a1fs_inode *) (fs->image + A1FS_BLOCK_SIZE*4 + sizeof(struct a1fs_inode) * entry->ino);   //entry->ino = inode number
                                match = 1;
                                break;
                            }else{
                                match = 0;  //because we need to match several times (more than one while iteration)
                            }
                        }
                        if (match ==1)
                            break;
                    }
                    if (match == 1)
                        break;
                }
            }else{
                return -1;
            }
        }
        p = strtok (NULL, "/"); //move onto next section of path
    }

    if (!match){
        return -1;
    }else{
        return block_inode->num;
    }

}

/* Returns an index of a free inode.
 * First available inode if it exists.
 * If there is any error, return -1.
 * possible error: no free inode
 */
int first_inode(const struct a1fs_ibitmap *ibitmap){
    for (int i=1; i<A1FS_BLOCK_SIZE; i++){
        if (ibitmap->map[i]== 0){
            return i;
        }
    }
    return -1;
}

/* Returns an index of a free block. Either next to block
 * or the first available block if it exists.
 * If there is any error, return -1.
 * possible error: no free block
 */
int first_block(const struct a1fs_bbitmap *bbitmap, int block_num){
    if (bbitmap->map[block_num +1] == 0){    //return block next to it if possible, otherwise iterate to find nearest
        return (block_num + 1);
    }
    for (int i=1; i<A1FS_BLOCK_SIZE; i++){
        if (bbitmap->map[i]== 0){
            return i;
        }
    }
    return -1;
}

/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
    // Nothing to initialize if only printing help
    if (opts->help) return true;

    size_t size;
    void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
    if (!image) return false;

    return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
    fs_ctx *fs = (fs_ctx*)ctx;
    if (fs->image) {
        munmap(fs->image, fs->size);
        fs_ctx_destroy(fs);
    }
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
    return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
    (void)path;// unused
    fs_ctx *fs = get_fs();

    memset(st, 0, sizeof(*st));
    st->f_bsize   = A1FS_BLOCK_SIZE;
    st->f_frsize  = A1FS_BLOCK_SIZE;
    st->f_namemax = A1FS_NAME_MAX;

    a1fs_superblock *sb = (struct a1fs_superblock *)(fs->image + A1FS_BLOCK_SIZE);
    st->f_blocks = sb->block_count; /* size of fs in f_frsize units */
    st->f_bfree = sb->block_count - sb->used_block_count;  /* # free blocks */
    st->f_bavail = st->f_bfree;  /* # free blocks for unprivileged users */
    st->f_files = sb->inode_count;    /* # inodes */
    st->f_ffree = sb->inode_count - sb->used_inode_count;   /* # free inodes */
    st->f_favail = st->f_ffree;  /* # free inodes for unprivileged users */
    return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors).
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
    if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
    fs_ctx *fs = get_fs();

    memset(st, 0, sizeof(*st));

    int num = path_lookup(path);
    if (num == -1) {
        return -ENOENT;
    }else if(num == -2){
        return -ENOTDIR;
    } else if (num == -3){
        return ENAMETOOLONG;
    }
    struct a1fs_inode *in = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);    //pointer to inode

    if (S_ISREG(in->mode))
        st->st_mode = S_IFREG | in->mode;
    else
        st->st_mode = S_IFDIR | in->mode;

    st->st_nlink = in->links;
    st->st_size = in->size;
    st->st_blksize = A1FS_BLOCK_SIZE;
    st->st_blocks = in->block_count * 8;  // (512 fragments) number of blocks used in extents. MAY ALSO INCLUDE INDIRECT BLOCK
    st->st_mtim =  in->mtime;
    return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    (void)offset;// unused
    (void)fi;// unused
    fs_ctx *fs = get_fs();

    int num = path_lookup(path);
    struct a1fs_inode *in = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);    //pointer to inode
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");
        return -errno;
    }
    filler(buf, "." , NULL, 0);
    filler(buf, "..", NULL, 0);

    if (in->empty == 0){    //check if directory is empty
        return 0;
    }

    struct a1fs_dentry *entry;
    for (int e=0; e < 12; e++){ //check all 12 direct extent pointers
        int start = in->extent[e].start, count = in->extent[e].count;
        if(count == 0)
            continue;
        for (int f = 0; f < count; f++){
            for (int i =0; i< 16; i++){ //check all 16 entries
                entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                if (strcmp("\0", entry->name)){
                   filler(buf, entry->name, NULL, 0);
                }
            }
        }
    }
    if (in->indirect != 0){   //check if there's an indirect block
        struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + in->indirect));
        for (int e=0; e < 500; e++){    //check up to max 512 extents
            int start = indirect->extent[e].start, count = indirect->extent[e].count;
            if (count == 0)
                continue;
            for (int f = 0; f < count; f++){
                for (int i =0; i< 16; i++){
                    entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                    if (strcmp("\0", entry->name)){
                       filler(buf, entry->name, NULL, 0);
                    }
                }
            }
        }
    }

    return 0;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
    mode = mode | S_IFDIR;
    fs_ctx *fs = get_fs();

    struct a1fs_superblock *sb = fs->sb;
    struct a1fs_ibitmap *ibitmap = fs->ibitmap;
    struct a1fs_bbitmap *bbitmap = fs->bbitmap;

    //check if we have enough blocks/inodes for a new directory
    if(sb->inode_count == sb->used_inode_count || sb->block_count == sb->used_block_count) {
        return -ENOSPC;
    }
    //PARSE PATH: entry_end will be dir name
    char *parent_path = strdup(path);
    char *parent_path_end = strrchr(parent_path, '/');
    char *entry_end = strdup(parent_path_end);
    *parent_path_end = '\0';

    if(entry_end[0] == '/') {
        entry_end++;
    }
    int num;
    if (!strcmp(parent_path, "")){  //our string parser removes root so we need a case for that
        num = path_lookup("/");
    }else{
        num = path_lookup(parent_path);
    }
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");
        return -errno;
    }
    //pointer to parent inode
    struct a1fs_inode *parent_inode = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);
    if (parent_inode->extent_count >= 512){
        fprintf(stderr, "MAX 512 EXTENTS\r\n");
        return -ENOSPC;
    }
    int inode = -1; //INODE INDEX OF NEW DIR
    int block = -1; //BLOCK INDEX OF NEW DIR

    //if empty directory, allocate to first entry, else allocate to first free entry
    struct a1fs_dentry *entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + parent_inode->extent[0].start));
    if (parent_inode->empty != 0){    //check if directory is empty
        int match = 0;
        //check all 12 direct extent pointers for empty entry to allocate
        for (int e=0; e < 12; e++){
            int start = parent_inode->extent[e].start, count = parent_inode->extent[e].count;
            if (count == 0){    // NEW EXTENT
                //we now need 2 data blocks. Check if enough space
                if(sb->block_count <= (sb->used_block_count + 1)) {
                    return -ENOSPC;
                }
                int b = first_block(bbitmap, start);
                bbitmap->map[b] = 1;    //new extent for parent_directory
                entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + b)); //first entry of new extent
                parent_inode->extent_count += 1;    //for allocating new extent block
                parent_inode->extent[e].start = b;
                parent_inode->extent[e].count = 1;
                parent_inode->block_count +=1;
                parent_inode->extent_count +=1;
                sb->used_block_count +=1;
                match = 1;
                break;
            }

            // EXISTING EXTENT. FIND FREE ENTRY
            for (int f = 0; f < count; f++){
                for (int i =0; i< 16; i++){
                    entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                    if (!strcmp("\0", entry->name)){
                        match = 1;
                        break;
                    }
                }
                if (match ==1)
                    break;
            }
            if (match == 1)
                break;
        }
        if (match == 0){
            //parent inode's direct extents are full. Time to check for indirect
            if (parent_inode->indirect == 0){   //NEW INDIRECT BLOCK
                //we need 3 data blocks: indirect, direct, new directory
                if(sb->block_count <= (sb->used_block_count + 3)) {
                    return -ENOSPC;
                }
                int b = first_block(bbitmap, 0); //new indirect block for parent_directory
                bbitmap->map[b] = 1;
                int c = first_block(bbitmap, b); //new block for an extent in the indirect block
                bbitmap->map[c] = 1;
                struct a1fs_indirect_ext *indirect= (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + b));
                memset(indirect, 0, A1FS_BLOCK_SIZE);
                indirect->extent[0].start = c;
                indirect->extent[0].count = 1;
                parent_inode->indirect = b;
                entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + c)); //first entry of new extent
                memset(entry, 0, sizeof(a1fs_dentry));
                parent_inode->block_count +=2;
                parent_inode->extent_count +=1;     //adds 1 extent block (doesnt count indirect block as one)
                sb->used_block_count +=2;
                match = 1;
            }else{  //EXISTING INDIRECT BLOCK
                struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + parent_inode->indirect));
                for (int e=0; e < 500; e++){
                    int start = indirect->extent[e].start, count = indirect->extent[e].count;
                    //NEW EXTENT
                    if (count == 0){
                        //need 2 data blocks. direct and new directory
                         if(sb->block_count <= (sb->used_block_count + 2)) {
                            return -ENOSPC;
                        }
                        int b = first_block(bbitmap, start);
                        bbitmap->map[b] = 1;    //new extent block for parent_directory
                        entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + b)); //first entry of new extent
                        memset(entry, 0, sizeof(a1fs_dentry));
                        indirect->extent[e].start = b;
                        indirect->extent[e].count = 1;
                        parent_inode->block_count +=1;
                        parent_inode->extent_count +=1; //adds 1 extent block
                        sb->used_block_count +=1;
                        match = 1;
                        break;
                    }
                    //EXISTING EXTENT
                    for (int f = 0; f < count; f++){    //Check all blocks in extent
                        for (int i =0; i< 16; i++){ //check all 16 entries
                            entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                            if (entry->ino == 0){
                                match = 1;
                                break;
                            }
                        }
                        if (match ==1)
                            break;
                    }
                    if (match == 1)
                        break;
                }
            }
            if (match == 0){
                fprintf(stderr, "ERR: no entry match even though there should be\r\n");
                return -errno;
            }
        }
    }

    //update bitmaps
    inode = first_inode(ibitmap);
    ibitmap->map[inode] = 1; //allocate inode in bitmap
    block = first_block(bbitmap, num);
    bbitmap->map[block] = 1;    //allocate block in bitmap
    //update parent
    parent_inode->links += 1;
    parent_inode->empty += 1;    //parent directory no longer empty
    clock_gettime(CLOCK_REALTIME, &parent_inode->mtime);
    //update superblock
    sb->used_inode_count += 1;  //from new directory
    sb->used_block_count += 1;  //from new directory
    //new dir's data + inode
    struct a1fs_inode *new = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * inode);
    memset(new, 0, sizeof(a1fs_inode));
    new->mode = mode;
    new->links = 2;
    new->size = 0;
    clock_gettime(CLOCK_REALTIME, &new->mtime);
    new->block_count = 1;
    new->num = inode;
    new->parent_num = num;
    new->extent[0].count=1; //since it's a new inode, first extent, first block will be allocated
    new->extent[0].start = block;
    new->extent_count += 1;
    //Put in entry
    entry->ino = inode;
    strcpy(entry->name, entry_end); //set entry values
    entry->name[strlen(entry->name)] = '\0';
    return(0);
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
    fs_ctx *fs = get_fs();

    int num = path_lookup(path);
    struct a1fs_inode *in = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");   //just in case
        return -errno;
    }
    if (in->empty > 0)
        return -ENOTEMPTY;  // Stop if directory is not empty

    struct a1fs_inode *parent_inode = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * in->parent_num);

    //search and remove from parent directory's entries
    struct a1fs_dentry *entry;
    for (int e=0; e < 12; e++){ //check all 12 direct extent pointers
        int start = parent_inode->extent[e].start, count = parent_inode->extent[e].count;
        if(count == 0)
            continue;
        for (int f = 0; f< count; f++){
            for (int i =0; i< 16; i++){
                entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                if (entry->ino == in->num){
                    memset(entry, 0, sizeof(struct a1fs_dentry));   //will wipe out the entry
                    break;
                }
            }
        }
    }
    //check indirect block if parent has one
    if (parent_inode->indirect != 0){
        struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + parent_inode->indirect));
        for (int e=0; e < 500; e++){    //check up to max 512 extents
            int start = indirect->extent[e].start, count = indirect->extent[e].count;
            if (count == 0)
                continue;
            //Check all blocks in extent
            for (int f = 0; f < count; f++){
                for (int i =0; i< 16; i++){ //check all 16 entries
                    entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                    if (entry->ino == in->num){
                        memset(entry, 0, sizeof(struct a1fs_dentry));   //will wipe out the entry
                        break;
                    }
                }
            }
        }
    }
    //update superblock, bitmap, and parent inode
    parent_inode->empty -= 1;
    parent_inode->links -= 1;
    parent_inode->size -= in->size;
    clock_gettime(CLOCK_REALTIME, &parent_inode->mtime);
    fs->sb->used_inode_count -= 1;  //need to update superblock count for used inodes
    fs->ibitmap->map[in->num] = 0;

    //Update block bitmap to remove directory's data
    for (int e=0; e < 12; e++){
        int start = in->extent[e].start, count = in->extent[e].count;
        if(count == 0)  //check if empty extent
            continue;
        for (int f = 0; f < count; f++){    //else unallocate all blocks in removed directory's extents
            fs->bbitmap->map[start + f] = 0;
            fs->sb->used_block_count -=1;
        }
    }
    if (in->indirect != 0){   //check if there's an indirect block in the removed directory
        struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + in->indirect));
        for (int e=0; e < 500; e++){
            int start = indirect->extent[e].start, count = indirect->extent[e].count;
            if (count == 0)
                continue;
            for (int f = 0; f < count; f++){
                fs->bbitmap->map[start + f] = 0;
                fs->sb->used_block_count -=1;
            }
        }
        fs->bbitmap->map[in->indirect] = 0; //unallocate indirect block in bitmap
        fs->sb->used_block_count -=1;
    }
    memset(in, 0, sizeof(struct a1fs_inode));
    return 0;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    (void)fi;// unused
    assert(S_ISREG(mode));
    fs_ctx *fs = get_fs();

    struct a1fs_superblock *sb = fs->sb;
    struct a1fs_ibitmap *ibitmap = fs->ibitmap;
    struct a1fs_bbitmap *bbitmap = fs->bbitmap;

    if(sb->inode_count == sb->used_inode_count) {   //We need to allocate one inode for new file
        return -ENOSPC;
    }
    //path parsing. entry_end is file name
    char *parent_path = strdup(path);
    char *parent_path_end = strrchr(parent_path, '/');
    char *entry_end = strdup(parent_path_end);
    *parent_path_end = '\0';

    if(entry_end[0] == '/') {
        entry_end++;
    }
    int num;
    if (!strcmp(parent_path, "")){  //our string parser removes root so we need a case for that
        num = path_lookup("/");
    }else{
        num = path_lookup(parent_path);
    }
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");   //just in case
        return -errno;
    }
    struct a1fs_inode *parent_inode = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);
    if (parent_inode->extent_count >= 512){
        fprintf(stderr, "MAX 512 EXTENTS\r\n");
        return -ENOSPC;
    }
    struct a1fs_dentry *entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + parent_inode->extent[0].start));
    //check if directory is empty; if it is allocate to first entry, first block
    if (parent_inode->empty == 0){
        entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + parent_inode->extent[0].start));
    }else{
        int match = 0;
        //check all 12 direct extent pointers for empty empty to allocate
        for (int e=0; e < 12; e++){
            int start = parent_inode->extent[e].start, count = parent_inode->extent[e].count;
            //If extent has 16 entries (max), allocate new extent
            if (count == 0){
                // We need to allocate 1 block for parent directory's new extent
                if(sb->block_count == sb->used_block_count) {
                    return -ENOSPC;
                }
                int b = first_block(bbitmap, start);
                bbitmap->map[b] = 1;    //new extent for parent_directory
                entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + b)); //first entry of new extent
                memset(entry, 0, sizeof(a1fs_dentry));
                parent_inode->extent[e].start = b;
                parent_inode->extent[e].count = 1;
                parent_inode->block_count +=1;
                parent_inode->extent_count +=1;
                sb->used_block_count +=1;
                match = 1;
                break;
            }

            // EXISTING EXTENT. FIND FREE ENTRY
            for (int f = 0; f < count; f++){
                for (int i =0; i< 16; i++){
                    entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                    if (!strcmp("\0", entry->name)){
                        match = 1;
                        break;
                    }
                }
                if (match ==1)
                    break;
            }
            if (match == 1)
                break;
        }
        if (match == 0){
            //parent inode's direct extents are full. Time to check for indirect
            if (parent_inode->indirect == 0){
                 //NEW INDIRECT BLOCK, NEW EXTENT BLOCK
                if(sb->block_count <= (sb->used_block_count + 2)) {
                    return -ENOSPC;
                }
                int b = first_block(bbitmap, 0);
                bbitmap->map[b] = 1;    //new indirect block for parent_directory
                int c = first_block(bbitmap, b);
                bbitmap->map[c] = 1;    //new block for an extent in the indirect block
                struct a1fs_indirect_ext *indirect= (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + b));
                indirect->extent[0].start = c;
                indirect->extent[0].count = 1;
                parent_inode->indirect = b;
                entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + c)); //first entry of new extent
                memset(entry, 0, sizeof(a1fs_dentry));
                parent_inode->block_count +=2;
                parent_inode->extent_count +=1;
                sb->used_block_count +=2;
                match = 1;
            }else{
                //EXISTING INDIRECT BLOCK
                struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + parent_inode->indirect));
                for (int e=0; e < 500; e++){
                    int start = indirect->extent[e].start, count = indirect->extent[e].count;
                    if (count == 0){    //NEW EXTENT
                        if(sb->block_count == sb->used_block_count) {  //1 extent block
                            return -ENOSPC;
                        }
                        int b = first_block(bbitmap, start);
                        bbitmap->map[b] = 1;    //new extent block for parent_directory
                        //First entry of new extent block
                        entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + b));
                        memset(entry, 0, sizeof(a1fs_dentry));
                        indirect->extent[e].start = b;
                        indirect->extent[e].count = 1;
                        parent_inode->block_count +=1;
                        parent_inode->extent_count +=1;
                        sb->used_block_count +=1;
                        match = 1;
                        break;
                    }
                    //EXISTING EXTENT
                    for (int f = 0; f < count; f++){    //Check all blocks in extent
                        for (int i =0; i< 16; i++){ //check all 16 entries
                            entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                            if (entry->ino == 0){
                                match = 1;
                                break;
                            }
                        }
                        if (match ==1)
                            break;
                    }
                    if (match == 1)
                        break;
                }
            }
            if (match == 0){
                fprintf(stderr, "ERR: no entry match even though there should be\r\n");
                return -errno;
            }
        }
    }

    parent_inode->empty += 1; //Update parent entry count
    clock_gettime(CLOCK_REALTIME, &parent_inode->mtime);
    int inode = first_inode(ibitmap);
    ibitmap->map[inode] = 1; //allocate inode in bitmap

    //update superblock
    sb->used_inode_count += 1;

    //Allocate first free inode
    struct a1fs_inode *new = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * inode);
    memset(new, 0, sizeof(a1fs_inode));
    new->mode = mode;
    new->links = 1; //1 link for new file
    new->size = 0;
    clock_gettime(CLOCK_REALTIME, &new->mtime);
    new->block_count = 0;
    new->num = inode;
    new->parent_num = num; //assign parent inode's number
    //Put in entry
    entry->ino = inode;
    strcpy(entry->name, entry_end); //set entry values
    entry->name[strlen(entry->name)] = '\0';

    return(0);
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
    fs_ctx *fs = get_fs();

    int num = path_lookup(path);
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");   //just in case
        return -errno;
    }
    struct a1fs_inode *in = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);
    struct a1fs_inode *parent_inode = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * in->parent_num);

    //search and remove from parent directory's entries
    struct a1fs_dentry *entry;
    //check all 12 direct extent pointers
    for (int e=0; e < 12; e++){
        int start = parent_inode->extent[e].start, count = parent_inode->extent[e].count;
        if(count == 0)
            continue;
        for (int f = 0; f< count; f++){
            for (int i =0; i< 16; i++){
                entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                if (entry->ino == in->num){
                    memset(entry, 0, sizeof(struct a1fs_dentry));   //will wipe out the entry
                    break;
                }
            }
        }
    }
    //Check if parent has indirect block
    if (parent_inode->indirect != 0){
        struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + parent_inode->indirect));
        for (int e=0; e < 500; e++){    //check up to max 512 extents
            int start = indirect->extent[e].start, count = indirect->extent[e].count;
            if (count == 0)
                continue;
            //Check all blocks in extent
            for (int f = 0; f < count; f++){
                for (int i =0; i< 16; i++){ //check all 16 entries
                    entry = (struct a1fs_dentry *) (fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table + start + f) + sizeof(a1fs_dentry) *i);
                    if (entry->ino == in->num){
                        memset(entry, 0, sizeof(struct a1fs_dentry));   //will wipe out the entry
                        break;
                    }
                }
            }
        }
    }
    clock_gettime(CLOCK_REALTIME, &parent_inode->mtime);
    parent_inode->empty -= 1;   //decrease entry count by 1
    parent_inode->size -= in->size;
    fs->sb->used_inode_count -= 1;  //need to update superblock count for used inodes
    fs->ibitmap->map[in->num] = 0;  //deallocate removed file

    //Update block bitmap if file has data blocks
    if (in->size > 0 || in->block_count > 0){
        for (int e=0; e < 12; e++){
            int start = in->extent[e].start, count = in->extent[e].count;
            if(count == 0)  //check if empty extent
                continue;
            for (int f = 0; f < count; f++){    //else unallocate all blocks in removed directory's extents
                fs->bbitmap->map[start + f] = 0;
                fs->sb->used_block_count -=1;
            }
        }
        //check if there's an indirect block in the removed directory
        if (in->indirect != 0){
            struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext *) (fs->image + A1FS_BLOCK_SIZE * (fs->sb->block_table + in->indirect));
            for (int e=0; e < 500; e++){
                int start = indirect->extent[e].start, count = indirect->extent[e].count;
                if (count == 0)
                    continue;
                for (int f = 0; f < count; f++){
                    fs->bbitmap->map[start + f] = 0;
                    fs->sb->used_block_count -=1;
                }
            }
            fs->bbitmap->map[in->indirect] = 0; //unallocate indirect block
            fs->sb->used_block_count -=1;
        }
    }
    memset(in, 0, sizeof(a1fs_inode));
    return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
    fs_ctx *fs = get_fs();

    // update the modification timestamp (mtime) in the inode for given
    // path with either the time passed as argument or the current time,
    // according to the utimensat man page

    int num = path_lookup(path);
    struct a1fs_inode *in = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");   //just in case
        return -errno;
    }
    if (times == NULL || (times[0].tv_nsec == UTIME_NOW && times[1].tv_nsec == UTIME_NOW)){ //update to current time
        clock_gettime(CLOCK_REALTIME, &in->mtime);
    }else if (times[0].tv_nsec == UTIME_OMIT && times[1].tv_nsec == UTIME_OMIT){    //do nothing
        return 0;
    }else{
        in->mtime = times[1];
    }
    return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
    fs_ctx *fs = get_fs();

    //TODO: set new file size, possibly "zeroing out" the uninitialized range
    struct a1fs_superblock *sb = fs->sb;
   // struct a1fs_ibitmap *ibitmap = fs->ibitmap;
    struct a1fs_bbitmap *bbitmap = fs->bbitmap;
    int num = path_lookup(path);
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");
        return -errno;
    }
    //find inode of file
    struct a1fs_inode *file = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);

    //nothing to do if file is the size
    if(file->size == (uint64_t) size) {
        return 0;
    } else if(size > (sb->block_count - sb->used_block_count)*A1FS_BLOCK_SIZE) { //not enough free blocks
        return -ENOSPC;
    } else if(file->size < (uint64_t) size) { //extend file
        int bytes_to_add = size - file->size;
        while(bytes_to_add > 0) {
            for (int i = 0; i < 13 && bytes_to_add > 0; i++) { //go through extents
                if (file->extent[i].start == 0) { //find empty extent
                    //allocate block
                    for(int j = 1; j < A1FS_BLOCK_SIZE; j++) { //find empty data block
                        if(bbitmap->map[j] == 0) {
                            bbitmap->map[j] = 1;
                            int extent_size = 1;
                            for(int k = j+1; k < A1FS_BLOCK_SIZE && bytes_to_add > (A1FS_BLOCK_SIZE*extent_size); k++) { //find size of extent
                                if (bbitmap->map[k] == 0) {
                                    extent_size++;
                                    bbitmap->map[k] = 1;
                                    //bytes_to_add = bytes_to_add - A1FS_BLOCK_SIZE;
                                }
                            }
                            if(bytes_to_add < A1FS_BLOCK_SIZE) {
                                memset(fs->image+(sb->block_table+j)*A1FS_BLOCK_SIZE, 0, bytes_to_add); //0 data
                                bytes_to_add = 0;
                            } else if(bytes_to_add < A1FS_BLOCK_SIZE*extent_size){
                                memset(fs->image+(sb->block_table+j)*A1FS_BLOCK_SIZE, 0, A1FS_BLOCK_SIZE*(extent_size-1) +bytes_to_add); //0 data
                                bytes_to_add = bytes_to_add - (A1FS_BLOCK_SIZE*(extent_size-1) + bytes_to_add);
                            } else {
                                memset(fs->image+(sb->block_table+j)*A1FS_BLOCK_SIZE, 0, A1FS_BLOCK_SIZE*(extent_size)); //0 data
                                bytes_to_add -= (A1FS_BLOCK_SIZE*extent_size);
                            }
                            file->extent[i].start = j;
                            file->extent[i].count = extent_size;
                            sb->used_block_count += extent_size;
                            file->extent_count += extent_size;
                            file->block_count += extent_size;
                            break; //break for loop after extent allocated
                        }
                    }
                }
            }
            if(bytes_to_add > 0) {
                if(file->indirect == 0 ) { //new indirect block
                    int b = first_block(bbitmap, 0);
                    bbitmap->map[b] = 1;
                    file->indirect = b;
                    file->block_count++;
                }
                struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*) (fs->image +
                                                                                 A1FS_BLOCK_SIZE * ((sb->block_table) +
                                                                                 file->indirect));
                for (int i = 0; i < 500 && bytes_to_add > 0; i++) {
                    if (indirect->extent[i].start == 0) { //find empty extent
                        //allocate block
                        for (int j = 1; j < A1FS_BLOCK_SIZE; j++) { //find empty data block
                            if (bbitmap->map[j] == 0) {
                                bbitmap->map[j] = 1;
                                int extent_size = 1;
                                for (int k = j + 1; k < A1FS_BLOCK_SIZE && bytes_to_add > (A1FS_BLOCK_SIZE *
                                                                                           extent_size); k++) { //find size of extent
                                    if (bbitmap->map[k] == 0) {
                                        extent_size++;
                                        bbitmap->map[k] = 1;
                                        //bytes_to_add = bytes_to_add - A1FS_BLOCK_SIZE;
                                    }
                                }
                                if (bytes_to_add < A1FS_BLOCK_SIZE) {
                                    memset(fs->image + (sb->block_table + j) * A1FS_BLOCK_SIZE, 0, bytes_to_add); //0 data
                                    bytes_to_add = 0;
                                } else if (bytes_to_add < A1FS_BLOCK_SIZE * extent_size) {
                                    memset(fs->image + (sb->block_table + j) * A1FS_BLOCK_SIZE, 0,
                                           A1FS_BLOCK_SIZE * (extent_size - 1) + bytes_to_add); //0 data
                                    bytes_to_add = bytes_to_add - (A1FS_BLOCK_SIZE * (extent_size - 1) + bytes_to_add);
                                } else {
                                    memset(fs->image + (sb->block_table + j) * A1FS_BLOCK_SIZE, 0,
                                           A1FS_BLOCK_SIZE * (extent_size)); //0 data
                                    bytes_to_add -= (A1FS_BLOCK_SIZE * extent_size);
                                }
                                indirect->extent[i].start = j;
                                indirect->extent[i].count = extent_size;
                                sb->used_block_count += extent_size;
                                file->block_count += extent_size;
                                file->extent_count += extent_size;
                                break; //break for loop after extent allocated
                            }
                        }
                    }
                }
            }
        }
        clock_gettime(CLOCK_REALTIME, &(file->mtime)); //update modified time
        file->size = size; //update file size
        return 0;
    } else if(file->size > (uint64_t) size) { //shrink file
        int bytes_to_remove = file->size - size;
        while(bytes_to_remove >= A1FS_BLOCK_SIZE) { //can remove whole block
            int last_extent = 0;
            if(file->indirect != 0) {
                struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
                for(int i = 0; i < 500; i++) {
                    if(indirect->extent[i].start != 0) {
                        last_extent = i;
                    }
                }
                for(int j = indirect->extent[last_extent].count; j >= 0 && bytes_to_remove >= A1FS_BLOCK_SIZE; j--) {
                    //remove block
                    sb->used_block_count--;
                    bbitmap->map[indirect->extent[last_extent].start + j] = 0;
                    indirect->extent[last_extent].count--;
                    file->extent_count--;
                    file->block_count--;
                    if(j == 0) { //extent needs to be cleared if empty
                        indirect->extent[last_extent].start = 0;
                        if(last_extent == 0) { //indirect block needs to be removed
                            bbitmap->map[file->indirect] = 0;
                            file->indirect = 0;
                            file->block_count--;
                        }
                    }
                    bytes_to_remove = bytes_to_remove - A1FS_BLOCK_SIZE;
                }
            } else {
                for (int i = 0; i < 13; i++) { //find last extent with data
                    if (file->extent[i].start != 0) {
                        last_extent = i;
                    }
                }
                //go through from end removing blocks
                for (int j = file->extent[last_extent].count; j >= 0 && bytes_to_remove >= A1FS_BLOCK_SIZE; j--) {
                    //remove block
                    sb->used_block_count--;
                    file->block_count--;
                    bbitmap->map[file->extent[last_extent].start + j] = 0;
                    file->extent[last_extent].count--;
                    if (j == 0) { //extent needs to be cleared if empty
                        file->extent[last_extent].start = 0;
                    }
                    bytes_to_remove = bytes_to_remove - A1FS_BLOCK_SIZE;
                }
            }
        }
        if(bytes_to_remove > 0) { //if partial block data still has to be removed
            if(file->indirect != 0) {
                struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
                int last_extent = 0;
                for (int i = 0; i < 13; i++) {
                    if(indirect->extent[i].start != 0) {
                        last_extent = i;
                    }
                }
                memset(fs->image + (sb->block_table + indirect->extent[last_extent].start + indirect->extent[last_extent].count)*A1FS_BLOCK_SIZE-bytes_to_remove, 0,
                       bytes_to_remove);
            } else {
                int last_extent = 0;
                for (int i = 0; i < 13; i++) {
                    if(file->extent[i].start != 0) {
                        last_extent = i;
                    }
                }
                memset(fs->image + (sb->block_table + file->extent[last_extent].start + file->extent[last_extent].count)*A1FS_BLOCK_SIZE-bytes_to_remove, 0,
                       bytes_to_remove);
            }

        }
        clock_gettime(CLOCK_REALTIME, &(file->mtime));
        file->size = size;
        return 0;
    }

    return -ENOSYS;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    (void)fi;// unused
    fs_ctx *fs = get_fs();

    //TODO: read data from the file at given offset into the buffer
    // (void)path;
    // (void)buf;
    // (void)size;
    // (void)offset;
    // (void)fs;
    struct a1fs_superblock *sb = fs->sb;
    int num = path_lookup(path);
    if (num < 0){
        fprintf(stderr, "PATHLOOKUP FAILED\r\n");
        return -errno;
    }
    struct a1fs_inode *file = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);
    unsigned int difference = file->size - offset;
    if (difference <= 0){
        return 0;
    }
    int bytes_left_to_read = size;
    int bytes_read_into_buf = 0;
    int bytes_read = 0, offset_extent = 0, offset_extent_count = 0, offset_extent_set = 0, in_indirect = 0, offset_found = 0;
    while(bytes_read < offset) { //first find offset
        for(int i = 0;i < 13 && bytes_read < offset; i++) {
            if(file->extent[i].start != 0) {
                for(int j = 0; j < (int) file->extent[i].count; j++) {
                    if(bytes_read/A1FS_BLOCK_SIZE < offset/A1FS_BLOCK_SIZE) { //skip blocks
                        bytes_read += A1FS_BLOCK_SIZE;
                    } else if(bytes_read/A1FS_BLOCK_SIZE != offset/A1FS_BLOCK_SIZE){ // find exact byte
                        offset_extent = i;
                        offset_extent_set = 1;
                        offset_extent_count = j;
                        for (int k = 0; k < A1FS_BLOCK_SIZE; k++) {
                            bytes_read++;
                            if(bytes_read == offset) { //found byte
                                if(bytes_left_to_read < (A1FS_BLOCK_SIZE-k)) { //read partial block
                                    // memcpy(fs->image + (sb->block_table + file->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                    //        k, buf, bytes_left_to_read);
                                    memcpy(buf, fs->image + (sb->block_table + file->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                           k, bytes_left_to_read);
                                    return size;
                                } else { //read full block and exit looking for offset
                                    // memcpy(fs->image + (sb->block_table + file->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                    //        k, buf, (A1FS_BLOCK_SIZE - bytes_left_to_read));
                                    memcpy(buf, fs->image + (sb->block_table + file->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                           k, (A1FS_BLOCK_SIZE - bytes_left_to_read));
                                    bytes_left_to_read -= (A1FS_BLOCK_SIZE-bytes_left_to_read);
                                    bytes_read_into_buf += (A1FS_BLOCK_SIZE-bytes_left_to_read);
                                    offset_found = 1;
                                }
                            }
                            if(offset_found) {
                                break;
                            }
                        }
                    }
                    if(offset_found) {
                        break;
                    }
                }
            }
            if(offset_found) {
                break;
            }
        }
        //same thing as above but if indirect extent is used
        for(int i = 0;i < 500 && bytes_read < offset; i++) {
            in_indirect = 1;
            struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
            if(indirect->extent[i].start != 0) {
                for(int j = 0; j < (int) indirect->extent[i].count; j++) {
                    if(bytes_read/A1FS_BLOCK_SIZE < offset/A1FS_BLOCK_SIZE) { //skip blocks
                        bytes_read += A1FS_BLOCK_SIZE;
                    } else if(bytes_read/A1FS_BLOCK_SIZE != offset/A1FS_BLOCK_SIZE){ // find exact byte
                        offset_extent = i;
                        offset_extent_set = 1;
                        offset_extent_count = j;
                        for (int k = 0; k < A1FS_BLOCK_SIZE; k++) {
                            bytes_read++;
                            if(bytes_read == offset) { //found byte
                                if(bytes_left_to_read < (A1FS_BLOCK_SIZE-k)) { //read partial block
                                    // memcpy(fs->image + (sb->block_table + indirect->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                    //        k, buf, bytes_left_to_read);
                                    memcpy(buf, fs->image + (sb->block_table + indirect->extent[i].start + indirect->extent[i].count) * A1FS_BLOCK_SIZE +
                                                k, bytes_left_to_read);
                                    return size;
                                } else { //read full block and exit looking for offset
                                    // memcpy(fs->image + (sb->block_table + indirect->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                    //        k, buf, (A1FS_BLOCK_SIZE - bytes_left_to_read));
                                    memcpy(buf, fs->image + (sb->block_table + indirect->extent[i].start + indirect->extent[i].count) * A1FS_BLOCK_SIZE +
                                                k, (A1FS_BLOCK_SIZE - bytes_left_to_read));
                                    bytes_left_to_read -= (A1FS_BLOCK_SIZE-bytes_left_to_read);
                                    bytes_read_into_buf += (A1FS_BLOCK_SIZE-bytes_left_to_read);
                                    offset_found = 1;
                                }
                            }
                            if(offset_found) {
                                break;
                            }
                        }
                    }
                    if(offset_found) {
                        break;
                    }
                }
            }
            if(offset_found) {
                break;
            }
        }
    }
    //now offset bytes have been read, time to read what we want.
    if(!in_indirect) {
        for(int i=offset_extent; i< 13; i++) {
            if (file->extent[i].start != 0) {
                for (int j = 0; j < (int) file->extent[i].count; j++) {
                    if (i == offset_extent && offset_extent_set == 1) { //jump to where offset is
                        j = offset_extent_count + 1;
                    }
                    if (bytes_left_to_read < A1FS_BLOCK_SIZE) { //partial block read
                        // memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                        //        buf + bytes_read_into_buf, bytes_left_to_read);
                        memcpy(buf + bytes_read_into_buf,
                               fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               bytes_left_to_read);
                        bytes_read_into_buf += bytes_left_to_read;
                        return bytes_read_into_buf;
                    } else { //full block read
                        // memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                        //        buf + bytes_read_into_buf, A1FS_BLOCK_SIZE);
                        memcpy(buf + bytes_read_into_buf,
                               fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               A1FS_BLOCK_SIZE);
                        bytes_read_into_buf += A1FS_BLOCK_SIZE;
                        bytes_left_to_read -= A1FS_BLOCK_SIZE;
                        if (bytes_left_to_read == 0) {
                            return bytes_read_into_buf;
                        }
                    }
                }
            }
        }
        //need to continue reading from indirect blocks
        for(int i = 0;i < 500; i++) {
            struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
            for(int j = 0; j < (int) indirect->extent[i].count; j++) {
                if(bytes_left_to_read < A1FS_BLOCK_SIZE) { //partial block read
                    // memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                    //        buf + bytes_read_into_buf, bytes_left_to_read);
                    memcpy(buf + bytes_read_into_buf, fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE, bytes_left_to_read);
                    bytes_read_into_buf += bytes_left_to_read;
                    return bytes_read_into_buf;
                } else { //full block read
                    // memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                    //        buf + bytes_read_into_buf, A1FS_BLOCK_SIZE);
                    memcpy(buf + bytes_read_into_buf, fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE, A1FS_BLOCK_SIZE);
                    bytes_read_into_buf += A1FS_BLOCK_SIZE;
                    bytes_left_to_read -= A1FS_BLOCK_SIZE;
                    if(bytes_left_to_read == 0) {
                        return bytes_read_into_buf;
                    }
                }
            }
        }
    } else {
        for(int i=offset_extent; i< 500; i++) {
            struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
            if (indirect->extent[i].start != 0) {
                for (int j = 0; j < (int) indirect->extent[i].count; j++) {
                    if (i == offset_extent && offset_extent_set == 1) { //jump to where offset is
                        j = offset_extent_count + 1;
                    }
                    if (bytes_left_to_read < A1FS_BLOCK_SIZE) { //partial block read
                        // memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                        //        buf + bytes_read_into_buf, bytes_left_to_read);
                        memcpy(buf + bytes_read_into_buf,
                               fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               bytes_left_to_read);
                        bytes_read_into_buf += bytes_left_to_read;
                        return bytes_read_into_buf;
                    } else { //full block read
                        // memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                        //        buf + bytes_read_into_buf, A1FS_BLOCK_SIZE);
                        memcpy(buf + bytes_read_into_buf,
                               fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               A1FS_BLOCK_SIZE);
                        bytes_read_into_buf += A1FS_BLOCK_SIZE;
                        bytes_left_to_read -= A1FS_BLOCK_SIZE;
                        if (bytes_left_to_read == 0) {
                            return bytes_read_into_buf;
                        }
                    }
                }
            }
        }
    }

    //implement 1 block read
//    struct a1fs_ibitmap *data = (struct a1fs_ibitmap *)(fs->image + A1FS_BLOCK_SIZE*(fs->sb->block_table +in->extent[0].start) + offset);  //shortcut to access bytes
//    char *p = &data->map[offset];
//    unsigned int toread = difference - size;
//    if(toread > 0) {
//        // strncpy(buf, p, toread);
//        memcpy(buf, p, size);
//        return size;
//    }else if (toread ==0){
//        *buf = '\0';
//    }

    return 0;
    // return -ENOSYS;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
    (void)fi;// unused
    fs_ctx *fs = get_fs();

    //TODO: write data from the buffer into the file at given offset, possibly
    // "zeroing out" the uninitialized range
    struct a1fs_superblock *sb = fs->sb;
    // struct a1fs_ibitmap *ibitmap = fs->ibitmap;
    //struct a1fs_bbitmap *bbitmap = fs->bbitmap;
    int num = path_lookup(path);
    if (num == -1) {
        return -errno;
    }
    //find inode of file
    struct a1fs_inode *file = (struct a1fs_inode *)(fs->image + A1FS_BLOCK_SIZE*4 + sizeof(a1fs_inode) * num);

    if(file->size < offset+size) {
        a1fs_truncate(path, offset + size); //extend file as necessary
    }
    int bytes_left_to_write = size;
    int bytes_written = 0;
    int bytes_read = 0, offset_extent = 0, offset_extent_count = 0, offset_extent_set = 0, in_indirect = 0, offset_found = 0;
    while(bytes_read < offset) { //first find offset
        for(int i = 0; i < 13 && bytes_read < offset; i++) {
            if(file->extent[i].start != 0) {
                for(int j = 0; j < (int) file->extent[i].count; j++) {
                    if(bytes_read/A1FS_BLOCK_SIZE < offset/A1FS_BLOCK_SIZE) { //skip blocks
                        bytes_read += A1FS_BLOCK_SIZE;
                    } else if(bytes_read/A1FS_BLOCK_SIZE != offset/A1FS_BLOCK_SIZE){ // find exact byte
                        offset_extent = i;
                        offset_extent_set = 1;
                        offset_extent_count = j;
                        for (int k = 0; k < A1FS_BLOCK_SIZE; k++) {
                            bytes_read++;
                            if(bytes_read == offset) { //found byte
                                if(bytes_left_to_write < (A1FS_BLOCK_SIZE-k)) { //write partial block
                                    memcpy(fs->image + (sb->block_table + file->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                           k, buf, bytes_left_to_write);
                                    clock_gettime(CLOCK_REALTIME, &(file->mtime));
                                    return size;
                                } else { //write full block and exit looking for offset
                                    memcpy(fs->image + (sb->block_table + file->extent[i].start + file->extent[i].count) * A1FS_BLOCK_SIZE +
                                           k, buf, (A1FS_BLOCK_SIZE - bytes_left_to_write));
                                    bytes_left_to_write -= (A1FS_BLOCK_SIZE-bytes_left_to_write);
                                    bytes_written += (A1FS_BLOCK_SIZE-bytes_left_to_write);
                                    offset_found = 1;
                                }
                            }
                            if(offset_found) {
                                break;
                            }
                        }
                    }
                    if(offset_found) {
                        break;
                    }
                }
            } //else { //need to allocate additional data blocks to reach offset
            //}
            if(offset_found) {
                break;
            }
        }
        //same thing as above but if indirect extent is used
        for(int i = 0; i < 500 && bytes_read < offset; i++) {
            in_indirect = 1;
            struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
            if(indirect->extent[i].start != 0) {
                for(int j = 0; j < (int) indirect->extent[i].count; j++) {
                    if(bytes_read/A1FS_BLOCK_SIZE < offset/A1FS_BLOCK_SIZE) { //skip blocks
                        bytes_read += A1FS_BLOCK_SIZE;
                    } else if(bytes_read/A1FS_BLOCK_SIZE != offset/A1FS_BLOCK_SIZE){ // find exact byte
                        offset_extent = i;
                        offset_extent_set = 1;
                        offset_extent_count = j;
                        for (int k = 0; k < A1FS_BLOCK_SIZE; k++) {
                            bytes_read++;
                            if(bytes_read == offset) { //found byte
                                if(bytes_left_to_write < (A1FS_BLOCK_SIZE-k)) { //write partial block
                                    memcpy(fs->image + (sb->block_table + indirect->extent[i].start + indirect->extent[i].count) * A1FS_BLOCK_SIZE +
                                           k, buf, bytes_left_to_write);
                                    clock_gettime(CLOCK_REALTIME, &(file->mtime));
                                    return size;
                                } else { //write full block and exit looking for offset
                                    memcpy(fs->image + (sb->block_table + indirect->extent[i].start + indirect->extent[i].count) * A1FS_BLOCK_SIZE +
                                           k, buf, (A1FS_BLOCK_SIZE - bytes_left_to_write));
                                    bytes_left_to_write -= (A1FS_BLOCK_SIZE-bytes_left_to_write);
                                    bytes_written += (A1FS_BLOCK_SIZE-bytes_left_to_write);
                                    offset_found = 1;
                                }
                            }
                            if(offset_found) {
                                break;
                            }
                        }
                    }
                    if(offset_found) {
                        break;
                    }
                }
            } //else { //need to allocate additional data blocks to reach offset
            //}
            if(offset_found) {
                break;
            }
        }

    }
    //now offset bytes have been read, time to write.
    if(!in_indirect) {
        for (int i = offset_extent; i < 13; i++) {
            if (file->extent[i].start != 0) {
                for (int j = 0; j < (int) file->extent[i].count; j++) {
                    if (i == offset_extent && offset_extent_set == 1) { //jump to where offset is
                        j = offset_extent_count + 1;
                    }
                    if (bytes_left_to_write < A1FS_BLOCK_SIZE) { //partial block write
                        memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               buf + bytes_written, bytes_left_to_write);
                        bytes_written += bytes_left_to_write;
                        clock_gettime(CLOCK_REALTIME, &(file->mtime));
                        return bytes_written;
                    } else { //full block write
                        memcpy(fs->image + (sb->block_table + file->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               buf + bytes_written, A1FS_BLOCK_SIZE);
                        bytes_written += A1FS_BLOCK_SIZE;
                        bytes_left_to_write -= A1FS_BLOCK_SIZE;
                        if (bytes_left_to_write == 0) {
                            clock_gettime(CLOCK_REALTIME, &(file->mtime));
                            return bytes_written;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 500; i++) {
            struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
            if (indirect->extent[i].start != 0) {
                for (int j = 0; j < (int) indirect->extent[i].count; j++) {
                    if (bytes_left_to_write < A1FS_BLOCK_SIZE) { //partial block write
                        memcpy(fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               buf + bytes_written, bytes_left_to_write);
                        bytes_written += bytes_left_to_write;
                        clock_gettime(CLOCK_REALTIME, &(file->mtime));
                        return bytes_written;
                    } else { //full block write
                        memcpy(fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               buf + bytes_written, A1FS_BLOCK_SIZE);
                        bytes_written += A1FS_BLOCK_SIZE;
                        bytes_left_to_write -= A1FS_BLOCK_SIZE;
                        if (bytes_left_to_write == 0) {
                            clock_gettime(CLOCK_REALTIME, &(file->mtime));
                            return bytes_written;
                        }
                    }
                }
            }
        }
    } else {
        for (int i = offset_extent; i < 500; i++) {
            struct a1fs_indirect_ext *indirect = (struct a1fs_indirect_ext*)(fs->image +A1FS_BLOCK_SIZE*(sb->block_table+file->indirect));
            if (indirect->extent[i].start != 0) {
                for (int j = 0; j < (int) indirect->extent[i].count; j++) {
                    if (i == offset_extent && offset_extent_set == 1) { //jump to where offset is
                        j = offset_extent_count + 1;
                    }
                    if (bytes_left_to_write < A1FS_BLOCK_SIZE) { //partial block write
                        memcpy(fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               buf + bytes_written, bytes_left_to_write);
                        bytes_written += bytes_left_to_write;
                        clock_gettime(CLOCK_REALTIME, &(file->mtime));
                        return bytes_written;
                    } else { //full block write
                        memcpy(fs->image + (sb->block_table + indirect->extent[i].start + j) * A1FS_BLOCK_SIZE,
                               buf + bytes_written, A1FS_BLOCK_SIZE);
                        bytes_written += A1FS_BLOCK_SIZE;
                        bytes_left_to_write -= A1FS_BLOCK_SIZE;
                        if (bytes_left_to_write == 0) {
                            clock_gettime(CLOCK_REALTIME, &(file->mtime));
                            return bytes_written;
                        }
                    }
                }
            }
        }
    }


    return -ENOSYS;
}


static struct fuse_operations a1fs_ops = {
    .destroy  = a1fs_destroy,
    .statfs   = a1fs_statfs,
    .getattr  = a1fs_getattr,
    .readdir  = a1fs_readdir,
    .mkdir    = a1fs_mkdir,
    .rmdir    = a1fs_rmdir,
    .create   = a1fs_create,
    .unlink   = a1fs_unlink,
    .utimens  = a1fs_utimens,
    .truncate = a1fs_truncate,
    .read     = a1fs_read,
    .write    = a1fs_write,
};

int main(int argc, char *argv[])
{
    a1fs_opts opts = {0};// defaults are all 0
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (!a1fs_opt_parse(&args, &opts)) return 1;

    fs_ctx fs = {0};
    if (!a1fs_init(&fs, &opts)) {
        fprintf(stderr, "Failed to mount the file system\n");
        return 1;
    }

    return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
