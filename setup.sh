fusermount -u /tmp/test
rm -f _img_
make
truncate -s 10M _img_
./mkfs.a1fs -i 4096 _img_ -f
./a1fs _img_ /tmp/test -d