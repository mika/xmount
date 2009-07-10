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

/*
 * Define CACHE_BLOCK_SIZE to be 512KB and BLOCK_MAP_ENTRYS to 1310720.
 * This allows cache objects of max 5TB.
 */
#ifdef __LP64__
  #define CACHE_BLOCK_SIZE 524288
  #define BLOCK_MAP_ENTRYS 1310720
#else
  #define CACHE_BLOCK_SIZE 524288LL
  #define BLOCK_MAP_ENTRYS 1310720LL
#endif

/*
 * The block map of a cache object.
 */
typedef uint64_t TBlockMap;

/*
 * Different cache object types
 */
typedef enum TCacheObjectType {
  TCacheObjectType_PublicDyn,  // A public object with dynamic size
  TCacheObjectType_PublicFix,  // A public object with fixed size
  TCacheObjectType_Private     // A private object for internal use
} TCacheObjectType;

typedef struct TCacheObjectInfo {
  uint64_t ObjectSize;
}

/*
 * A cache object
 */
typedef struct TCacheObject {
  uint64_t ObjectId;       // Object identifier
  TObjectType ObjectType;  // Object type
  uint64_t offBlockMap;    // Offset to block map
} TCacheObject;

/*
 * The cache file
 */
typedef struct TCacheFile {
  uint64_t CacheBlockSize;
  TCacheObject *pCacheObjects;
} TCacheFile;

/*
 * Public cache functions
 */
int OpenCache(TCacheFile *pCacheFile,
              char *pFile);
int CloseCache(TCacheFile *pCacheFile);
int CreateCacheObject(TCacheFile *pCacheFile,
                      uint64_t ObjectId,
                      TCacheObjectType ObjectType,
                      uint64_t ObjectSize=0);
int DestroyCacheObject(TCacheFile *pCacheFile,
                       uint64_t ObjectId);
int GetCacheObjectInfo(TCacheFile *pCacheFile,
                       uint64_t ObjectId,
                       TCacheObjectInfo *pObjectInfo);
int GetCacheObjectData(TCacheFile *pCacheFile,
                       uint64_t ObjectId,
                       char *buf,
                       uint64_t offset,
                       uint64_t size);
int SetCacheObjectData(TCacheFile *pCacheFile,
                       uint64_t ObjectId,
                       char *buf,
                       uint64_t offset,
                       uint64_t size);

/*
  ----- Change history -----
  20090413: 
