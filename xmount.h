/*******************************************************************************
* xmount Copyright (c) 2008-2012 by Gillen Daniel <gillen.dan@pinguin.lu>      *
*                                                                              *
* xmount is a small tool to "fuse mount" various image formats and enable      *
* virtual write access.                                                        *
*                                                                              *
* This program is free software: you can redistribute it and/or modify it      *
* under the terms of the GNU General Public License as published by the Free   *
* Software Foundation, either version 3 of the License, or (at your option)    *
* any later version.                                                           *
*                                                                              *
* This program is distributed in the hope that it will be useful, but WITHOUT  *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for     *
* more details.                                                                *
*                                                                              *
* You should have received a copy of the GNU General Public License along with *
* this program. If not, see <http://www.gnu.org/licenses/>.                    *
*******************************************************************************/

#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdarg.h>

#undef FALSE
#undef TRUE
#define FALSE 0
#define TRUE 1

#ifndef __APPLE__
  #define FOPEN fopen64
#else
  // Apple does use fopen for fopen64 too
  #define FOPEN fopen
#endif

/*
 * Constants
 */
#define IMAGE_INFO_HEADER "The following values have been extracted from " \
                          "the mounted image file:\n\n"

/*
 * Virtual image types
 */
typedef enum TVirtImageType {
  /** Virtual image is a DD file */
  TVirtImageType_DD,
  /** Virtual image is a DMG file */
  TVirtImageType_DMG,
  /** Virtual image is a VDI file */
  TVirtImageType_VDI,
  /** Virtual image is a VMDK file (IDE bus)*/
  TVirtImageType_VMDK,
  /** Virtual image is a VMDK file (SCSI bus)*/
  TVirtImageType_VMDKS,
  /** Virtual image is a VHD file*/
  TVirtImageType_VHD
} TVirtImageType;

/*
 * Input image types
 */
typedef enum TOrigImageType {
  /** Input image is a DD file */
  TOrigImageType_DD,
  /** Input image is an EWF file */
  TOrigImageType_EWF,
  /** Input image is an AFF file */
  TOrigImageType_AFF
} TOrigImageType;

/*
 * Various mountimg runtime options
 */
typedef struct TXMountConfData {
  /** Input image type */
  TOrigImageType OrigImageType;
  /** Virtual image type */
  TVirtImageType VirtImageType;
  /** Enable debug output */
  uint32_t Debug;
  /** Path of virtual image file */
  char *pVirtualImagePath;
  /** Path of virtual VMDK file */
  char *pVirtualVmdkPath;
  /** Path of virtual image info file */
  char *pVirtualImageInfoPath;
  /** Enable virtual write support */
  uint32_t Writable;
  /** Overwrite existing cache */
  uint32_t OverwriteCache;
  /** Cache file to save changes to */
  char *pCacheFile;
  /** Size of input image */
  uint64_t OrigImageSize;
  /** Size of virtual image */
  uint64_t VirtImageSize;
  /** Partial MD5 hash of input image */
  uint64_t InputHashLo;
  uint64_t InputHashHi;
} __attribute__ ((packed)) TXMountConfData;

/*
 * VDI Binary File Header structure
 */
#define VDI_FILE_COMMENT "<<< This is a virtual VDI image >>>"
#define VDI_HEADER_COMMENT "This VDI was emulated using xmount v" \
                           PACKAGE_VERSION
