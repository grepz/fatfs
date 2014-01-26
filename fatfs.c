#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include <errno.h>

#include "fatfs.h"

static uint8_t *__sbuf;
static uint8_t *__fbuf;

static int __fatfs_hwread(fatfs_t *fs, uint8_t *buf,
                          off_t sector, unsigned int sector_num);
static int __get_bpb(fatfs_t *fs);
static int __path2dirent(const char **path, char *dp, char *terminator);
static off_t __fatfs_get_cluster(fatfs_t *fs, uint32_t clust);
static int __get_curr_sector(fatfs_t *fs, off_t pos);

uint16_t fat_getuint16(uint8_t *ptr)
{
#ifdef CONFIG_ENDIAN_BIG
  return ((uint16_t)ptr[0] << 8) | ptr[1];
#else
  return ((uint16_t)ptr[1] << 8) | ptr[0];
#endif
}

uint32_t fat_getuint32(uint8_t *ptr)
{
#ifdef CONFIG_ENDIAN_BIG
  return ((uint32_t)fat_getuint16(&ptr[0]) << 16) | fat_getuint16(&ptr[2]);
#else
  return ((uint32_t)fat_getuint16(&ptr[2]) << 16) | fat_getuint16(&ptr[0]);
#endif
}

int fatfs_mount(fatfs_t *fs)
{
    int ret;

    fs->hwops.init();

    __sbuf = malloc(1 << 10);
    if (!__sbuf)
        return -errno;

    ret = __fatfs_hwread(fs, __sbuf, 0, 1);
    if (ret < 0)
        return ret;

    /* Read and process boot sector. Check if we work with known FS. */
    ret = __get_bpb(fs);
    if (ret)
        return ret;

    return ret;
}

void fatfs_umount(fatfs_t *fs)
{
    (void)fs;
    free(__sbuf);
}

int fatfs_open(fatfs_t *fs, const char *path)
{
    int ret, i;
    off_t diroff = 0;
    char entry_name[12];
    char term;

    if (!path || *path == '\0')
        return -1;

    entry_name[11] = '\0';
    ret = __path2dirent(&path, entry_name, &term);
    if (ret != 0)
        return ret;

    /* We work only with root directory entries, so starting sector is root
     * dir sector
     */
    for (i = 0; i < fs->bpb.rootdirsect; i++) {
        ret = __fatfs_hwread(fs, __sbuf, fs->rootdir_off + i, 1);
        while (diroff != fs->bpb.bpersect) {
            if (!memcmp(entry_name, __sbuf + diroff, 11)) {
                goto __dent_found;
            }
            diroff += 32;
        }
    }

    return -ENOENT;

__dent_found:

    fs->file.currclust = fat_getuint16(__sbuf + diroff + DIRENT_FSTCLSTL_OFF);
    fs->file.size      = fat_getuint32(__sbuf + diroff + DIRENT_FSIZE_OFF);
    fs->file.pos       = 0;
    fs->file.currsect  = 0;

    printf("Found entry: %s; Data cluster: %d. Size: %d\n",
           entry_name, fs->file.currclust, fs->file.size);

    printf("Data sector: %lu\n", SECT_OF_CLUST(fs, fs->file.currclust));

//    printf("%s\n", __sbuf);

    return 0;
}

