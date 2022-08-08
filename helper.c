// HELPER FUNCTIONS AND STRUCTS

/* Returns the inode number for the element at the end of the path
 * if it exists.
 * Possible errors include:
 *   - The path is not an absolute path: -1
 *   - An element on the path cannot be found: -1
 *   - component is not directory: -2
 */
#include "helper.h"

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
        if (strlen(p) >= A1FS_NAME_MAX) return -3;
        if (S_ISREG(block_inode->mode)) return -2;
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