#define VDI_IMAGE_SIGNATURE 0xBEDA107F // 1:1 copy from hp
#define VDI_IMAGE_VERSION 0x00010001 // Vers 1.1
#define VDI_IMAGE_TYPE_FIXED 0x00000002 // Type 2 (fixed size)
#define VDI_IMAGE_FLAGS 0
#define VDI_IMAGE_BLOCK_SIZE (1024*1024) // 1 Megabyte
typedef struct TVdiFileHeader {
// ----- VDIPREHEADER ------
  /** Just text info about image type, for eyes only. */
  char szFileInfo[64];
  /** The image signature (VDI_IMAGE_SIGNATURE). */
  uint32_t u32Signature;
  /** The image version (VDI_IMAGE_VERSION). */
  uint32_t u32Version;
// ----- VDIHEADER1PLUS -----
  /** Size of header structure in bytes. */
  uint32_t cbHeader;
  /** The image type (VDI_IMAGE_TYPE_*). */
  uint32_t u32Type;
  /** Image flags (VDI_IMAGE_FLAGS_*). */
  uint32_t fFlags;
  /** Image comment. (UTF-8) */
  char szComment[256];
  /** Offset of Blocks array from the begining of image file.
   * Should be sector-aligned for HDD access optimization. */
  uint32_t offBlocks;
  /** Offset of image data from the begining of image file.
   * Should be sector-aligned for HDD access optimization. */
  uint32_t offData;
  /** Legacy image geometry (previous code stored PCHS there). */
  /** Cylinders. */
  uint32_t cCylinders;
  /** Heads. */
  uint32_t cHeads;
  /** Sectors per track. */
  uint32_t cSectors;
  /** Sector size. (bytes per sector) */
  uint32_t cbSector;
  /** Was BIOS HDD translation mode, now unused. */
  uint32_t u32Dummy;
  /** Size of disk (in bytes). */
  uint64_t cbDisk;
  /** Block size. (For instance VDI_IMAGE_BLOCK_SIZE.) Must be a power of 2! */
  uint32_t cbBlock;
  /** Size of additional service information of every data block.
   * Prepended before block data. May be 0.
   * Should be a power of 2 and sector-aligned for optimization reasons. */
  uint32_t cbBlockExtra;
  /** Number of blocks. */
  uint32_t cBlocks;
  /** Number of allocated blocks. */
  uint32_t cBlocksAllocated;
  /** UUID of image. */
  uint64_t uuidCreate_l;
  uint64_t uuidCreate_h;
  /** UUID of image's last modification. */
  uint64_t uuidModify_l;
  uint64_t uuidModify_h;
  /** Only for secondary images - UUID of previous image. */
  uint64_t uuidLinkage_l;
  uint64_t uuidLinkage_h;
  /** Only for secondary images - UUID of previous image's last modification. */
  uint64_t uuidParentModify_l;
  uint64_t uuidParentModify_h;
  /** Padding to get 512 byte alignment */
  uint64_t padding0;
  uint64_t padding1;
  uint64_t padding2;
  uint64_t padding3;
  uint64_t padding4;
  uint64_t padding5;
  uint64_t padding6;
} __attribute__ ((packed)) TVdiFileHeader, *pTVdiFileHeader;

//    /** The way the UUID is declared by the DCE specification. */
//    struct
//    {
//        uint32_t    u32TimeLow;
//        uint16_t    u16TimeMid;
//        uint16_t    u16TimeHiAndVersion;
//        uint8_t     u8ClockSeqHiAndReserved;
//        uint8_t     u8ClockSeqLow;
//        uint8_t     au8Node[6];
//    } Gen;

/*
 * VHD Binary File footer structure
 *
 * At the time of writing, the specs could be found here:
 *   http://www.microsoft.com/downloads/details.aspx?
 *     FamilyID=C2D03242-2FFB-48EF-A211-F0C44741109E
 *
 * Warning: All values are big-endian!
 */
// 
#ifdef __LP64__
  #define VHD_IMAGE_HVAL_COOKIE 0x78697463656E6F63 // "conectix"
#else
  #define VHD_IMAGE_HVAL_COOKIE 0x78697463656E6F63LL 
#endif
#define VHD_IMAGE_HVAL_FEATURES 0x02000000
#define VHD_IMAGE_HVAL_FILE_FORMAT_VERSION 0x00000100
#ifdef __LP64__
  #define VHD_IMAGE_HVAL_DATA_OFFSET 0xFFFFFFFFFFFFFFFF
