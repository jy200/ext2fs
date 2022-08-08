#include <stdio.h>
#include <stdlib.h>
#include "a1fs.h"

int main()
{
	printf("inode: %ld\r\n", sizeof(struct a1fs_inode));
	printf("indirect size: %ld\r\n", sizeof(struct a1fs_indirect_ext));
	printf("pointer to indirect size: %ld\r\n", sizeof(struct a1fs_indirect_ext *));
	// printf("entry size: %ld\n", sizeof(a1fs_dentry));
	// printf("inode bitmap: %ld\n", sizeof(a1fs_ibitmap));
	// printf("%block bitmap: ld\n", sizeof(a1fs_bbitmap));
}