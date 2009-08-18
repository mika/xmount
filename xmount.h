/*******************************************************************************
* xmount Copyright (c) 2008,2009 by Gillen Daniel <gillen.dan@pinguin.lu>      *
*                                                                              *
* xmount is a small tool to "fuse mount" various image formats as dd or vdi    *
* files and enable virtual write access.                                       *
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

/*
 * Virtual image types
 */
typedef enum TVirtImageType {
  /** Virtual image is a DD file */
  TVirtImageType_DD,
  /** Virtual image is a VDI file */
  TVirtImageType_VDI,
  /** Virtual image is a VMDK file (IDE bus)*/
  TVirtImageType_VMDK,
  /** Virtual image is a VMDK file (SCSI bus)*/
  TVirtImageType_VMDKS
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
} TXMountConfData;

/*
 * VDI Binary File Header structure
 */
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
  /** Block size. (For instance VDI_IMAGE_BLOCK_SIZE.) Should be a power of 2! */
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
} TVdiFileHeader, *pTVdiFileHeader;

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
 * Cache file block index array element
 */
typedef struct TCacheFileBlockIndex {
  /** Set to 1 if block is assigned (This block has data in cache file) */
  uint32_t Assigned;
  /** Offset to data in cache file */
  uint64_t off_data;
} TCacheFileBlockIndex, *pTCacheFileBlockIndex;

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
  /** Padding until offset 512 to ease further additions */
  char HeaderPadding[444];
} TCacheFileHeader, *pTCacheFileHeader;

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
 * Some macros to ease debugging and error reporting
 */
#define LOG_ERROR(...) \
  LogMessage("ERROR",(char*)__FUNCTION__,__LINE__,__VA_ARGS__);
#define LOG_DEBUG(...) { \
  if(XMountConfData.Debug) \
    LogMessage("DEBUG",(char*)__FUNCTION__,__LINE__,__VA_ARGS__); \
}

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
*/