#else
  #define VHD_IMAGE_HVAL_DATA_OFFSET 0xFFFFFFFFFFFFFFFFLL
#endif
#define VHD_IMAGE_HVAL_CREATOR_APPLICATION 0x746E6D78 // "xmnt"
#define VHD_IMAGE_HVAL_CREATOR_VERSION 0x00000500
// This one is funny! According to VHD specs, I can only choose between Windows
// and Macintosh. I'm going to choose the most common one.
#define VHD_IMAGE_HVAL_CREATOR_HOST_OS 0x6B326957 // "Win2k"
#define VHD_IMAGE_HVAL_DISK_TYPE 0x02000000
// Seconds from January 1st, 1970 to January 1st, 2000
#define VHD_IMAGE_TIME_CONVERSION_OFFSET 0x386D97E0
typedef struct TVhdFileHeader {
  uint64_t cookie;
  uint32_t features;
  uint32_t file_format_version;
  uint64_t data_offset;
  uint32_t creation_time;
  uint32_t creator_app;
  uint32_t creator_ver;
  uint32_t creator_os;
  uint64_t size_original;
  uint64_t size_current;
  uint16_t disk_geometry_c;
  uint8_t disk_geometry_h;
  uint8_t disk_geometry_s;
  uint32_t disk_type;
  uint32_t checksum;
  uint64_t uuid_l;
  uint64_t uuid_h;
  uint8_t saved_state;
  char Reserved[427];
} __attribute__ ((packed)) TVhdFileHeader, *pTVhdFileHeader;

/*
 * Cache file block index array element
 */
#ifdef __LP64__
  #define CACHE_BLOCK_FREE 0xFFFFFFFFFFFFFFFF
#else
  #define CACHE_BLOCK_FREE 0xFFFFFFFFFFFFFFFFLL 
#endif
typedef struct TCacheFileBlockIndex {
  /** Set to 1 if block is assigned (This block has data in cache file) */
  uint32_t Assigned;
  /** Offset to data in cache file */
  uint64_t off_data;
} __attribute__ ((packed)) TCacheFileBlockIndex, *pTCacheFileBlockIndex;

/*
 * Cache file header structures
 */
#define CACHE_BLOCK_SIZE (1024*1024) // 1 megabyte
#ifdef __LP64__
  #define CACHE_FILE_SIGNATURE 0xFFFF746E756F6D78 // "xmount\xFF\xFF"
#else
  #define CACHE_FILE_SIGNATURE 0xFFFF746E756F6D78LL 
#endif
#define CUR_CACHE_FILE_VERSION 0x00000002 // Current cache file version
#define HASH_AMOUNT (1024*1024)*10 // Amount of data used to construct a
                                   // "unique" hash for every input image
                                   // (10MByte)
// Current header
typedef struct TCacheFileHeader {
  /** Simple signature to identify cache files */
  uint64_t FileSignature;
  /** Cache file version */
  uint32_t CacheFileVersion;
  /** Cache block size */
  uint64_t BlockSize;
  /** Total amount of cache blocks */
  uint64_t BlockCount;
  /** Offset to the first block index array element */
  uint64_t pBlockIndex;
  /** Set to 1 if VDI file header is cached */
  uint32_t VdiFileHeaderCached;
  /** Offset to cached VDI file header */
  uint64_t pVdiFileHeader;
  /** Set to 1 if VMDK file is cached */
  uint32_t VmdkFileCached;
  /** Size of VMDK file */
  uint64_t VmdkFileSize;
  /** Offset to cached VMDK file */
  uint64_t pVmdkFile;
  
  /** Set to 1 if VHD header is cached */
  uint32_t VhdFileHeaderCached;
  /** Offset to cached VHD header */
  uint64_t pVhdFileHeader;
  
  /** Padding until offset 512 to ease further additions */
  char HeaderPadding[432];
} __attribute__ ((packed)) TCacheFileHeader, *pTCacheFileHeader;

