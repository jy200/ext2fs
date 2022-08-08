#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <time.h>   //for clock_gettime


#include "a1fs.h"
#include "map.h"


/** Command line options. */
typedef struct mkfs_opts {
    /** File system image file path. */
    const char *img_path;
    /** Number of inodes. */
    size_t n_inodes;

    /** Print help and exit. */
    bool help;
    /** Overwrite existing file system. */
    bool force;
    /** Zero out image contents. */
    bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
    fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
    char o;
    while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
        switch (o) {
            case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

            case 'h': opts->help  = true; return true;// skip other arguments
            case 'f': opts->force = true; break;
            case 'z': opts->zero  = true; break;

            case '?': return false;
            default : assert(false);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Missing image path\n");
        return false;
    }
    opts->img_path = argv[optind];

    if (opts->n_inodes == 0) {
        fprintf(stderr, "Missing or invalid number of inodes\n");
        return false;
    }
    return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
    const struct a1fs_superblock *sb = (const struct a1fs_superblock *)(image + A1FS_BLOCK_SIZE);
    if (sb->magic != A1FS_MAGIC)
        return false;
    return true;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
    //NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777

    //Superblock initialized
    struct a1fs_superblock *sb = (struct a1fs_superblock *)(image + A1FS_BLOCK_SIZE);
    sb->magic = A1FS_MAGIC;
    sb->size = size;
    sb->inode_count = opts->n_inodes;
    sb->block_count = size/A1FS_BLOCK_SIZE - (3 + opts->n_inodes);  //total blocks - (sb, 2 bitmaps and total inodes)
    sb->used_block_count = 1;
    sb->used_inode_count = 1;
    sb->inode_bitmap = 2;
    sb->block_bitmap = 3;
    sb->inode_table = 4;
    sb->block_table = 4 + (opts->n_inodes * sizeof(a1fs_inode) + A1FS_BLOCK_SIZE-1)/A1FS_BLOCK_SIZE;    //rounds up inode blocks

    //initialize root inode
    struct a1fs_inode *inode = (struct a1fs_inode *)(image + A1FS_BLOCK_SIZE * 4);
    inode->mode = S_IFDIR | 0777;
    inode->links = 2;
    inode->size = 0;
    clock_gettime(CLOCK_REALTIME, &inode->mtime);
    inode->extent[0].start = 0;
    inode->extent[0].count = 1;
    inode->indirect = 0;
    inode->block_count = 1; //block 0 is root
    inode->num = 0;
    inode->parent_num = 0;
    inode->extent_count = 1;

    struct a1fs_ibitmap *imap = (struct a1fs_ibitmap *)(image + A1FS_BLOCK_SIZE * sb->inode_bitmap);
    struct a1fs_bbitmap *bmap = (struct a1fs_bbitmap *)(image + A1FS_BLOCK_SIZE * sb->block_bitmap);
    memset(image + A1FS_BLOCK_SIZE * sb->inode_bitmap, 0, A1FS_BLOCK_SIZE); //initialize bitmaps to 0
    memset(image + A1FS_BLOCK_SIZE * sb->block_bitmap, 0, A1FS_BLOCK_SIZE);
    imap->map[0] = 1;   //allocate root inode and block
    bmap->map[0] = 1;
    return true;
}


int main(int argc, char *argv[])
{
    mkfs_opts opts = {0};// defaults are all 0
    if (!parse_args(argc, argv, &opts)) {
        // Invalid arguments, print help to stderr
        print_help(stderr, argv[0]);
        return 1;
    }
    if (opts.help) {
        // Help requested, print it to stdout
        print_help(stdout, argv[0]);
        return 0;
    }

    // Map image file into memory
    size_t size;
    void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
    if (image == NULL) return 1;

    // Check if overwriting existing file system
    int ret = 1;
    if (!opts.force && a1fs_is_present(image)) {
        fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
        goto end;
    }

    if (opts.zero) memset(image, 0, size);
    if (!mkfs(image, size, &opts)) {
        fprintf(stderr, "Failed to format the image\n");
        goto end;
    }

    ret = 0;
end:
    munmap(image, size);
    return ret;
}
