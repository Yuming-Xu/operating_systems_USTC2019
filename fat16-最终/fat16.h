#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Unit size */
#define BYTES_PER_SECTOR 512
#define BYTES_PER_DIR 32

/* File property */
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME 0x0f

#define MAX_SHORT_NAME_LEN 13

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

/* FAT16 BPB Structure */
typedef struct {
  BYTE BS_jmpBoot[3];
  BYTE BS_OEMName[8];
  WORD BPB_BytsPerSec;
  BYTE BPB_SecPerClus;
  WORD BPB_RsvdSecCnt;
  BYTE BPB_NumFATS;
  WORD BPB_RootEntCnt;
  WORD BPB_TotSec16;
  BYTE BPB_Media;
  WORD BPB_FATSz16;
  WORD BPB_SecPerTrk;
  WORD BPB_NumHeads;
  DWORD BPB_HiddSec;
  DWORD BPB_TotSec32;
  BYTE BS_DrvNum;
  BYTE BS_Reserved1;
  BYTE BS_BootSig;
  DWORD BS_VollID;
  BYTE BS_VollLab[11];
  BYTE BS_FilSysType[8];
  BYTE Reserved2[448];
  WORD Signature_word;
} __attribute__ ((packed)) BPB_BS;

/* FAT Directory Structure */
typedef struct {
  BYTE DIR_Name[11];
  BYTE DIR_Attr;
  BYTE DIR_NTRes;
  BYTE DIR_CrtTimeTenth;
  WORD DIR_CrtTime;
  WORD DIR_CrtDate;
  WORD DIR_LstAccDate;
  WORD DIR_FstClusHI;
  WORD DIR_WrtTime;
  WORD DIR_WrtDate;
  WORD DIR_FstClusLO;
  DWORD DIR_FileSize;
} __attribute__ ((packed)) DIR_ENTRY;

/* FAT16 volume data with a file handler of the FAT16 image file */
typedef struct
{
  DWORD FirstRootDirSecNum;
  DWORD FirstDataSector;
  BPB_BS Bpb;
} FAT16;
typedef struct
{
    BYTE LDIR_Ord;
    BYTE LDIR_Name1[10];
    BYTE LDIR_Attr;
    BYTE LDIR_Type;
    BYTE LDIR_Chksum;
    BYTE LDIR_Name2[12];
    WORD LDIR_FstClusLO;
    BYTE LDIR_Name3[4];
}__attribute__ ((packed))LDIR_ENTRY;
#endif