// Old v1 header
typedef struct TCacheFileHeader_v1 {
  /** Simple signature to identify cache files */
  uint64_t FileSignature;
  /** Cache file version */
  uint32_t CacheFileVersion;
  /** Total amount of cache blocks */
  uint64_t BlockCount;
  /** Offset to the first block index array element */
  uint64_t pBlockIndex;
  /** Set to 1 if VDI file header is cached */
  uint32_t VdiFileHeaderCached;
  /** Offset to cached VDI file header */
  uint64_t pVdiFileHeader;
  /** Set to 1 if VMDK file is cached */
} TCacheFileHeader_v1, *pTCacheFileHeader_v1;

/*
 * Macros to ease debugging and error reporting
 */
#define LOG_ERROR(...) \
  LogMessage("ERROR",(char*)__FUNCTION__,__LINE__,__VA_ARGS__);
#define LOG_WARNING(...) \
  LogMessage("WARNING",(char*)__FUNCTION__,__LINE__,__VA_ARGS__);
#define LOG_DEBUG(...) { \
  if(XMountConfData.Debug) \
    LogMessage("DEBUG",(char*)__FUNCTION__,__LINE__,__VA_ARGS__); \
}

/*
 * Macros to alloc or realloc memory and check whether it worked
 */
#define XMOUNT_MALLOC(var,var_type,size) { \
  (var)=(var_type)malloc(size); \
  if((var)==NULL) { \
    LOG_ERROR("Couldn't allocate memmory!\n"); \
    exit(1); \
  } \
}
#define XMOUNT_REALLOC(var,var_type,size) { \
  (var)=(var_type)realloc((var),size); \
  if((var)==NULL) { \
    LOG_ERROR("Couldn't allocate memmory!\n"); \
    exit(1); \
  } \
}

/*
 * Macros for some often used string functions
 */
#define XMOUNT_STRSET(var1,var2) { \
  XMOUNT_MALLOC(var1,char*,strlen(var2)+1) \
  strcpy(var1,var2); \
}
#define XMOUNT_STRNSET(var1,var2,size) { \
  XMOUNT_MALLOC(var1,char*,(size)+1) \
  strncpy(var1,var2,size); \
  (var1)[size]='\0'; \
}
#define XMOUNT_STRAPP(var1,var2) { \
  XMOUNT_REALLOC(var1,char*,strlen(var1)+strlen(var2)+1) \
  strcpy((var1)+strlen(var1),var2); \
}
#define XMOUNT_STRNAPP(var1,var2,size) { \
  XMOUNT_REALLOC(var1,char*,strlen(var1)+(size)+1) \
  (var1)[strlen(var1)+(size)]='\0'; \
  strncpy((var1)+strlen(var1),var2,size); \
}

/*
 * Macros for endian conversions
 */
// First we need to have the bswap functions
#if HAVE_BYTESWAP_H
  #include <byteswap.h>
#elif defined(HAVE_ENDIAN_H)
  #include <endian.h>
#elif defined(__APPLE__)
  #include <libkern/OSByteOrder.h>
  #define bswap_16 OSSwapInt16
  #define bswap_32 OSSwapInt32
  #define bswap_64 OSSwapInt64
#else
  #define	bswap_16(value) {                    \
    ((((value) & 0xff) << 8) | ((value) >> 8)) \
  }
  #define	bswap_32(value)	{                                     \
 	  (((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
 	  (uint32_t)bswap_16((uint16_t)((value) >> 16)))              \
 	}
  #define	bswap_64(value)	{                                         \
 	  (((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) << 32) | \
 	  (uint64_t)bswap_32((uint32_t)((value) >> 32)))                  \
 	}
#endif
// Next we need to know what endianess is used
#if defined(__LITTLE_ENDIAN__)
  #define XMOUNT_BYTEORDER_LE
