# Run
Setup fuse and edit setup.sh to replace with the proper directory

# Proposal - Disk Image
## How we partition disk space:
- Divide the disk into 4KiB blocks. Arrangement: superblock, inode bitmap, block bitmap,
data region.
- Superblock will include the number of inodes, number of blocks, and where the blocks
where block bitmap, inode bitmap and inode table start. Will also store the number of
free blocks and inodes in the system.
- Inode bitmap is an array of bytes that shows which inodes are being used. 1 represents
used, 0 represents available. Likewise, block bitmap follows a similar scheme.
- Inodes are the blocks between block bitmap and data blocks
Extents
- Starting extent block and number of blocks in the extent are stored in struct a1fs_extent
- 13 pointers to extents inside inodes (based on VSFS):
    - 0-11 point to extents
    - 12 points to a single indirect block: max 500 pointers to extents because the
maximum number of extents is 512
Describe how to allocate disk blocks to a file when it is extended. In other words, how do you
identify available extents and allocate it to a file?
- Allocation depends on size. No data blocks will be allocated for empty files
- Algorithm for allocation data blocks:
    - Check the end of the file’s last extent to see if there’s free data blocks in the
block bitmap.
    - If there are enough free data blocks, allocate required data blocks into one extent
and append to file’s inode extent pointers.
    - If there are free data blocks but not enough for our extent, place all the possible
free data blocks into one extent. Check the block bitmap and place the rest of the
data blocks in the nearest available block (repeat until all of the required data
blocks have been allocated).


## Explain how your allocation algorithm will help keep fragmentation low.
- We keep fragmentation low by claiming data blocks after the ends of our existing extents
when available.
Describe how to free disk blocks a file is truncated (reduced in size) or deleted.
- Starting from the end of the last extent (because extents are contiguous), we go into the
block bitmap and flip the corresponding data blocks in the extent from 1 to 0. In other
words, the data will still be there, but it will be free for future allocation.
    - If the data blocks in the extent are fully freed, we will need to delete the
corresponding extent pointer inside the inode.
    - If we need to free up part of an extent instead of deleting it, we will decrease the
corresponding extent’s length (a1fs_blk_t count) by the number of blocks freed.
- Go into the second last extent, third last, etc... and repeat until the correct number of
blocks have been freed.

## Describe the algorithm to seek a specific byte in a file.
- Each data block has a size 4KiB. Since extents record their count in struct a1fs_extent,
we can use this information to seek a specific byte.
- We go through the relative inode’s first 12 extent pointers and keep track of the number
of bytes by looking at the count of the a1fs_extent and multiplying it by 4096 bytes. If the
byte we want to find is less than that we go to that data block, else we continue going
through the extents.
    - If the byte is not located within the first 12 extent pointers, we go into our 13th
pointer to a single indirect block and repeat the process. If after 512 extents, we
do not find the specific byte, we return an error

## Describe how to allocate and free inodes.
- Inode bitmap tells us which inodes are available.
- Allocating inodes:
We allocate to the first available inode (represented by 0) and switch to 1 (in use).
- Freeing inodes:
When inode is no longer needed, corresponding byte in bitmap is turned to a 0.
Describe how to allocate and free directory entries within the data block(s) that represent the
directory.
- Allocate: Inode -> available extent -> data block. Add inode number of entry and
filename. Allocate more data blocks (extents) if required.
- Freeing: First check if the entry exists (if it doesn’t, then do nothing). If it does, we use
the truncating/ freeing algorithm above to remove the entry’s inode number and
filename.


## Describe how to lookup a file given a full path to it, starting from the root directory.
- We know the inode for root directory
(superblock -> block where inode table begins -> first inode)
- Read inode for root directory for extent pointers
- Read the data blocks pointed by the first extent pointer and search for following
directory/file’s inode
- If this is the file we’re looking for:
    - Search inode block containing that file for first extent’s starting block
    - Read that block to access beginning of the file we need
- Else, continue reading through inode -> extent -> data block in extent -> search through
data block for following file’s inode