int fatfs_read(fatfs_t *fs, uint8_t *buf, size_t sz)
{
    int ret;
    size_t btrd;
    unsigned int nsectors, readsz, bytesread;
    int sectorindex;
    uint32_t clust;

    btrd = fs->file.size - fs->file.pos;
    if (sz > btrd)
        sz = btrd;

    if (!fs->file.currsect) {
        ret = __get_curr_sector(fs, fs->file.pos);
        if (ret)
            return ret;
    }

    readsz = 0;
    sectorindex = fs->file.pos & (fs->bpb.bpersect - 1);

    while (sz > 0) {
        bytesread  = 0;
        nsectors = sz / fs->bpb.bpersect;
        if (nsectors > 0 && sectorindex == 0) {
            if (nsectors > fs->file.sectsinclust)
                nsectors = fs->file.sectsinclust;

            ret = __fatfs_hwread(fs, buf, fs->file.currsect, nsectors);
            if (ret < 0)
                return ret;

            fs->file.sectsinclust -= nsectors;
            fs->file.currsect     += nsectors;
            bytesread = nsectors * fs->bpb.bpersect;
        } else {
            ret = __fatfs_hwread(fs, __sbuf, fs->file.currsect, 1);
            if (ret < 0)
                return ret;

            bytesread = fs->bpb.bpersect - sectorindex;
            if (bytesread > sz) {
                bytesread = sz;
            } else {
                fs->file.sectsinclust--;
                fs->file.currsect++;
            }

            memcpy(buf, &__sbuf[sectorindex], bytesread);
        }

        buf          += bytesread;
        fs->file.pos += bytesread;
        readsz       += bytesread;
        sz           -= bytesread;
        sectorindex   = fs->file.pos & (fs->bpb.bpersect - 1);

        if (fs->file.sectsinclust < 1) {
            clust = __fatfs_get_cluster(fs, fs->file.currclust);
            printf("New cluster: %d\n", clust);
            if (clust < 2 || clust >= fs->bpb.clust_num) {
                return -EINVAL;
            }

            fs->file.currclust = clust;
            fs->file.currsect = SECT_OF_CLUST(fs, clust);
            fs->file.sectsinclust = fs->bpb.sectperclust;
        }
    }

    return readsz;
}

void fatfs_close(fatfs_t *fs)
{
    memset(&fs->file, 0, sizeof(fs->file));
}

int fatfs_freecount(const fatfs_t *fs)
{
    (void)fs;
    return 0;
}

static int __fatfs_hwread(fatfs_t *fs, uint8_t *buf,
                          off_t sector, unsigned int sector_num)
{
    int ret;

    if (!fs)
        return -ENODEV;

    ret = fs->hwops.read(buf, sector, sector_num);
    if ((unsigned int)ret == sector_num)
        return 0;

    return ret;
}

static int __get_bpb(fatfs_t *fs)
{
    uint32_t fatsect_num;

    /* Unknown format? */
    if (fat_getuint16(&__sbuf[BPB_SIG_OFF]) != BPB_SIG_FAT16)
        return 1;
    fs->bpb.bpersect = fat_getuint16(&__sbuf[BPB_BPERSECT_OFF]);
    if (fs->bpb.bpersect != (1 << SECTOR_PGSHIFT)) {
        return 1;
    }

    /* Number of dir entries in a root dir */
    fs->bpb.rootent = fat_getuint16(&__sbuf[BPB_ROOTENT_OFF]);
    /* Sectors per one FAT */
    fs->bpb.fatsect = fat_getuint16(&__sbuf[BPB_FAT16SZ_OFF]);
    /* Total number of sectors on volume */
    fs->bpb.fattotsect = fat_getuint16(&__sbuf[BPB_TOTSECT_OFF]);

    fs->bpb.rootdirsect = ((fs->bpb.rootent * 32) +
                       (fs->bpb.bpersect - 1))/fs->bpb.bpersect;
    /* Not a fat12/16 fs */
    if (!fs->bpb.fatsect || !fs->bpb.fattotsect)
        return 2;

    fs->bpb.rsvdsect     = fat_getuint16(&__sbuf[BPB_RSVDSECTCNT_OFF]);
    fs->bpb.fat_num      = __sbuf[BPB_FATNUM_OFF];
    fs->bpb.sectperclust = __sbuf[BPB_SECTPERCLUST_OFF];
    /* Total number of FAT sectors */
    fatsect_num      = fs->bpb.fat_num * fs->bpb.fatsect;
    /* Number of data sectors */
    fs->bpb.datasect_num = fs->bpb.fattotsect -
        (fs->bpb.rsvdsect + fatsect_num + fs->bpb.rootdirsect);
    fs->bpb.clust_num = fs->bpb.datasect_num / fs->bpb.sectperclust;

    /* Check if FS is fat16 */
    if (fs->bpb.clust_num > FAT16_MAXCLUST)
        return 2;

    /* FAT table offset */
    fs->fat_off = fs->bpb.rsvdsect;
    /* Root directory entry offset */
    fs->rootdir_off = fs->fat_off + fatsect_num;
    /* Data clusters offset */
    fs->data_off = fs->fat_off + fatsect_num + fs->bpb.rootdirsect;

    return 0;
}