#elif defined(__BIG_ENDIAN__)
  #define XMOUNT_BYTEORDER_BE
#elif defined(__BYTE_ORDER__)
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define XMOUNT_BYTEORDER_LE
  #else
    #define XMOUNT_BYTEORDER_BE
  #endif
#endif
// And finally we can define the macros
#ifdef XMOUNT_BYTEORDER_LE
  #ifndef be16toh
    #define be16toh(x) bswap_16(x)
  #endif
  #ifndef htobe16
    #define htobe16(x) bswap_16(x)
  #endif
  #ifndef be32toh
    #define be32toh(x) bswap_32(x)
  #endif
  #ifndef htobe32
    #define htobe32(x) bswap_32(x)
  #endif
  #ifndef be64toh
    #define be64toh(x) bswap_64(x)
  #endif
  #ifndef htobe64
    #define htobe64(x) bswap_64(x)
  #endif
  #ifndef le16toh
    #define le16toh(x) (x)
  #endif
  #ifndef htole16
    #define htole16(x) (x)
  #endif
  #ifndef le32toh
    #define le32toh(x) (x)
  #endif
  #ifndef htole32
    #define htole32(x) (x)
  #endif
  #ifndef le64toh
    #define le64toh(x) (x)
  #endif
  #ifndef htole64
    #define htole64(x) (x)
  #endif
#else
  #ifndef be16toh
    #define be16toh(x) (x)
  #endif
  #ifndef htobe16
    #define htobe16(x) (x)
  #endif
  #ifndef be32toh
    #define be32toh(x) (x)
  #endif
  #ifndef htobe32
    #define htobe32(x) (x)
  #endif
  #ifndef be64toh
    #define be64toh(x) (x)
  #endif
  #ifndef htobe64
    #define htobe64(x) (x)
  #endif
  #ifndef le16toh
    #define le16toh(x) bswap_16(x)
  #endif
  #ifndef htole16
    #define htole16(x) bswap_16(x)
  #endif
  #ifndef le32toh
    #define le32toh(x) bswap_32(x)
  #endif
  #ifndef htole32
    #define htole32(x) bswap_32(x)
  #endif
  #ifndef le64toh
    #define le64toh(x) bswap_64(x)
  #endif
  #ifndef htole64
    #define htole64(x) bswap_64(x)
  #endif
#endif

/*
  ----- Change history -----
  20090226: * Added change history information to this file.
            * Added TVirtImageType enum to identify virtual image type.
            * Added TOrigImageType enum to identify input image type.
            * Added TMountimgConfData struct to hold various mountimg runtime
              options.
            * Renamed VDIFILEHEADER to TVdiFileHeader.
  20090228: * Added LOG_ERROR and LOG_DEBUG macros
            * Added defines for various static VDI header values
            * Added defines for TRUE and FALSE
  20090307: * Added defines for various static cache file header values
            * Added VdiFileHeaderCached and pVdiFileHeader values to be able to
              cache the VDI file header separatly.
  20090519: * Added new cache file header structure and moved old one to
              TCacheFileHeader_v1.
            * New cache file structure includes VmdkFileCached and pVmdkFile to
              cache virtual VMDK file and makes room for further additions so
              current cache file version 2 cache files can be easily converted
              to newer ones.
  20090814: * Added XMOUNT_MALLOC and XMOUNT_REALLOC macros.
  20090816: * Added XMOUNT_STRSET, XMOUNT_STRNSET, XMOUNT_STRAPP and
              XMOUNT_STRNAPP macros.
  20100324: * Added "__attribute__ ((packed))" to all header structs to prevent
              different sizes on i386 and amd64.
  20111109: * Added TVirtImageType_DMG type.
  20120130: * Added LOG_WARNING macro.
  20120507: * Added TVhdFileHeader structure.
  20120511: * Added endianess conversation macros
*/
