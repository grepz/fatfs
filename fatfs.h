#ifndef __FATFS_H
#define __FATFS_H

#define FAT12_MAXCLUST 0xFF5
#define FAT16_MAXCLUST 0xFFF5

#define BPB_SIG_FAT16 0xAA55

#define BPB_BPERSECT_OFF     11 /* 2B. Bytes per sector */
#define BPB_SECTPERCLUST_OFF 13 /* 1B. Sectors per cluster. pow of 2. */
#define BPB_RSVDSECTCNT_OFF  14 /* 2B. Reserved sectors to align data area */
#define BPB_FATNUM_OFF       16 /* 1B. Num of FAT's */
#define BPB_ROOTENT_OFF      17 /* 2B. Num of dirs in root dir(fat12/16) */
#define BPB_TOTSECT_OFF      19 /* 2B. Total num of sectors on vol. */
#define BPB_FAT16SZ_OFF      22 /* 2B. Sectors occupied by one FAT */
#define BPB_SIG_OFF          510

#define DIRENT_NAME_OFF      0 /* 11B. Directory entry name */
#define DIRENT_FSTCLSTH_OFF 20 /* 2B. High word of entry's first data cluster */
#define DIRENT_FSTCLSTL_OFF 26 /* 2B. Low word of entry's first data cluster */
#define DIRENT_FSIZE_OFF    28 /* 4B. File size XXX: 32? */

#define SECTOR_NUM 67108864 /* 8 pages in 1 block, 8192 blocks, 32 sectors */
#define SECTOR_PGSHIFT 10

#define SECT_OF_CLUST(f,n) (((n-2) * f->bpb.sectperclust) + f->data_off)

typedef struct __hwops
{
    int (*init)(void);
    int (*read)(uint8_t *buf, off_t sector, unsigned int sector_num);
} hwops_t;

typedef struct __fatfs_bpb
{
    uint16_t rootent;
    uint16_t fatsect;
    uint16_t fattotsect;
    uint16_t bpersect;
    uint16_t rsvdsect;
    uint16_t rootdirsect;
    uint32_t datasect_num;
    uint32_t clust_num;
    uint8_t fat_num;
    uint8_t sectperclust;
} fatfs_bpb_t;

typedef struct __fatfs_file
{
    uint16_t currclust;
    uint32_t pos;
    uint32_t size;
    uint32_t currsect;
    uint16_t sectsinclust;
} fatfs_file_t;

typedef struct __fatfs
{
    hwops_t      hwops;
    uint8_t      status;
    uint8_t      fstype;
    fatfs_bpb_t  bpb;
    fatfs_file_t file;
    off_t        fat_off;
    off_t        rootdir_off;
    off_t        data_off;
} fatfs_t;

int fatfs_mount(fatfs_t *fs);
void fatfs_umount(fatfs_t *fs);

int fatfs_read(fatfs_t *fs, uint8_t *buf, size_t sz);

void fatfs_printfs(const fatfs_t *fs);

int fatfs_open(fatfs_t *fs, const char *path);
void fatfs_close(fatfs_t *fs);

#endif /* __FATFS_H */
