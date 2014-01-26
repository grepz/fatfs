#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>

#include "fatfs.h"

static int file_read(uint8_t *buf, off_t sector, unsigned int sector_num);
static int file_init(void);

fatfs_t fatfs = {
    .hwops = {
        .init = &file_init,
        .read = &file_read,
    },
};

typedef struct __dev_file
{
    int fd;
    off_t curoff;
} dev_file_t;

dev_file_t file;

int main(int argc, char *argv[])
{
    int ret, i;
    char rdbuf[4096];

    (void)argc;
    (void)argv;

    ret = fatfs_mount(&fatfs);
    if (ret) {
        printf("Failed mounting FatFS image, reason: %d\n", ret);
        return EXIT_FAILURE;
    }

    fatfs_printfs(&fatfs);

    ret = fatfs_open(&fatfs, "TEST.OUT");
    if (ret) {
        printf("Failed opening first file for reading: %d\n", ret);
        return EXIT_FAILURE;
    }
    int readsz = 0;
    for (i = 0; i < 140; i++) {
        ret = fatfs_read(&fatfs, rdbuf, 512);
        if (ret < 0) {
            printf("Error: %d\n", ret);
            return EXIT_FAILURE;
        }
        readsz += ret;
    }
    printf("Left: %d\n", readsz);
    ret = fatfs_read(&fatfs, rdbuf, 780);
    if (ret < 0) {
        printf("error\n");
        return EXIT_FAILURE;
    }
    printf("Read: %d\n", ret);
    for (i = 0; i < 559; i++)
        printf("%c", rdbuf[i]);

    fatfs_close(&fatfs);

    ret = fatfs_open(&fatfs, "TEST2.TXT");
    if (ret) {
        printf("Failed opening second file for reading: %d\n", ret);
        return EXIT_FAILURE;
    }
    fatfs_close(&fatfs);

    ret = fatfs_open(&fatfs, "TEST3.TXT");
    if (ret) {
        printf("Failed opening third file for reading: %d\n", ret);
        return EXIT_FAILURE;
    }
    fatfs_close(&fatfs);

    ret = fatfs_open(&fatfs, "SOMEFILE.TXT");
    if (ret) {
        printf("Failed to open.\n");
        return EXIT_FAILURE;
    }

    ret = fatfs_read(&fatfs, rdbuf, 780);
    if (ret < 0) {
        printf("error\n");
        return EXIT_FAILURE;    }

    for (i = 0; i < ret; i++)
        printf("%c", rdbuf[i]);

    fatfs_umount(&fatfs);

    return 0;
}

static int file_init(void)
{
    file.fd = open("/home/grepz/Projects/FatFS/fatfs.img", O_RDONLY);
    if (file.fd == -1)
        return -errno;

    file.curoff = 0;

    return 0;
}

static int file_read(uint8_t *buf, off_t sector, unsigned int sector_num)
{
    int ret = 0;
    off_t off;

    off = (sector << SECTOR_PGSHIFT);

    printf("Reading %d bytes at offset %lu\n",
           (1 << SECTOR_PGSHIFT) * sector_num, off);

    ret = pread(file.fd, buf, (1 << SECTOR_PGSHIFT) * sector_num, off);
    if (ret == -1)
        return -errno;
    else if (ret != (1 << SECTOR_PGSHIFT))
        return -EIO;

    return sector_num;
}