void fatfs_printfs(const fatfs_t *fs)
{
    printf("FAT filesystem info:\n");
    printf("                Root entries: %d\n", fs->bpb.rootent);
    printf("             Sectors per FAT: %d\n", fs->bpb.fatsect);
    printf("     Total number of sectors: %d\n", fs->bpb.fattotsect);
    printf("            Bytes per sector: %d\n", fs->bpb.bpersect);
    printf("            Reserved sectors: %d\n", fs->bpb.rsvdsect);
    printf("      Root directory sectors: %d\n", fs->bpb.rootdirsect);
    printf("      Number of data sectors: %d\n", fs->bpb.datasect_num);
    printf("     Number of data clusters: %d\n", fs->bpb.clust_num);
    printf("        Number of FAT tables: %d\n", fs->bpb.fat_num);
    printf("         Sectors per cluster: %d\n", fs->bpb.sectperclust);
    printf("         Data sectors offset: %lu\n", fs->data_off);
    printf("Root directory offset offset: %lu\n", fs->rootdir_off);
    printf("            FAT table offset: %lu\n", fs->fat_off);
}


static int __path2dirent(const char **path, char *dp, char *terminator)
{
    const char *_path = *path;
    char ch;
    int ndx = 0, endndx = 8;

    memset(dp, ' ', 11);

    for (;;) {
        ch = *_path++;
        if ((ch == '\0' || ch == '/') && ndx != 0) {
            *terminator = ch;
            *path = _path;
            return 0;
        } else if (!isgraph(ch))
            return -1;
        else if (ch == '.' && endndx == 8) {
            ndx = 8;
            endndx = 11;
            continue;
        } else if (ch == '"' || (ch >= '*' && ch <= ',') ||
                   ch == '.' || ch == '/' ||
                   (ch >= ':' && ch <= '?') ||
                   (ch >= '[' && ch <= ']') ||
                   (ch == '|'))
            return -1;
        else if (islower(ch))
            ch = toupper(ch);

        if (ndx >= endndx)
            return -1;

        dp[ndx++] = ch;
    }

    return 0;
}

static int __get_curr_sector(fatfs_t *fs, off_t pos)
{
    int sect_off;

    if (pos > fs->file.pos)
        return -EINVAL;

    sect_off = (pos/fs->bpb.bpersect) & (fs->bpb.sectperclust - 1);
    fs->file.currsect = sect_off + SECT_OF_CLUST(fs, fs->file.currclust);
    /* Remaining sectors in cluster to read */
    fs->file.sectsinclust = fs->bpb.sectperclust - sect_off;

    return 0;
}

static off_t __fatfs_get_cluster(fatfs_t *fs, uint32_t clust)
{
    int ret;
    unsigned int fatoff, fatind, _clust;
    off_t fatsect;

    if (clust < 2 && clust < fs->bpb.clust_num)
        return (off_t)(-EINVAL);
#if 0
    fatoff = 2 * clust;
    fatsect = fs->fat_off + fatoff/fs->bpb.bpersect;
    fatind = fatoff & (fs->bpb.bpersect - 1);
    printf("Fatsect: %d\n", fatsect);

    ret = __fatfs_hwread(fs, __sbuf, fatsect, 1);
    if (ret < 0)
        return (off_t)(-EINVAL);
#endif

    fatoff = (clust * 3) / 2;
    fatsect = fs->fat_off + fatoff/fs->bpb.bpersect;

    if (__fatfs_hwread(fs, __sbuf, fatsect, 1) < 0)
        return (off_t)(-EINVAL);

    fatind = fatoff & (fs->bpb.bpersect - 1);
    _clust = __sbuf[fatind];

    fatind++;
    if (fatind >= fs->bpb.bpersect) {
        fatsect ++;
        fatind = 0;
        if (__fatfs_hwread(fs, __sbuf, fatsect, 1) < 0)
            return (off_t)(-EINVAL);
    }

    _clust |= (unsigned int)__sbuf[fatind] << 8;
    if ((clust & 1) != 0)
        _clust >>= 4;
    else
        _clust &= 0x0fff;

    return _clust;

    return fat_getuint16(&__sbuf[fatind]);
}
