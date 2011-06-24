/*******************************************************************************
* xmount Copyright (c) 2008-2011 by Gillen Daniel <gillen.dan@pinguin.lu>      *
*                                                                              *
* xmount is a small tool to "fuse mount" various harddisk image formats as dd, *
* vdi or vmdk files and enable virtual write access to them.                   *
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

#undef HAVE_LIBAFF_STATIC
#undef HAVE_LIBEWF_STATIC

#include "config.h"

#ifdef HAVE_LIBEWF
  #define WITH_LIBEWF
#endif
#ifdef HAVE_LIBEWF_STATIC
  #define WITH_LIBEWF
#endif

#ifdef HAVE_LIBAFFLIB
  #define WITH_LIBAFF
#endif
#ifdef HAVE_LIBAFF_STATIC
  #define WITH_LIBAFF
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <pthread.h>
#include <stdint.h>
#ifdef HAVE_LIBEWF
  #include <libewf.h>
#endif
#ifdef HAVE_LIBEWF_STATIC
  #include "libewf/include/libewf.h"
#endif
#ifdef HAVE_LIBAFFLIB
  #include <afflib/afflib.h>
#endif
#ifdef HAVE_LIBAFF_STATIC
  #include "libaff/lib/afflib.h"
#endif
#include "xmount.h"
#include "md5.h"

// Some constant values
#define IMAGE_INFO_HEADER "The following values have been extracted from " \
                          "the mounted image file:\n\n"
#define VDI_FILE_COMMENT "<<< This is a virtual VDI image >>>"
#define VDI_HEADER_COMMENT "This VDI was emulated using xmount v" \
                           PACKAGE_VERSION

// Struct that contains various runtime configuration options
static TXMountConfData XMountConfData;
// Handles for input image types
static FILE *hDdFile=NULL;
#ifdef WITH_LIBEWF
  static LIBEWF_HANDLE *hEwfFile=NULL;
#endif
#ifdef WITH_LIBAFF
  static AFFILE *hAffFile=NULL;
#endif
// Pointer to virtual info file
static char *pVirtualImageInfoFile=NULL;
// Vars needed for VDI emulation
static TVdiFileHeader *pVdiFileHeader=NULL;
static uint32_t VdiFileHeaderSize=0;
static char *pVdiBlockMap=NULL;
static uint32_t VdiBlockMapSize=0;
// Vars needed for VMDK emulation
static char *pVirtualVmdkFile=NULL;
static int VirtualVmdkFileSize=0;
static char *pVirtualVmdkLockDir=NULL;
static char *pVirtualVmdkLockDir2=NULL;
static char *pVirtualVmdkLockFileData=NULL;
static int VirtualVmdkLockFileDataSize=0;
static char *pVirtualVmdkLockFileName=NULL;
// Vars needed for virtual write access
static FILE *hCacheFile=NULL;
static pTCacheFileHeader pCacheFileHeader=NULL;
static pTCacheFileBlockIndex pCacheFileBlockIndex=NULL;
// Mutexes to control concurrent read & write access
static pthread_mutex_t mutex_image_rw;
static pthread_mutex_t mutex_info_read;

/*
 * LogMessage:
 *   Print error and debug messages to stdout
 *
 * Params:
 *  pMessageType: "ERROR" or "DEBUG"
 *  pCallingFunction: Name of calling function
 *  line: Line number of call
 *  pMessage: Message string
 *  ...: Variable params with values to include in message string
 *
 * Returns:
 *   n/a
 */
static void LogMessage(char *pMessageType,
                       char *pCallingFunction,
                       int line,
                       char *pMessage,
                       ...)
{
  va_list VaList;

  // Print message "header"
  printf("%s: %s.%s@%u : ",pMessageType,pCallingFunction,PACKAGE_VERSION,line);
  // Print message with variable parameters
  va_start(VaList,pMessage);
  vprintf(pMessage,VaList);
  va_end(VaList);
}

/*
 * LogWarnMessage:
 *   Print warning messages to stdout
 *
 * Params:
 *  pMessage: Message string
 *  ...: Variable params with values to include in message string
 *
 * Returns:
 *   n/a
 */
static void LogWarnMessage(char *pMessage,...) {
  va_list VaList;

  // Print message "header"
  printf("WARNING: ");
  // Print message with variable parameters
  va_start(VaList,pMessage);
  vprintf(pMessage,VaList);
  va_end(VaList);
}

/*
 * PrintUsage:
 *   Print usage instructions (cmdline options etc..)
 *
 * Params:
 *   pProgramName: Program name (argv[0])
 *
 * Returns:
 *   n/a
 */
static void PrintUsage(char *pProgramName) {
  printf("\nxmount v%s copyright (c) 2008-2011 by Gillen Daniel "
         "<gillen.dan@pinguin.lu>\n",PACKAGE_VERSION);
  printf("\nUsage:\n");
  printf("  %s [[fopts] [mopts]] <ifile> [<ifile> [...]] <mntp>\n\n",pProgramName);
  printf("Options:\n");
  printf("  fopts:\n");
  printf("    -d : Enable FUSE's and xmount's debug mode.\n");
  printf("    -h : Display this help message.\n");
  printf("    -s : Run single threaded.\n");
  printf("    -o no_allow_other : Disable automatic addition of FUSE's allow_other option.\n");
  printf("    -o <fmopts> : Specify fuse mount options. Will also disable automatic\n");
  printf("                  addition of FUSE's allow_other option!\n");
  printf("    INFO: For VMDK emulation, you have to uncomment \"user_allow_other\" in\n");
  printf("          /etc/fuse.conf or run xmount as root.\n");
  printf("  mopts:\n");
  printf("    --cache <file> : Enable virtual write support and set cachefile to use.\n");
//  printf("    --debug : Enable xmount's debug mode.\n");
  printf("    --in <itype> : Input image format. <itype> can be \"dd\"");
#ifdef WITH_LIBEWF
  printf(", \"ewf\"");
#endif
#ifdef WITH_LIBAFF
  printf(", \"aff\"");
#endif
  printf(".\n");
  printf("    --info : Print out some infos about used compiler and libraries.\n");
  printf("    --out <otype> : Output image format. <otype> can be \"dd\", \"vdi\", \"vmdk(s)\".\n");
  printf("    --owcache <file> : Same as --cache <file> but overwrites existing cache.\n");
  printf("    --rw <file> : Same as --cache <file>.\n");
  printf("    --version : Same as --info.\n");
  printf("    INFO: Input and output image type defaults to \"dd\" if not specified.\n");
  printf("    WARNING: Output image type \"vmdk(s)\" should be considered experimental!\n");
  printf("  ifile:\n");
  printf("    Input image file.");
#ifdef WITH_LIBEWF
  printf(" If you use EWF files, you have to specify all image\n");
  printf("    segments! (If your shell supports it, you can use .E?? as file extension\n");
  printf("    to specify them all)\n");
#else
  printf("\n");
#endif
  printf("  mntp:\n");
  printf("    Mount point where virtual files should be located.\n");
}

/*
 * CheckFuseAllowOther:
 *   Check if FUSE allows us to pass the -o allow_other parameter.
 *   This only works if we are root or user_allow_other is set in
 *   /etc/fuse.conf.
 *
 * Params:
 *   n/a
 *
 * Returns:
 *   TRUE on success, FALSE on error
 */
static int CheckFuseAllowOther() {
  if(geteuid()!=0) {
    // Not running xmount as root. Try to read FUSE's config file /etc/fuse.conf
    FILE *hFuseConf=(FILE*)FOPEN("/etc/fuse.conf","r");
    if(hFuseConf==NULL) {
      LogWarnMessage("FUSE will not allow other users nor root to access your "
                     "virtual harddisk image. To change this behavior, please "
                     "add \"user_allow_other\" to /etc/fuse.conf or execute "
                     "xmount as root.\n");
      return FALSE;
    }
    // Search conf file for set user_allow_others
    char line[256];
    int PermSet=FALSE;
    while(fgets(line,sizeof(line),hFuseConf)!=NULL && PermSet!=TRUE) {
      // TODO: This works as long as there is no other parameter beginning with
      // "user_allow_other" :)
      if(strncmp(line,"user_allow_other",strlen("user_allow_other"))==0) {
        PermSet=TRUE;
      }
    }
    fclose(hFuseConf);
    if(PermSet==FALSE) {
      LogWarnMessage("FUSE will not allow other users nor root to access your "
                     "virtual harddisk image. To change this behavior, please "
                     "add \"user_allow_other\" to /etc/fuse.conf or execute "
                     "xmount as root.\n");
      return FALSE;
    }
  }
  // Running xmount as root or user_allow_other is set in /etc/fuse.conf
  return TRUE;
}

/*
 * ParseCmdLine:
 *   Parse command line options
 *
 * Params:
 *   argc: Number of cmdline params
 *   argv: Array containing cmdline params
 *   pNargv: Number of FUSE options is written to this var
 *   pppNargv: FUSE options are written to this array
 *   pFilenameCount: Number of input image files is written to this var
 *   pppFilenames: Input image filenames are written to this array
 *   ppMountpoint: Mountpoint is written to this var
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int ParseCmdLine(const int argc,
                        char **argv,
                        int *pNargc,
                        char ***pppNargv,
                        int *pFilenameCount,
                        char ***pppFilenames,
                        char **ppMountpoint) {
  int i=1,files=0,opts=0,FuseMinusOControl=TRUE,FuseAllowOther=TRUE;

  // add argv[0] to pppNargv
  opts++;
  XMOUNT_MALLOC(*pppNargv,char**,opts*sizeof(char*))
  XMOUNT_STRSET((*pppNargv)[opts-1],argv[0])

  // Parse options
  while(i<argc && *argv[i]=='-') {
    if(strlen(argv[i])>1 && *(argv[i]+1)!='-') {
      // Options beginning with - are mostly FUSE specific
      if(strcmp(argv[i],"-d")==0) {
        // Enable FUSE's and xmount's debug mode
        opts++;
        XMOUNT_REALLOC(*pppNargv,char**,opts*sizeof(char*))
        XMOUNT_STRSET((*pppNargv)[opts-1],argv[i])
        XMountConfData.Debug=TRUE;
      } else if(strcmp(argv[i],"-h")==0) {
        // Print help message
        PrintUsage(argv[0]);
        exit(1);
      } else if(strcmp(argv[i],"-o")==0) {
        // Next parameter specifies fuse mount options
        if((argc+1)>i) {
          i++;
          // As the user specified the -o option, we assume he knows what he is
          // doing. We won't append allow_other automatically. And we allow him
          // to disable allow_other by passing a single "-o no_allow_other"
          // which won't be passed to FUSE as it is xmount specific.
          if(strcmp(argv[i],"no_allow_other")!=0) {
            opts+=2;
            XMOUNT_REALLOC(*pppNargv,char**,opts*sizeof(char*))
            XMOUNT_STRSET((*pppNargv)[opts-2],argv[i-1])
            XMOUNT_STRSET((*pppNargv)[opts-1],argv[i])
            FuseMinusOControl=FALSE;
          } else FuseAllowOther=FALSE;
        } else {
          LOG_ERROR("Couldn't parse mount options!\n")
          PrintUsage(argv[0]);
          exit(1);
        }
      } else if(strcmp(argv[i],"-s")==0) {
        // Enable FUSE's single threaded mode
        opts++;
        XMOUNT_REALLOC(*pppNargv,char**,opts*sizeof(char*))
        XMOUNT_STRSET((*pppNargv)[opts-1],argv[i])
      } else if(strcmp(argv[i],"-V")==0) {
        // Display FUSE version info
        opts++;
        XMOUNT_REALLOC(*pppNargv,char**,opts*sizeof(char*))
        XMOUNT_STRSET((*pppNargv)[opts-1],argv[i])
      } else {
        LOG_ERROR("Unknown command line option \"%s\"\n",argv[i]);
        PrintUsage(argv[0]);
        exit(1);
      }
    } else {
      // Options beginning with -- are xmount specific
      if(strcmp(argv[i],"--cache")==0 || strcmp(argv[i],"--rw")==0) {
        // Emulate writable access to mounted image
        // Next parameter must be cache file to read/write changes from/to
        if((argc+1)>i) {
          i++;
          XMOUNT_STRSET(XMountConfData.pCacheFile,argv[i])
          XMountConfData.Writable=TRUE;
        } else {
          LOG_ERROR("You must specify a cache file to read/write data from/to!\n")
          PrintUsage(argv[0]);
          exit(1);
        }
        LOG_DEBUG("Enabling virtual write support using cache file \"%s\"\n",
                  XMountConfData.pCacheFile)
      } else if(strcmp(argv[i],"--in")==0) {
        // Specify input image type
        // Next parameter must be image type
        if((argc+1)>i) {
          i++;
          if(strcmp(argv[i],"dd")==0) {
            XMountConfData.OrigImageType=TOrigImageType_DD;
            LOG_DEBUG("Setting input image type to DD\n")
#ifdef WITH_LIBEWF
          } else if(strcmp(argv[i],"ewf")==0) {
            XMountConfData.OrigImageType=TOrigImageType_EWF;
            LOG_DEBUG("Setting input image type to EWF\n")
#endif
#ifdef WITH_LIBAFF
          } else if(strcmp(argv[i],"aff")==0) {
            XMountConfData.OrigImageType=TOrigImageType_AFF;
            LOG_DEBUG("Setting input image type to AFF\n")
#endif
          } else {
            LOG_ERROR("Unknown input image type \"%s\"!\n",argv[i])
            PrintUsage(argv[0]);
            exit(1);
          }
        } else {
          LOG_ERROR("You must specify an input image type!\n");
          PrintUsage(argv[0]);
          exit(1);
        }
      } else if(strcmp(argv[i],"--out")==0) {
        // Specify output image type
        // Next parameter must be image type
        if((argc+1)>i) {
          i++;
          if(strcmp(argv[i],"dd")==0) {
            XMountConfData.VirtImageType=TVirtImageType_DD;
            LOG_DEBUG("Setting virtual image type to DD\n")
          } else if(strcmp(argv[i],"vdi")==0) {
            XMountConfData.VirtImageType=TVirtImageType_VDI;
            LOG_DEBUG("Setting virtual image type to VDI\n")
          } else if(strcmp(argv[i],"vmdk")==0) {
            XMountConfData.VirtImageType=TVirtImageType_VMDK;
            LOG_DEBUG("Setting virtual image type to VMDK\n")
          } else if(strcmp(argv[i],"vmdks")==0) {
            XMountConfData.VirtImageType=TVirtImageType_VMDKS;
            LOG_DEBUG("Setting virtual image type to VMDKS\n")
          } else {
            LOG_ERROR("Unknown output image type \"%s\"!\n",argv[i])
            PrintUsage(argv[0]);
            exit(1);
          }
        } else {
          LOG_ERROR("You must specify an output image type!\n");
          PrintUsage(argv[0]);
          exit(1);
        }
      } else if(strcmp(argv[i],"--owcache")==0) {
        // Enable writable access to mounted image and overwrite existing cache
        // Next parameter must be cache file to read/write changes from/to
        if((argc+1)>i) {
          i++;
          XMOUNT_STRSET(XMountConfData.pCacheFile,argv[i])
          XMountConfData.Writable=TRUE;
          XMountConfData.OverwriteCache=TRUE;
        } else {
          LOG_ERROR("You must specify a cache file to read/write data from/to!\n")
          PrintUsage(argv[0]);
          exit(1);
        }
        LOG_DEBUG("Enabling virtual write support overwriting cache file \"%s\"\n",
                  XMountConfData.pCacheFile)
      } else if(strcmp(argv[i],"--version")==0 || strcmp(argv[i],"--info")==0) {
        printf("xmount v%s copyright (c) 2008-2011 by Gillen Daniel "
               "<gillen.dan@pinguin.lu>\n\n",PACKAGE_VERSION);
#ifdef __GNUC__
        printf("  compile timestamp: %s %s\n",__DATE__,__TIME__);
        printf("  gcc version: %s\n",__VERSION__);
#endif
#ifdef WITH_LIBEWF
        printf("  libewf support: YES (version %s)\n",LIBEWF_VERSION_STRING);
#else
        printf("  libewf support: NO\n");
#endif
#ifdef WITH_LIBAFF
        printf("  libaff support: YES (version %s)\n",af_version());
#else
        printf("  libaff support: NO\n");
#endif
        printf("\n");
        exit(0);
      } else {
        LOG_ERROR("Unknown command line option \"%s\"\n",argv[i]);
        PrintUsage(argv[0]);
        exit(1);
      }
    }
    i++;
  }
  
  // Parse input image filename(s)
  while(i<(argc-1)) {
    files++;
    XMOUNT_REALLOC(*pppFilenames,char**,files*sizeof(char*))
    XMOUNT_STRSET((*pppFilenames)[files-1],argv[i])
    i++;
  }
  if(files==0) {
    LOG_ERROR("No input files specified!\n")
    PrintUsage(argv[0]);
    exit(1);
  }
  *pFilenameCount=files;

  // Extract mountpoint
  if(i==(argc-1)) {
    XMOUNT_STRSET(*ppMountpoint,argv[argc-1])
    opts++;
    XMOUNT_REALLOC(*pppNargv,char**,opts*sizeof(char*))
    XMOUNT_STRSET((*pppNargv)[opts-1],*ppMountpoint)
  } else {
    LOG_ERROR("No mountpoint specified!\n")
    PrintUsage(argv[0]);
    exit(1);
  }

  if(FuseMinusOControl==TRUE) {
    // We control the -o flag, set subtype, fsname and allow_other options
    opts+=2;
    XMOUNT_REALLOC(*pppNargv,char**,opts*sizeof(char*))
    XMOUNT_STRSET((*pppNargv)[opts-2],"-o")
    XMOUNT_STRSET((*pppNargv)[opts-1],"subtype=xmount,fsname=")
    XMOUNT_STRAPP((*pppNargv)[opts-1],(*pppFilenames)[0])
    if(FuseAllowOther==TRUE) {
      // Try to add "allow_other" to FUSE's cmd-line params
      if(CheckFuseAllowOther()==TRUE) {
        XMOUNT_STRAPP((*pppNargv)[opts-1],",allow_other")
      }
    }
  }

  *pNargc=opts;

  return TRUE;
}

/*
 * ExtractVirtFileNames:
 *   Extract virtual file name from input image name
 *
 * Params:
 *   pOrigName: Name of input image (Can include a path)
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int ExtractVirtFileNames(char *pOrigName) {
  char *tmp;

  // Truncate any leading path
  tmp=strrchr(pOrigName,'/');
  if(tmp!=NULL) pOrigName=tmp+1;

  // Extract file extension
  tmp=strrchr(pOrigName,'.');

  // Set leading '/'
  XMOUNT_STRSET(XMountConfData.pVirtualImagePath,"/")
  XMOUNT_STRSET(XMountConfData.pVirtualImageInfoPath,"/")
  if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
     XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    XMOUNT_STRSET(XMountConfData.pVirtualVmdkPath,"/")
  }

  // Copy filename
  if(tmp==NULL) {
    // Input image filename has no extension
    XMOUNT_STRAPP(XMountConfData.pVirtualImagePath,pOrigName)
    XMOUNT_STRAPP(XMountConfData.pVirtualImageInfoPath,pOrigName)
    if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
       XMountConfData.VirtImageType==TVirtImageType_VMDKS)
    {
      XMOUNT_STRAPP(XMountConfData.pVirtualVmdkPath,pOrigName)
    }
    XMOUNT_STRAPP(XMountConfData.pVirtualImageInfoPath,".info")
  } else {
    XMOUNT_STRNAPP(XMountConfData.pVirtualImagePath,pOrigName,
                   strlen(pOrigName)-strlen(tmp))
    XMOUNT_STRNAPP(XMountConfData.pVirtualImageInfoPath,pOrigName,
                   strlen(pOrigName)-strlen(tmp))
    if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
       XMountConfData.VirtImageType==TVirtImageType_VMDKS)
    {
      XMOUNT_STRNAPP(XMountConfData.pVirtualVmdkPath,pOrigName,
                     strlen(pOrigName)-strlen(tmp))
    }
    XMOUNT_STRAPP(XMountConfData.pVirtualImageInfoPath,".info")
  }

  // Add virtual file extensions
  switch(XMountConfData.VirtImageType) {
    case TVirtImageType_DD:
      XMOUNT_STRAPP(XMountConfData.pVirtualImagePath,".dd")
      break;
    case TVirtImageType_VDI:
      XMOUNT_STRAPP(XMountConfData.pVirtualImagePath,".vdi")
      break;
    case TVirtImageType_VMDK:
    case TVirtImageType_VMDKS:
      XMOUNT_STRAPP(XMountConfData.pVirtualImagePath,".dd")
      XMOUNT_STRAPP(XMountConfData.pVirtualVmdkPath,".vmdk")
      break;
    default:
      LOG_ERROR("Unknown virtual image type!\n")
      return FALSE;
  }

  LOG_DEBUG("Set virtual image name to \"%s\"\n",
            XMountConfData.pVirtualImagePath)
  LOG_DEBUG("Set virtual image info name to \"%s\"\n",
            XMountConfData.pVirtualImageInfoPath)
  if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
     XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    LOG_DEBUG("Set virtual vmdk name to \"%s\"\n",
              XMountConfData.pVirtualVmdkPath)
  }
  return TRUE;
}

/*
 * GetOrigImageSize:
 *   Get size of original image
 *
 * Params:
 *   size: Pointer to an uint64_t to which the size will be written to
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int GetOrigImageSize(uint64_t *size) {
  // Make sure to return correct values when dealing with only 32bit file sizes
  *size=0;

  // When size was already queryed, use old value rather than regetting value
  // from disk
  if(XMountConfData.OrigImageSize!=0) {
    *size=XMountConfData.OrigImageSize;
    return TRUE;
  }

  // Now get size of original image
  switch(XMountConfData.OrigImageType) {
    case TOrigImageType_DD:
      // Original image is a DD file. Seek to end to get size.
      if(fseeko(hDdFile,0,SEEK_END)!=0) {
        LOG_ERROR("Couldn't seek to end of image file!\n")
        return FALSE;
      }
      *size=ftello(hDdFile);
      break;
#ifdef WITH_LIBEWF
    case TOrigImageType_EWF:
      // Original image is an EWF file. Just query media size.
      if(libewf_get_media_size(hEwfFile,size)!=1) {
        LOG_ERROR("Couldn't get ewf media size!\n")
        return FALSE;
      }
      break;
#endif
#ifdef WITH_LIBAFF
    case TOrigImageType_AFF:
      // Original image is an AFF file.
      *size=af_seek(hAffFile,0,SEEK_END);
      break;
#endif
    default:
      LOG_ERROR("Unsupported image type!\n")
      return FALSE;
  }
  // Save size so we have not to reget it from disk next time
  XMountConfData.OrigImageSize=*size;
  return TRUE;
}

/*
 * GetVirtImageSize:
 *   Get size of the emulated image
 *
 * Params:
 *   size: Pointer to an uint64_t to which the size will be written to
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int GetVirtImageSize(uint64_t *size) {
  if(XMountConfData.VirtImageSize!=0) {
    *size=XMountConfData.VirtImageSize;
    return TRUE;
  }

  switch(XMountConfData.VirtImageType) {
    case TVirtImageType_VMDK:
    case TVirtImageType_VMDKS:
    case TVirtImageType_DD:
      // Virtual image is a DD or VMDK file. Just return the size of the
      // original image
      if(!GetOrigImageSize(size)) {
        LOG_ERROR("Couldn't get size of input image!\n")
        return FALSE;
      }
      break;
    case TVirtImageType_VDI:
      // Virtual image is a VDI file. Get size of original image and add size
      // of VDI header etc.
      if(!GetOrigImageSize(size)) {
        LOG_ERROR("Couldn't get size of input image!\n")
        return FALSE;
      }
      (*size)+=(sizeof(TVdiFileHeader)+VdiBlockMapSize);
      break;
    default:
      LOG_ERROR("Unsupported image type!\n")
      return FALSE;
  }
  XMountConfData.VirtImageSize=*size;
  return TRUE;
}

/*
 * GetOrigImageData:
 *   Read data from original image
 *
 * Params:
 *   buf: Pointer to buffer to write read data to (Must be preallocated!)
 *   offset: Offset at which data should be read
 *   size: Size of data which should be read (Size of buffer)
 *
 * Returns:
 *   Number of read bytes on success or "-1" on error
 */
static int GetOrigImageData(char *buf, off_t offset, size_t size) {
  size_t ToRead=0;
  uint64_t ImageSize=0;

  // Make sure we aren't reading past EOF of image file
  if(!GetOrigImageSize(&ImageSize)) {
    LOG_ERROR("Couldn't get image size!\n")
    return -1;
  }
  if(offset>=ImageSize) {
    // Offset is beyond image size
    LOG_DEBUG("Offset is beyond image size.\n")
    return 0;
  }
  if(offset+size>ImageSize) {
    // Attempt to read data past EOF of image file
    ToRead=ImageSize-offset;
    LOG_DEBUG("Attempt to read data past EOF. Corrected size from %zd"
              " to %zd.\n",size,ToRead)
  } else ToRead=size;

  // Now read data from image file
  switch(XMountConfData.OrigImageType) {
    case TOrigImageType_DD:
      // Original image is a DD file. Seek to offset and read ToRead bytes.
      // TODO: Perhaps check whether it is cheaper to seek from current position
      // to offset than seeking from beginning of the file
      if(fseeko(hDdFile,offset,SEEK_SET)!=0) {
        LOG_ERROR("Couldn't seek to offset %" PRIu64 "!\n",offset)
        return -1;
      }
      if(fread(buf,ToRead,1,hDdFile)!=1) {
        LOG_ERROR("Couldn't read %zd bytes from offset %" PRIu64
                  "!\n",ToRead,offset)
        return -1;
      }
      LOG_DEBUG("Read %zd bytes at offset %" PRIu64 " from DD file\n",
                ToRead,offset)
      break;
#ifdef WITH_LIBEWF
    case TOrigImageType_EWF:
      // Original image is an EWF file. Seek to offset and read ToRead bytes.
      if(libewf_seek_offset(hEwfFile,offset)!=-1) {
        if(libewf_read_buffer(hEwfFile,buf,ToRead)!=ToRead) {
          LOG_ERROR("Couldn't read %zd bytes from offset %" PRIu64
                    "!\n",ToRead,offset)
          return -1;
        }
      } else {
        LOG_ERROR("Couldn't seek to offset %" PRIu64 "!\n",offset)
        return -1;
      }
      LOG_DEBUG("Read %zd bytes at offset %" PRIu64 " from EWF file\n",
                ToRead,offset)
      break;
#endif
#ifdef WITH_LIBAFF
    case TOrigImageType_AFF:
      // Original image is an AFF file.
      af_seek(hAffFile,offset,SEEK_SET);
      if(af_read(hAffFile,buf,ToRead)!=ToRead) {
        LOG_ERROR("Couldn't read %zd bytes from offset %" PRIu64
                  "!\n",ToRead,offset)
        return -1;
      }
      LOG_DEBUG("Read %zd bytes at offset %" PRIu64 " from AFF file\n",
                ToRead,offset)
      break;
#endif
    default:
      LOG_ERROR("Unsupported image type!\n")
      return -1;
  }
  return ToRead;
}

/*
 * GetVirtVmdkData:
 *   Read data from virtual VMDK file
 *
 * Params:
 *   buf: Pointer to buffer to write read data to (Must be preallocated!)
 *   offset: Offset at which data should be read
 *   size: Size of data which should be read (Size of buffer)
 *
 * Returns:
 *   Number of read bytes on success or "-1" on error
 */
 /*
static int GetVirtualVmdkData(char *buf, off_t offset, size_t size) {
  uint32_t len;

  len=strlen(pVirtualVmdkFile);
  if(offset<len) {
    if(offset+size>len) {
      size=len-offset;
      LOG_DEBUG("Attempt to read past EOF of virtual vmdk file\n")
    }
    if(XMountConfData.Writable==TRUE &&
       pCacheFileHeader->VmdkFileCached==TRUE)
    {
      // VMDK file is cached. Read data from cache file
      // TODO: Read data from cache file
    } else {
      // No write support or VMDK file not cached.
      memcpy(buf,pVirtualVmdkFile+offset,size);
      LOG_DEBUG("Read %" PRIu64 " bytes at offset %" PRIu64
                " from virtual vmdk file\n",size,offset)
    }
  } else {
    LOG_DEBUG("Attempt to read past EOF of virtual vmdk file\n");
    return -1;
  }
  return size;
}
*/

/*
 * GetVirtImageData:
 *   Read data from virtual image
 *
 * Params:
 *   buf: Pointer to buffer to write read data to (Must be preallocated!)
 *   offset: Offset at which data should be read
 *   size: Size of data which should be read (Size of buffer)
 *
 * Returns:
 *   Number of read bytes on success or "-1" on error
 */
static int GetVirtImageData(char *buf, off_t offset, size_t size) {
  uint32_t CurBlock=0;
  uint64_t VirtImageSize;
  size_t ToRead=0;
  size_t CurToRead=0;
  off_t FileOff=offset;
  off_t BlockOff=0;

  // Get virtual image size
  if(!GetVirtImageSize(&VirtImageSize)) {
    LOG_ERROR("Couldn't get virtual image size!\n")
    return -1;
  }

  if(offset>=VirtImageSize) {
    LOG_ERROR("Attempt to read beyond virtual image EOF!\n")
    return -1;
  }

  if(offset+size>VirtImageSize) {
    LOG_DEBUG("Attempt to read pas EOF of virtual image file\n")
    size=VirtImageSize-offset;
  }

  ToRead=size;

  // Read virtual image type specific data
  switch(XMountConfData.VirtImageType) {
    case TVirtImageType_DD:
    case TVirtImageType_VMDK:
    case TVirtImageType_VMDKS:
      break;
    case TVirtImageType_VDI:
      if(FileOff<VdiFileHeaderSize) {
        if(FileOff+ToRead>VdiFileHeaderSize) CurToRead=VdiFileHeaderSize-FileOff;
        else CurToRead=ToRead;
        if(XMountConfData.Writable==TRUE &&
           pCacheFileHeader->VdiFileHeaderCached==TRUE)
        {
          // VDI header was already cached
          if(fseeko(hCacheFile,
                    pCacheFileHeader->pVdiFileHeader+FileOff,
                    SEEK_SET)!=0)
          {
            LOG_ERROR("Couldn't seek to cached VDI header at offset %"
                      PRIu64 "\n",pCacheFileHeader->pVdiFileHeader+FileOff)
            return 0;
          }
          if(fread(buf,CurToRead,1,hCacheFile)!=1) {
            LOG_ERROR("Couldn't read %zu bytes from cache file at offset %"
                      PRIu64 "\n",CurToRead,
                      pCacheFileHeader->pVdiFileHeader+FileOff)
            return 0;
          }
          LOG_DEBUG("Read %zd bytes from cached VDI header at offset %"
                    PRIu64 " at cache file offset %" PRIu64 "\n",
                    CurToRead,FileOff,
                    pCacheFileHeader->pVdiFileHeader+FileOff)
        } else {
          // VDI header isn't cached
          memcpy(buf,((char*)pVdiFileHeader)+FileOff,CurToRead);
          LOG_DEBUG("Read %zd bytes at offset %" PRIu64
                    " from virtual VDI header\n",CurToRead,
                    FileOff)
        }
        if(ToRead==CurToRead) return ToRead;
        else {
          // Adjust values to read from original image
          ToRead-=CurToRead;
          buf+=CurToRead;
          FileOff=0;
        }
      } else FileOff-=VdiFileHeaderSize;
      break;
  }

  // Calculate block to read data from
  CurBlock=FileOff/CACHE_BLOCK_SIZE;
  BlockOff=FileOff%CACHE_BLOCK_SIZE;
  
  // Read image data
  while(ToRead!=0) {
    // Calculate how many bytes we have to read from this block
    if(BlockOff+ToRead>CACHE_BLOCK_SIZE) {
      CurToRead=CACHE_BLOCK_SIZE-BlockOff;
    } else CurToRead=ToRead;
    if(XMountConfData.Writable==TRUE &&
       pCacheFileBlockIndex[CurBlock].Assigned==TRUE)
    {
      // Write support enabled and need to read altered data from cachefile
      if(fseeko(hCacheFile,
                pCacheFileBlockIndex[CurBlock].off_data+BlockOff,
                SEEK_SET)!=0)
      {
        LOG_ERROR("Couldn't seek to offset %" PRIu64
                  " in cache file\n")
        return -1;
      }
      if(fread(buf,CurToRead,1,hCacheFile)!=1) {
        LOG_ERROR("Couldn't read data from cache file!\n")
        return -1;
      }
      LOG_DEBUG("Read %zd bytes at offset %" PRIu64
                " from cache file\n",CurToRead,FileOff)
    } else {
      // No write support or data not cached
      if(GetOrigImageData(buf,
                          FileOff,
                          CurToRead)!=CurToRead)
      {
        LOG_ERROR("Couldn't read data from input image!\n")
        return -1;
      }
      LOG_DEBUG("Read %zd bytes at offset %" PRIu64
                " from original image file\n",CurToRead,
                FileOff)
    }
    CurBlock++;
    BlockOff=0;
    buf+=CurToRead;
    ToRead-=CurToRead;
    FileOff+=CurToRead;
  }
  return size;
}

/*
 * SetVdiFileHeaderData:
 *   Write data to virtual VDI file header
 *
 * Params:
 *   buf: Buffer containing data to write
 *   offset: Offset of changes
 *   size: Amount of bytes to write
 *
 * Returns:
 *   Number of written bytes on success or "-1" on error
 */
static int SetVdiFileHeaderData(char *buf,off_t offset,size_t size) {
  if(offset+size>VdiFileHeaderSize) size=VdiFileHeaderSize-offset;
  LOG_DEBUG("Need to cache %zu bytes at offset %" PRIu64
            " from VDI header\n",size,offset)
  if(pCacheFileHeader->VdiFileHeaderCached==1) {
    // Header was already cached
    if(fseeko(hCacheFile,
              pCacheFileHeader->pVdiFileHeader+offset,
              SEEK_SET)!=0)
    {
      LOG_ERROR("Couldn't seek to cached VDI header at address %"
                PRIu64 "\n",pCacheFileHeader->pVdiFileHeader+offset)
      return -1;
    }
    if(fwrite(buf,size,1,hCacheFile)!=1) {
      LOG_ERROR("Couldn't write %zu bytes to cache file at offset %"
                PRIu64 "\n",size,
                pCacheFileHeader->pVdiFileHeader+offset)
      return -1;
    }
    LOG_DEBUG("Wrote %zd bytes at offset %" PRIu64 " to cache file\n",
              size,pCacheFileHeader->pVdiFileHeader+offset)
  } else {
    // Header wasn't already cached.
    if(fseeko(hCacheFile,
              0,
              SEEK_END)!=0)
    {
      LOG_ERROR("Couldn't seek to end of cache file!")
      return -1;
    }
    pCacheFileHeader->pVdiFileHeader=ftello(hCacheFile);
    LOG_DEBUG("Caching whole VDI header\n")
    if(offset>0) {
      // Changes do not begin at offset 0, need to prepend with data from
      // VDI header
      if(fwrite((char*)pVdiFileHeader,offset,1,hCacheFile)!=1) {
        LOG_ERROR("Error while writing %" PRIu64 " bytes "
                  "to cache file at offset %" PRIu64 "!\n",
                  offset,
                  pCacheFileHeader->pVdiFileHeader);
        return -1;
      }
      LOG_DEBUG("Prepended changed data with %" PRIu64
                " bytes at cache file offset %" PRIu64 "\n",
                offset,pCacheFileHeader->pVdiFileHeader)
    }
    // Cache changed data
    if(fwrite(buf,size,1,hCacheFile)!=1) {
      LOG_ERROR("Couldn't write %zu bytes to cache file at offset %"
                PRIu64 "\n",size,
                pCacheFileHeader->pVdiFileHeader+offset)
      return -1;
    }
    LOG_DEBUG("Wrote %zu bytes of changed data to cache file offset %"
              PRIu64 "\n",size,
              pCacheFileHeader->pVdiFileHeader+offset)
    if(offset+size!=VdiFileHeaderSize) {
      // Need to append data from VDI header to cache whole data struct
      if(fwrite(((char*)pVdiFileHeader)+offset+size,
                VdiFileHeaderSize-(offset+size),
                1,
                hCacheFile)!=1)
      {
        LOG_ERROR("Couldn't write %zu bytes to cache file at offset %"
                  PRIu64 "\n",VdiFileHeaderSize-(offset+size),
                  (uint64_t)(pCacheFileHeader->pVdiFileHeader+offset+size))
        return -1;
      }
      LOG_DEBUG("Appended %" PRIu32
                " bytes to changed data at cache file offset %"
                PRIu64 "\n",VdiFileHeaderSize-(offset+size),
                pCacheFileHeader->pVdiFileHeader+offset+size)
    }
    // Mark header as cached and update header in cache file
    pCacheFileHeader->VdiFileHeaderCached=1;
    if(fseeko(hCacheFile,0,SEEK_SET)!=0) {
      LOG_ERROR("Couldn't seek to offset 0 of cache file!\n")
      return -1;
    }
    if(fwrite((char*)pCacheFileHeader,sizeof(TCacheFileHeader),1,hCacheFile)!=1) {
      LOG_ERROR("Couldn't write changed cache file header!\n")
      return -1;
    }
  }
  // All important data has been written, now flush all buffers to make
  // sure data is written to cache file
  fflush(hCacheFile);
#ifndef __APPLE__
  ioctl(fileno(hCacheFile),BLKFLSBUF,0);
#endif
  return size;
}

/*
 * SetVirtImageData:
 *   Write data to virtual image
 *
 * Params:
 *   buf: Buffer containing data to write
 *   offset: Offset to start writing at
 *   size: Size of data to be written
 *
 * Returns:
 *   Number of written bytes on success or "-1" on error
 */
static int SetVirtImageData(const char *buf, off_t offset, size_t size) {
  uint64_t CurBlock=0;
  uint64_t VirtImageSize;
  uint64_t OrigImageSize;
  size_t ToWrite=0;
  size_t CurToWrite=0;
  off_t FileOff=offset;
  off_t BlockOff=0;
  char *WriteBuf=(char*)buf;
  char *buf2;
  ssize_t ret;

  // Get virtual image size
  if(!GetVirtImageSize(&VirtImageSize)) {
    LOG_ERROR("Couldn't get virtual image size!\n")
    return -1;
  }

  if(offset>=VirtImageSize) {
    LOG_ERROR("Attempt to write beyond EOF of virtual image file!\n")
    return -1;
  }

  if(offset+size>VirtImageSize) {
    LOG_DEBUG("Attempt to write past EOF of virtual image file\n")
    size=VirtImageSize-offset;
  }

  ToWrite=size;

  // Cache virtual image type specific data
  if(XMountConfData.VirtImageType==TVirtImageType_VDI) {
    if(FileOff<VdiFileHeaderSize) {
      ret=SetVdiFileHeaderData(WriteBuf,FileOff,ToWrite);
      if(ret==-1) {
        LOG_ERROR("Couldn't write data to virtual VDI file header!\n")
        return -1;
      }
      if(ret==ToWrite) return ToWrite;
      else {
        ToWrite-=ret;
        WriteBuf+=ret;
        FileOff=0;
      }
    } else FileOff-=VdiFileHeaderSize;
  }

  // Get original image size
  if(!GetOrigImageSize(&OrigImageSize)) {
    LOG_ERROR("Couldn't get original image size!\n")
    return -1;
  }

  // Calculate block to write data to
  CurBlock=FileOff/CACHE_BLOCK_SIZE;
  BlockOff=FileOff%CACHE_BLOCK_SIZE;
  
  while(ToWrite!=0) {
    // Calculate how many bytes we have to write to this block
    if(BlockOff+ToWrite>CACHE_BLOCK_SIZE) {
      CurToWrite=CACHE_BLOCK_SIZE-BlockOff;
    } else CurToWrite=ToWrite;
    if(pCacheFileBlockIndex[CurBlock].Assigned==1) {
      // Block was already cached
      // Seek to data offset in cache file
      if(fseeko(hCacheFile,
             pCacheFileBlockIndex[CurBlock].off_data+BlockOff,
             SEEK_SET)!=0)
      {
        LOG_ERROR("Couldn't seek to cached block at address %" PRIu64 "\n",
                  pCacheFileBlockIndex[CurBlock].off_data+BlockOff)
        return -1;
      }
      if(fwrite(WriteBuf,CurToWrite,1,hCacheFile)!=1) {
        LOG_ERROR("Error while writing %zu bytes "
                  "to cache file at offset %" PRIu64 "!\n",
                  CurToWrite,
                  pCacheFileBlockIndex[CurBlock].off_data+BlockOff);
        return -1;
      }
      LOG_DEBUG("Wrote %zd bytes at offset %" PRIu64
                " to cache file\n",CurToWrite,
                pCacheFileBlockIndex[CurBlock].off_data+BlockOff)
    } else {
      // Uncached block. Need to cache entire new block
      // Seek to end of cache file to append new cache block
      fseeko(hCacheFile,0,SEEK_END);
      pCacheFileBlockIndex[CurBlock].off_data=ftello(hCacheFile);
      if(BlockOff!=0) {
        // Changed data does not begin at block boundry. Need to prepend
        // with data from virtual image file
        XMOUNT_MALLOC(buf2,char*,BlockOff*sizeof(char))
        if(GetOrigImageData(buf2,FileOff-BlockOff,BlockOff)!=BlockOff) {
          LOG_ERROR("Couldn't read data from original image file!\n")
          return -1;
        }
        if(fwrite(buf2,BlockOff,1,hCacheFile)!=1) {
          LOG_ERROR("Couldn't writing %" PRIu64 " bytes "
                    "to cache file at offset %" PRIu64 "!\n",
                    BlockOff,
                    pCacheFileBlockIndex[CurBlock].off_data);
          return -1;
        }
        LOG_DEBUG("Prepended changed data with %" PRIu64
                  " bytes from virtual image file at offset %" PRIu64
                  "\n",BlockOff,FileOff-BlockOff)
        free(buf2);
      }
      if(fwrite(WriteBuf,CurToWrite,1,hCacheFile)!=1) {
        LOG_ERROR("Error while writing %zd bytes "
                  "to cache file at offset %" PRIu64 "!\n",
                  CurToWrite,
                  pCacheFileBlockIndex[CurBlock].off_data+BlockOff);
        return -1;
      }
      if(BlockOff+CurToWrite!=CACHE_BLOCK_SIZE) {
        // Changed data does not end at block boundry. Need to append
        // with data from virtual image file
        XMOUNT_MALLOC(buf2,char*,(CACHE_BLOCK_SIZE-
                                 (BlockOff+CurToWrite))*sizeof(char))
        memset(buf2,0,CACHE_BLOCK_SIZE-(BlockOff+CurToWrite));
        if((FileOff-BlockOff)+CACHE_BLOCK_SIZE>OrigImageSize) {
          // Original image is smaller than full cache block
          if(GetOrigImageData(buf2,
               FileOff+CurToWrite,
               OrigImageSize-(FileOff+CurToWrite))!=
             OrigImageSize-(FileOff+CurToWrite))
          {
            LOG_ERROR("Couldn't read data from virtual image file!\n")
            return -1;
          }
        } else {
          if(GetOrigImageData(buf2,
               FileOff+CurToWrite,
               CACHE_BLOCK_SIZE-(BlockOff+CurToWrite))!=
             CACHE_BLOCK_SIZE-(BlockOff+CurToWrite))
          {
            LOG_ERROR("Couldn't read data from virtual image file!\n")
            return -1;
          }
        }
        if(fwrite(buf2,
                  CACHE_BLOCK_SIZE-(BlockOff+CurToWrite),
                  1,
                  hCacheFile)!=1)
        {
          LOG_ERROR("Error while writing %zd bytes "
                    "to cache file at offset %" PRIu64 "!\n",
                    CACHE_BLOCK_SIZE-(BlockOff+CurToWrite),
                    pCacheFileBlockIndex[CurBlock].off_data+BlockOff+CurToWrite);
          return -1;
        }
        free(buf2);
      }
      // All important data for this cache block has been written,
      // flush all buffers and mark cache block as assigned
      fflush(hCacheFile);
#ifndef __APPLE__
      ioctl(fileno(hCacheFile),BLKFLSBUF,0);
#endif
      pCacheFileBlockIndex[CurBlock].Assigned=1;
      // Update cache block index entry in cache file
      fseeko(hCacheFile,
             sizeof(TCacheFileHeader)+(CurBlock*sizeof(TCacheFileBlockIndex)),
             SEEK_SET);
      if(fwrite(&(pCacheFileBlockIndex[CurBlock]),
                sizeof(TCacheFileBlockIndex),
                1,
                hCacheFile)!=1)
      {
        LOG_ERROR("Couldn't update cache file block index!\n");
        return -1;
      }
      LOG_DEBUG("Updated cache file block index: Number=%" PRIu64
                ", Data offset=%" PRIu64 "\n",CurBlock,
                pCacheFileBlockIndex[CurBlock].off_data);
    }
    // Flush buffers
    fflush(hCacheFile);
#ifndef __APPLE__
    ioctl(fileno(hCacheFile),BLKFLSBUF,0);
#endif
    BlockOff=0;
    CurBlock++;
    WriteBuf+=CurToWrite;
    ToWrite-=CurToWrite;
    FileOff+=CurToWrite;
  }

  return size;
}

/*
 * GetVirtFileAccess:
 *   FUSE access implementation
 *
 * Params:
 *   path: Path of file to get attributes from
 *   perm: Requested permissisons
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
/*
static int GetVirtFileAccess(const char *path, int perm) {
  // TODO: Implement propper file permission handling
  // http://www.cs.cf.ac.uk/Dave/C/node20.html
  // Values for the second argument to access.
  // These may be OR'd together.
  //#define	R_OK	4		// Test for read permission.
  //#define	W_OK	2		// Test for write permission.
  //#define	X_OK	1		// Test for execute permission.
  //#define	F_OK	0		// Test for existence.
  return 0;
}
*/

/*
 * GetVirtFileAttr:
 *   FUSE getattr implementation
 *
 * Params:
 *   path: Path of file to get attributes from
 *   stbuf: Pointer to stat structure to save attributes to
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
static int GetVirtFileAttr(const char *path, struct stat *stbuf) {
  memset(stbuf,0,sizeof(struct stat));
  if(strcmp(path,"/")==0) {
    // Attributes of mountpoint
    stbuf->st_mode=S_IFDIR | 0777;
    stbuf->st_nlink=2;
  } else if(strcmp(path,XMountConfData.pVirtualImagePath)==0) {
    // Attributes of virtual image
    if(!XMountConfData.Writable) stbuf->st_mode=S_IFREG | 0444;
    else stbuf->st_mode=S_IFREG | 0666;
    stbuf->st_nlink=1;
    // Get virtual image file size
    if(!GetVirtImageSize(&(stbuf->st_size))) {
      LOG_ERROR("Couldn't get image size!\n");
      return -ENOENT;
    }
  } else if(strcmp(path,XMountConfData.pVirtualImageInfoPath)==0) {
    // Attributes of virtual image info file
    stbuf->st_mode=S_IFREG | 0444;
    stbuf->st_nlink=1;
    // Get virtual image info file size
    if(pVirtualImageInfoFile!=NULL) {
      stbuf->st_size=strlen(pVirtualImageInfoFile);
    } else stbuf->st_size=0;
  } else if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
            XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    // Some special files only present when emulating VMDK files
    if(strcmp(path,XMountConfData.pVirtualVmdkPath)==0) {
      // Attributes of virtual vmdk file
      if(!XMountConfData.Writable) stbuf->st_mode=S_IFREG | 0444;
      else stbuf->st_mode=S_IFREG | 0666;
      stbuf->st_nlink=1;
      // Get virtual image info file size
      if(pVirtualVmdkFile!=NULL) {
        stbuf->st_size=VirtualVmdkFileSize;
      } else stbuf->st_size=0;
    } else if(pVirtualVmdkLockDir!=NULL &&
              strcmp(path,pVirtualVmdkLockDir)==0)
    {
      stbuf->st_mode=S_IFDIR | 0777;
      stbuf->st_nlink=2;
    } else if(pVirtualVmdkLockDir2!=NULL &&
              strcmp(path,pVirtualVmdkLockDir2)==0)
    {
      stbuf->st_mode=S_IFDIR | 0777;
      stbuf->st_nlink=2;
    } else if(pVirtualVmdkLockFileName!=NULL &&
              strcmp(path,pVirtualVmdkLockFileName)==0)
    {
      stbuf->st_mode=S_IFREG | 0666;
      if(pVirtualVmdkLockFileName!=NULL) {
        stbuf->st_size=strlen(pVirtualVmdkLockFileName);
      } else stbuf->st_size=0;
    } else return -ENOENT;
  } else return -ENOENT;
  // Set uid and gid of all files to uid and gid of current process
  stbuf->st_uid=getuid();
  stbuf->st_gid=getgid();
  return 0;
}

/*
 * CreateVirtDir:
 *   FUSE mkdir implementation
 *
 * Params:
 *   path: Directory path
 *   mode: Directory permissions
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
static int CreateVirtDir(const char *path, mode_t mode) {
  // Only allow creation of VMWare's lock directories
  if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
     XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    if(pVirtualVmdkLockDir==NULL)  {
      char aVmdkLockDir[strlen(XMountConfData.pVirtualVmdkPath)+5];
      sprintf(aVmdkLockDir,"%s.lck",XMountConfData.pVirtualVmdkPath);
      if(strcmp(path,aVmdkLockDir)==0) {
        LOG_DEBUG("Creating virtual directory \"%s\"\n",aVmdkLockDir)
        XMOUNT_STRSET(pVirtualVmdkLockDir,aVmdkLockDir)
        return 0;
      } else {
        LOG_ERROR("Attempt to create illegal directory \"%s\"!\n",path)
        LOG_DEBUG("Supposed: %s\n",aVmdkLockDir)
        return -1;
      }
    } else if(pVirtualVmdkLockDir2==NULL &&
              strncmp(path,pVirtualVmdkLockDir,strlen(pVirtualVmdkLockDir))==0)
    {
      LOG_DEBUG("Creating virtual directory \"%s\"\n",path)
      XMOUNT_STRSET(pVirtualVmdkLockDir2,path)
      return 0;
    } else {
      LOG_ERROR("Attempt to create illegal directory \"%s\"!\n",path)
      LOG_DEBUG("Compared to first %u chars of \"%s\"\n",strlen(pVirtualVmdkLockDir),pVirtualVmdkLockDir)
      return -1;
    }
  }
  LOG_ERROR("Attempt to create directory \"%s\" "
            "on read-only filesystem!\n",path)
  return -1;
}

/*
 * CreateVirtFile:
 *   FUSE create implementation.
 *   Only allows to create VMWare's lock file!
 *
 * Params:
 *   path: File to create
 *   mode: File mode
 *   dev: ??? but not used
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
static int CreateVirtFile(const char *path,
                          mode_t mode,
                          dev_t dev)
{
  if((XMountConfData.VirtImageType==TVirtImageType_VMDK ||
      XMountConfData.VirtImageType==TVirtImageType_VMDKS) &&
     pVirtualVmdkLockDir!=NULL && pVirtualVmdkLockFileName==NULL)
  {
    LOG_DEBUG("Creating virtual file \"%s\"\n",path)
    XMOUNT_STRSET(pVirtualVmdkLockFileName,path);
    return 0;
  } else {
    LOG_ERROR("Attempt to create illegal file \"%s\"\n",path)
    return -1;
  }
}

/*
 * GetVirtFiles:
 *   FUSE readdir implementation
 *
 * Params:
 *   path: Path from where files should be listed
 *   buf: Buffer to write file entrys to
 *   filler: Function to write file entrys to buffer
 *   offset: ??? but not used
 *   fi: ??? but not used
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
static int GetVirtFiles(const char *path,
                        void *buf,
                        fuse_fill_dir_t filler,
                        off_t offset,
                        struct fuse_file_info *fi)
{
  (void)offset;
  (void)fi;

  if(strcmp(path,"/")==0) {
    // Add std . and .. entrys
    filler(buf,".",NULL,0);
    filler(buf,"..",NULL,0);
    // Add our virtual files (p+1 to ignore starting "/")
    filler(buf,XMountConfData.pVirtualImagePath+1,NULL,0);
    filler(buf,XMountConfData.pVirtualImageInfoPath+1,NULL,0);
    if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
       XMountConfData.VirtImageType==TVirtImageType_VMDKS)
    {
      // For VMDK's, we use an additional descriptor file
      filler(buf,XMountConfData.pVirtualVmdkPath+1,NULL,0);
      // And there could also be a lock directory
      if(pVirtualVmdkLockDir!=NULL) {
        filler(buf,pVirtualVmdkLockDir+1,NULL,0);
      }
    }
  } else if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
            XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    // For VMDK emulation, there could be a lock directory
    if(pVirtualVmdkLockDir!=NULL && strcmp(path,pVirtualVmdkLockDir)==0) {
      filler(buf,".",NULL,0);
      filler(buf,"..",NULL,0);
      if(pVirtualVmdkLockFileName!=NULL) {
        filler(buf,pVirtualVmdkLockFileName+strlen(pVirtualVmdkLockDir)+1,NULL,0);
      }
    } else if(pVirtualVmdkLockDir2!=NULL &&
              strcmp(path,pVirtualVmdkLockDir2)==0)
    {
      filler(buf,".",NULL,0);
      filler(buf,"..",NULL,0);
    } else return -ENOENT;
  } else return -ENOENT;
  return 0;
}

/*
 * OpenVirtFile:
 *   FUSE open implementation
 *
 * Params:
 *   path: Path to file to open
 *   fi: ??? but not used
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
static int OpenVirtFile(const char *path, struct fuse_file_info *fi) {
  if(strcmp(path,XMountConfData.pVirtualImagePath)==0 ||
     strcmp(path,XMountConfData.pVirtualImageInfoPath)==0)
  {
    // Check open permissions
    if(!XMountConfData.Writable && (fi->flags & 3)!=O_RDONLY) {
      // Attempt to open a read-only file for writing
      LOG_DEBUG("Attempt to open the read-only file \"%s\" for writing.\n",path)
      return -EACCES;
    }
    return 0;
  } else if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
            XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    if(strcmp(path,XMountConfData.pVirtualVmdkPath)==0) {
      // Check open permissions
      if(!XMountConfData.Writable && (fi->flags & 3)!=O_RDONLY) {
        // Attempt to open a read-only file for writing
        LOG_DEBUG("Attempt to open the read-only file \"%s\" for writing.\n",path)
        return -EACCES;
      }
      return 0;
    } else if(pVirtualVmdkLockFileName!=NULL &&
              strcmp(path,pVirtualVmdkLockFileName)==0)
    {
      // Check open permissions
      if(!XMountConfData.Writable && (fi->flags & 3)!=O_RDONLY) {
        // Attempt to open a read-only file for writing
        LOG_DEBUG("Attempt to open the read-only file \"%s\" for writing.\n",path)
        return -EACCES;
      }
      return 0;
    } else {
      // Attempt to open a non existant file
      LOG_DEBUG("Attempt to open non existant file \"%s\".\n",path)
      return -ENOENT;
    }
  } else {
    // Attempt to open a non existant file
    LOG_DEBUG("Attempt to open non existant file \"%s\".\n",path)
    return -ENOENT;
  }
}

/*
 * ReadVirtFile:
 *   FUSE read implementation
 *
 * Params:
 *   buf: Buffer where read data is written to
 *   size: Number of bytes to read
 *   offset: Offset to start reading at
 *   fi: ?? but not used
 *
 * Returns:
 *   Read bytes on success, negated error code on error
 */
static int ReadVirtFile(const char *path,
                        char *buf,
                        size_t size,
                        off_t offset,
                        struct fuse_file_info *fi)
{
  uint64_t len;

  if(strcmp(path,XMountConfData.pVirtualImagePath)==0) {
    // Wait for other threads to end reading/writing data
    pthread_mutex_lock(&mutex_image_rw);

    // Get virtual image file size
    if(!GetVirtImageSize(&len)) {
      LOG_ERROR("Couldn't get virtual image size!\n")
      pthread_mutex_unlock(&mutex_image_rw);
      return 0;
    }
    if(offset<len) {
      if(offset+size>len) size=len-offset;
      if(GetVirtImageData(buf,offset,size)!=size) {
        LOG_ERROR("Couldn't read data from virtual image file!\n")
        pthread_mutex_unlock(&mutex_image_rw);
        return 0;
      }
    } else {
      LOG_DEBUG("Attempt to read past EOF of virtual image file\n");
      pthread_mutex_unlock(&mutex_image_rw);
      return 0;
    }

    // Allow other threads to read/write data again
    pthread_mutex_unlock(&mutex_image_rw);

  } else if(strcmp(path,XMountConfData.pVirtualImageInfoPath)==0) {
    // Read data from virtual image info file
    len=strlen(pVirtualImageInfoFile);
    if(offset<len) {
      if(offset+size>len) {
        size=len-offset;
        LOG_DEBUG("Attempt to read past EOF of virtual image info file\n")
      }
      pthread_mutex_lock(&mutex_info_read);
      memcpy(buf,pVirtualImageInfoFile+offset,size);
      pthread_mutex_unlock(&mutex_info_read);
      LOG_DEBUG("Read %" PRIu64 " bytes at offset %" PRIu64
                " from virtual image info file\n",size,offset)
    } else {
      LOG_DEBUG("Attempt to read past EOF of virtual info file\n");
      return 0;
    }
  } else if(strcmp(path,XMountConfData.pVirtualVmdkPath)==0) {
    // Read data from virtual vmdk file
    len=VirtualVmdkFileSize;
    if(offset<len) {
      if(offset+size>len) {
        LOG_DEBUG("Attempt to read past EOF of virtual vmdk file\n")
        LOG_DEBUG("Adjusting read size from %u to %u\n",size,len-offset)
        size=len-offset;
      }
      pthread_mutex_lock(&mutex_image_rw);
      memcpy(buf,pVirtualVmdkFile+offset,size);
      pthread_mutex_unlock(&mutex_image_rw);
      LOG_DEBUG("Read %" PRIu64 " bytes at offset %" PRIu64
                " from virtual vmdk file\n",size,offset)
    } else {
      LOG_DEBUG("Attempt to read behind EOF of virtual vmdk file\n");
      return 0;
    }
  } else if(pVirtualVmdkLockFileName!=NULL &&
            strcmp(path,pVirtualVmdkLockFileName)==0)
  {
    // Read data from virtual lock file
    len=VirtualVmdkLockFileDataSize;
    if(offset<len) {
      if(offset+size>len) {
        LOG_DEBUG("Attempt to read past EOF of virtual vmdk lock file\n")
        LOG_DEBUG("Adjusting read size from %u to %u\n",size,len-offset)
        size=len-offset;
      }
      pthread_mutex_lock(&mutex_image_rw);
      memcpy(buf,pVirtualVmdkLockFileData+offset,size);
      pthread_mutex_unlock(&mutex_image_rw);
      LOG_DEBUG("Read %" PRIu64 " bytes at offset %" PRIu64
                " from virtual vmdk lock file\n",size,offset)
    } else {
      LOG_DEBUG("Attempt to read past EOF of virtual vmdk lock file\n");
      return 0;
    }
  } else {
    // Attempt to read non existant file
    LOG_DEBUG("Attempt to read from non existant file \"%s\"\n",path)
    return -ENOENT;
  }

  return size;
}

/*
 * RenameVirtFile:
 *   FUSE rename implementation
 *
 * Params:
 *   path: File to rename
 *   npath: New filename
 *
 * Returns:
 *   "0" on error, negated error code on error
 */
static int RenameVirtFile(const char *path, const char *npath) {
  if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
     XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    if(pVirtualVmdkLockFileName!=NULL &&
       strcmp(path,pVirtualVmdkLockFileName)==0)
    {
      LOG_DEBUG("Renaming virtual lock file from \"%s\" to \"%s\"\n",
                pVirtualVmdkLockFileName,
                npath)
      XMOUNT_REALLOC(pVirtualVmdkLockFileName,char*,
                     (strlen(npath)+1)*sizeof(char));
      strcpy(pVirtualVmdkLockFileName,npath);
      return 0;
    }
  }
  return -ENOENT;
}

/*
 * DeleteVirtDir:
 *   FUSE rmdir implementation
 *
 * Params:
 *   path: Directory to delete
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
static int DeleteVirtDir(const char *path) {
  // Only VMWare's lock directories can be deleted
  if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
     XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    if(pVirtualVmdkLockDir!=NULL && strcmp(path,pVirtualVmdkLockDir)==0) {
      LOG_DEBUG("Deleting virtual lock dir \"%s\"\n",pVirtualVmdkLockDir)
      free(pVirtualVmdkLockDir);
      pVirtualVmdkLockDir=NULL;
      return 0;
    } else if(pVirtualVmdkLockDir2!=NULL &&
              strcmp(path,pVirtualVmdkLockDir2)==0)
    {
      LOG_DEBUG("Deleting virtual lock dir \"%s\"\n",pVirtualVmdkLockDir)
      free(pVirtualVmdkLockDir2);
      pVirtualVmdkLockDir2=NULL;
      return 0;
    }
  }
  return -1;
}

/*
 * DeleteVirtFile:
 *   FUSE unlink implementation
 *
 * Params:
 *   path: File to delete
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
static int DeleteVirtFile(const char *path) {
  // Only VMWare's lock file can be deleted
  if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
     XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    if(pVirtualVmdkLockFileName!=NULL &&
       strcmp(path,pVirtualVmdkLockFileName)==0)
    {
      LOG_DEBUG("Deleting virtual file \"%s\"\n",pVirtualVmdkLockFileName)
      free(pVirtualVmdkLockFileName);
      free(pVirtualVmdkLockFileData);
      pVirtualVmdkLockFileName=NULL;
      pVirtualVmdkLockFileData=NULL;
      VirtualVmdkLockFileDataSize=0;
      return 0;
    }
  }
  return -1;
}

/*
 * GetVirtFsStats:
 *   FUSE statfs implementation
 *
 * Params:
 *   path: Get stats for fs that the specified file resides in
 *   stats: Stats
 *
 * Returns:
 *   "0" on success, negated error code on error
 */
/*
static int GetVirtFsStats(const char *path, struct statvfs *stats) {
  struct statvfs CacheFileFsStats;
  int ret;

  if(XMountConfData.Writable==TRUE) {
    // If write support is enabled, return stats of fs upon which cache file
    // resides in
    if((ret=statvfs(XMountConfData.pCacheFile,&CacheFileFsStats))==0) {
      memcpy(stats,&CacheFileFsStats,sizeof(struct statvfs));
      return 0;
    } else {
      LOG_ERROR("Couldn't get stats for fs upon which resides \"%s\"\n",
                XMountConfData.pCacheFile)
      return ret;
    }
  } else {
    // TODO: Return read only
    return 0;
  }
}
*/

/*
 * WriteVirtFile:
 *   FUSE write implementation
 *
 * Params:
 *   buf: Buffer containing data to write
 *   size: Number of bytes to write
 *   offset: Offset to start writing at
 *   fi: ?? but not used
 *
 * Returns:
 *   Written bytes on success, negated error code on error
 */
static int WriteVirtFile(const char *path,
                         const char *buf,
                         size_t size,
                         off_t offset,
                         struct fuse_file_info *fi)
{
  uint64_t len;

  if(strcmp(path,XMountConfData.pVirtualImagePath)==0) {
    // Wait for other threads to end reading/writing data
    pthread_mutex_lock(&mutex_image_rw);

    // Get virtual image file size
    if(!GetVirtImageSize(&len)) {
      LOG_ERROR("Couldn't get virtual image size!\n")
      pthread_mutex_unlock(&mutex_image_rw);
      return 0;
    }
    if(offset<len) {
      if(offset+size>len) size=len-offset;
      if(SetVirtImageData(buf,offset,size)!=size) {
        LOG_ERROR("Couldn't write data to virtual image file!\n")
        pthread_mutex_unlock(&mutex_image_rw);
        return 0;
      }
    } else {
      LOG_DEBUG("Attempt to write past EOF of virtual image file\n")
      pthread_mutex_unlock(&mutex_image_rw);
      return 0;
    }

    // Allow other threads to read/write data again
    pthread_mutex_unlock(&mutex_image_rw);
  } else if(strcmp(path,XMountConfData.pVirtualVmdkPath)==0) {
    pthread_mutex_lock(&mutex_image_rw);
    len=VirtualVmdkFileSize;
    if((offset+size)>len) {
      // Enlarge or create buffer if needed
      if(len==0) {
        len=offset+size;
        XMOUNT_MALLOC(pVirtualVmdkFile,char*,len*sizeof(char))
      } else {
        len=offset+size;
        XMOUNT_REALLOC(pVirtualVmdkFile,char*,len*sizeof(char))
      }
      VirtualVmdkFileSize=offset+size;
    }
    // Copy data to buffer
    memcpy(pVirtualVmdkFile+offset,buf,size);
    pthread_mutex_unlock(&mutex_image_rw);
  } else if(pVirtualVmdkLockFileName!=NULL &&
            strcmp(path,pVirtualVmdkLockFileName)==0)
  {
    pthread_mutex_lock(&mutex_image_rw);
    if((offset+size)>VirtualVmdkLockFileDataSize) {
      // Enlarge or create buffer if needed
      if(VirtualVmdkLockFileDataSize==0) {
        VirtualVmdkLockFileDataSize=offset+size;
        XMOUNT_MALLOC(pVirtualVmdkLockFileData,char*,
                      VirtualVmdkLockFileDataSize*sizeof(char))
      } else {
        VirtualVmdkLockFileDataSize=offset+size;
        XMOUNT_REALLOC(pVirtualVmdkLockFileData,char*,
                       VirtualVmdkLockFileDataSize*sizeof(char))
      }
    }
    // Copy data to buffer
    memcpy(pVirtualVmdkLockFileData+offset,buf,size);
    pthread_mutex_unlock(&mutex_image_rw);
  } else if(strcmp(path,XMountConfData.pVirtualImageInfoPath)==0) {
    // Attempt to write data to read only image info file
    LOG_DEBUG("Attempt to write data to virtual info file\n");
    return -ENOENT;
  } else {
    // Attempt to write to non existant file
    LOG_DEBUG("Attempt to write to the non existant file \"%s\"\n",path)
    return -ENOENT;
  }

  return size;
}

/*
 * CalculateInputImageHash:
 *   Calculates an MD5 hash of the first HASH_AMOUNT bytes of the input image.
 *
 * Params:
 *   pHashLow : Pointer to the lower 64 bit of the hash
 *   pHashHigh : Pointer to the higher 64 bit of the hash
 *
 * Returns:
 *   TRUE on success, FALSE on error
 */
static int CalculateInputImageHash(uint64_t *pHashLow, uint64_t *pHashHigh) {
  char hash[16];
  md5_state_t md5_state;
  char *buf;
  XMOUNT_MALLOC(buf,char*,HASH_AMOUNT*sizeof(char))
  size_t read_data=GetOrigImageData(buf,0,HASH_AMOUNT);
  if(read_data>0) {
    // Calculate MD5 hash
    md5_init(&md5_state);
    md5_append(&md5_state,buf,HASH_AMOUNT);
    md5_finish(&md5_state,hash);
    // Convert MD5 hash into two 64bit integers
    *pHashLow=*((uint64_t*)hash);
    *pHashHigh=*((uint64_t*)(hash+8));
    free(buf);
    return TRUE;
  } else {
    LOG_ERROR("Couldn't read data from original image file!\n")
    free(buf);
    return FALSE;
  }
}

/*
 * InitVirtVdiHeader:
 *   Build and init virtual VDI file header
 *
 * Params:
 *   n/a
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int InitVirtVdiHeader() {
  // See http://forums.virtualbox.org/viewtopic.php?t=8046 for a
  // "description" of the various header fields

  uint64_t ImageSize;
  off_t offset;
  uint32_t i,BlockEntries;

  // Get input image size
  if(!GetOrigImageSize(&ImageSize)) {
    LOG_ERROR("Couldn't get input image size!\n")
    return FALSE;
  }

  // Calculate how many VDI blocks we need
  BlockEntries=ImageSize/VDI_IMAGE_BLOCK_SIZE;
  if((ImageSize%VDI_IMAGE_BLOCK_SIZE)!=0) BlockEntries++;
  VdiBlockMapSize=BlockEntries*sizeof(uint32_t);
  LOG_DEBUG("BlockMap: %d (%08X) entries, %d (%08X) bytes!\n",
            BlockEntries,
            BlockEntries,
            VdiBlockMapSize,
            VdiBlockMapSize)

  // Allocate memory for vdi header and block map
  VdiFileHeaderSize=sizeof(TVdiFileHeader)+VdiBlockMapSize;
  XMOUNT_MALLOC(pVdiFileHeader,pTVdiFileHeader,VdiFileHeaderSize)
  memset(pVdiFileHeader,0,VdiFileHeaderSize);
  pVdiBlockMap=((void*)pVdiFileHeader)+sizeof(TVdiFileHeader);

  // Init header values
  strncpy(pVdiFileHeader->szFileInfo,VDI_FILE_COMMENT,
          strlen(VDI_FILE_COMMENT)+1);
  pVdiFileHeader->u32Signature=VDI_IMAGE_SIGNATURE;
  pVdiFileHeader->u32Version=VDI_IMAGE_VERSION;
  pVdiFileHeader->cbHeader=0x00000180;  // No idea what this is for! Testimage had same value
  pVdiFileHeader->u32Type=VDI_IMAGE_TYPE_FIXED;
  pVdiFileHeader->fFlags=VDI_IMAGE_FLAGS;
  strncpy(pVdiFileHeader->szComment,VDI_HEADER_COMMENT,
          strlen(VDI_HEADER_COMMENT)+1);
  pVdiFileHeader->offData=VdiFileHeaderSize;
  pVdiFileHeader->offBlocks=sizeof(TVdiFileHeader);
  pVdiFileHeader->cCylinders=0; // Legacy info
  pVdiFileHeader->cHeads=0; // Legacy info
  pVdiFileHeader->cSectors=0; // Legacy info
  pVdiFileHeader->cbSector=512; // Legacy info
  pVdiFileHeader->u32Dummy=0;
  pVdiFileHeader->cbDisk=ImageSize;
  // Seems as VBox is always using a 1MB blocksize
  pVdiFileHeader->cbBlock=VDI_IMAGE_BLOCK_SIZE;
  pVdiFileHeader->cbBlockExtra=0;
  pVdiFileHeader->cBlocks=BlockEntries;
  pVdiFileHeader->cBlocksAllocated=BlockEntries;
  // Use partial MD5 input file hash as creation UUID and generate a random
  // modification UUID. VBox won't accept immages where create and modify UUIDS
  // aren't set.
  pVdiFileHeader->uuidCreate_l=XMountConfData.InputHashLo;
  pVdiFileHeader->uuidCreate_h=XMountConfData.InputHashHi;
  //*((uint32_t*)(&(pVdiFileHeader->uuidCreate_l)))=rand();
  //*((uint32_t*)(&(pVdiFileHeader->uuidCreate_l))+4)=rand();
  //*((uint32_t*)(&(pVdiFileHeader->uuidCreate_h)))=rand();
  //*((uint32_t*)(&(pVdiFileHeader->uuidCreate_h))+4)=rand();

#define rand64(var) {              \
  *((uint32_t*)&(var))=rand();     \
  *(((uint32_t*)&(var))+1)=rand(); \
}

  rand64(pVdiFileHeader->uuidModify_l);
  rand64(pVdiFileHeader->uuidModify_h);

#undef rand64

  // Generate block map
  i=0;
  for(offset=0;offset<VdiBlockMapSize;offset+=4) {
    *((uint32_t*)(pVdiBlockMap+offset))=i;
    i++;
  }

  LOG_DEBUG("VDI header size = %u\n",VdiFileHeaderSize)

  return TRUE;
}

/*
 * InitVirtualVmdkFile:
 *   Init the virtual VMDK file
 *
 * Params:
 *   n/a
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int InitVirtualVmdkFile() {
  uint64_t ImageSize=0;
  uint64_t ImageBlocks=0;
  char buf[500];

  // Get original image size
  if(!GetOrigImageSize(&ImageSize)) {
    LOG_ERROR("Couldn't get original image size!\n")
    return FALSE;
  }

  ImageBlocks=ImageSize/512;
  if(ImageSize%512!=0) ImageBlocks++;

#define VMDK_DESC_FILE "# Disk DescriptorFile\n" \
                       "version=1\n" \
                       "CID=fffffffe\n" \
                       "parentCID=ffffffff\n" \
                       "createType=\"monolithicFlat\"\n\n" \
                       "# Extent description\n" \
                       "RW %" PRIu64 " FLAT \"%s\" 0\n\n" \
                       "# The Disk Data Base\n" \
                       "#DDB\n" \
                       "ddb.virtualHWVersion = \"3\"\n" \
                       "ddb.adapterType = \"%s\"\n" \
                       "ddb.geometry.cylinders = \"0\"\n" \
                       "ddb.geometry.heads = \"0\"\n" \
                       "ddb.geometry.sectors = \"0\"\n"

  if(XMountConfData.VirtImageType==TVirtImageType_VMDK) {
    // VMDK with IDE bus
    sprintf(buf,
            VMDK_DESC_FILE,
            ImageBlocks,
            (XMountConfData.pVirtualImagePath)+1,
            "ide");
  } else if(XMountConfData.VirtImageType==TVirtImageType_VMDKS){
    // VMDK with SCSI bus
    sprintf(buf,
            VMDK_DESC_FILE,
            ImageBlocks,
            (XMountConfData.pVirtualImagePath)+1,
            "scsi");
  } else {
    LOG_ERROR("Unknown virtual VMDK file format!\n")
    return FALSE;
  }

#undef VMDK_DESC_FILE

  // Do not use XMOUNT_STRSET here to avoid adding '\0' to the buffer!
  XMOUNT_MALLOC(pVirtualVmdkFile,char*,strlen(buf))
  strncpy(pVirtualVmdkFile,buf,strlen(buf));
  VirtualVmdkFileSize=strlen(buf);

  return TRUE;
}

/*
 * InitVirtImageInfoFile:
 *   Create virtual image info file
 *
 * Params:
 *   n/a
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int InitVirtImageInfoFile() {
  char buf[200];
  int ret;

  // Add static header to file
  XMOUNT_MALLOC(pVirtualImageInfoFile,char*,(strlen(IMAGE_INFO_HEADER)+1))
  strncpy(pVirtualImageInfoFile,IMAGE_INFO_HEADER,strlen(IMAGE_INFO_HEADER)+1);

  switch(XMountConfData.OrigImageType) {
    case TOrigImageType_DD:
      // Original image is a DD file. There isn't much info to extract. Perhaps
      // just add image size
      // TODO: Add infos to virtual image info file
      break;

#ifdef WITH_LIBEWF
#define M_SAVE_VALUE(DESC) { \
  if(ret==1) {             \
    XMOUNT_REALLOC(pVirtualImageInfoFile,char*, \
      (strlen(pVirtualImageInfoFile)+strlen(buf)+strlen(DESC)+2)) \
    strncpy((pVirtualImageInfoFile+strlen(pVirtualImageInfoFile)),DESC,strlen(DESC)+1); \
    strncpy((pVirtualImageInfoFile+strlen(pVirtualImageInfoFile)),buf,strlen(buf)+1); \
    strncpy((pVirtualImageInfoFile+strlen(pVirtualImageInfoFile)),"\n",2); \
  } else if(ret==-1) { \
    LOG_ERROR("Couldn't query EWF image info!\n") \
    return FALSE; \
  } \
}
    case TOrigImageType_EWF:
      // Original image is an EWF file. Extract various infos from ewf file and
      // add them to the virtual image info file content.
      ret=libewf_get_header_value_case_number(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Case number: ")
      ret=libewf_get_header_value_description(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Description: ")
      ret=libewf_get_header_value_examiner_name(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Examiner: ")
      ret=libewf_get_header_value_evidence_number(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Evidence number: ")
      ret=libewf_get_header_value_notes(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Notes: ")
      ret=libewf_get_header_value_acquiry_date(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Acquiry date: ")
      ret=libewf_get_header_value_system_date(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("System date: ")
      ret=libewf_get_header_value_acquiry_operating_system(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Acquiry os: ")
      ret=libewf_get_header_value_acquiry_software_version(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("Acquiry sw version: ")
      ret=libewf_get_hash_value_md5(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("MD5 hash: ")
      ret=libewf_get_hash_value_sha1(hEwfFile,buf,sizeof(buf));
      M_SAVE_VALUE("SHA1 hash: ")
      break;
#undef M_SAVE_VALUE
#endif

#ifdef WITH_LIBAFF
    case TOrigImageType_AFF:
      // Original image is an AFF file.
      // TODO: Extract some infos from AFF file to add to our info file
      break;
#endif
    default:
      LOG_ERROR("Unsupported input image type!\n")
      return FALSE;
  }
  return TRUE;
}

/*
 * InitCacheFile:
 *   Create / load cache file to enable virtual write support
 *
 * Params:
 *   n/a
 *
 * Returns:
 *   "TRUE" on success, "FALSE" on error
 */
static int InitCacheFile() {
  uint64_t ImageSize=0;
  uint64_t BlockIndexSize=0;
  uint64_t CacheFileHeaderSize=0;
  uint64_t CacheFileSize=0;
  uint32_t NeededBlocks=0;
  uint64_t buf;

  if(!XMountConfData.OverwriteCache) {
    // Try to open an existing cache file or create a new one
    hCacheFile=(FILE*)FOPEN(XMountConfData.pCacheFile,"rb+");
    if(hCacheFile==NULL) {
      // As the c lib seems to have no possibility to open a file rw wether it
      // exists or not (w+ does not work because it truncates an existing file),
      // when r+ returns NULL the file could simply not exist
      LOG_DEBUG("Cache file does not exist. Creating new one\n")
      hCacheFile=(FILE*)FOPEN(XMountConfData.pCacheFile,"wb+");
      if(hCacheFile==NULL) {
        // There is really a problem opening the file
        LOG_ERROR("Couldn't open cache file \"%s\"!\n",
                  XMountConfData.pCacheFile)
        return FALSE;
      }
    }
  } else {
    // Overwrite existing cache file or create a new one
    hCacheFile=(FILE*)FOPEN(XMountConfData.pCacheFile,"wb+");
    if(hCacheFile==NULL) {
      LOG_ERROR("Couldn't open cache file \"%s\"!\n",
                XMountConfData.pCacheFile)
      return FALSE;
    }
  }

  // Get input image size
  if(!GetOrigImageSize(&ImageSize)) {
    LOG_ERROR("Couldn't get input image size!\n")
    return FALSE;
  }

  // Calculate how many blocks are needed and how big the buffers must be
  // for the actual cache file version
  NeededBlocks=ImageSize/CACHE_BLOCK_SIZE;
  if((ImageSize%CACHE_BLOCK_SIZE)!=0) NeededBlocks++;
  BlockIndexSize=NeededBlocks*sizeof(TCacheFileBlockIndex);
  CacheFileHeaderSize=sizeof(TCacheFileHeader)+BlockIndexSize;
  LOG_DEBUG("Cache blocks: %u (%04X) entries, %zd (%08zX) bytes\n",
            NeededBlocks,
            NeededBlocks,
            BlockIndexSize,
            BlockIndexSize)

  // Get cache file size
  // fseeko64 had massive problems!
  if(fseeko(hCacheFile,0,SEEK_END)!=0) {
    LOG_ERROR("Couldn't seek to end of cache file!\n")
    return FALSE;
  }
  // Same here, ftello64 didn't work at all and returned 0 all the times
  CacheFileSize=ftello(hCacheFile);
  LOG_DEBUG("Cache file has %zd bytes\n",CacheFileSize)

  if(CacheFileSize>0) {
    // Cache file isn't empty, parse block header
    LOG_DEBUG("Cache file not empty. Parsing block header\n")
    if(fseeko(hCacheFile,0,SEEK_SET)!=0) {
      LOG_ERROR("Couldn't seek to beginning of cache file!\n")
      return FALSE;
    }
    // Read and check file signature
    if(fread(&buf,8,1,hCacheFile)!=1 || buf!=CACHE_FILE_SIGNATURE) {
      free(pCacheFileHeader);
      LOG_ERROR("Not an xmount cache file or cache file corrupt!\n")
      return FALSE;
    }
    // Now get cache file version (Has only 32bit!)
    if(fread(&buf,4,1,hCacheFile)!=1) {
      free(pCacheFileHeader);
      LOG_ERROR("Not an xmount cache file or cache file corrupt!\n")
      return FALSE;
    }
    switch((uint32_t)buf) {
      case 0x00000001:
        // Old v1 cache file.
        LOG_ERROR("Unsupported cache file version!\n")
        LOG_ERROR("Please use xmount-tool to upgrade your cache file.\n")
        return FALSE;
      case CUR_CACHE_FILE_VERSION:
        // Current version
        if(fseeko(hCacheFile,0,SEEK_SET)!=0) {
          LOG_ERROR("Couldn't seek to beginning of cache file!\n")
          return FALSE;
        }
        // Alloc memory for header and block index
        XMOUNT_MALLOC(pCacheFileHeader,pTCacheFileHeader,CacheFileHeaderSize)
        memset(pCacheFileHeader,0,CacheFileHeaderSize);
        // Read header and block index from file
        if(fread(pCacheFileHeader,CacheFileHeaderSize,1,hCacheFile)!=1) {
          // Cache file isn't big enough
          free(pCacheFileHeader);
          LOG_ERROR("Cache file corrupt!\n")
          return FALSE;
        }
        break;
      default:
        LOG_ERROR("Unknown cache file version!\n")
        return FALSE;
    }
    // Check if cache file has same block size as we do
    if(pCacheFileHeader->BlockSize!=CACHE_BLOCK_SIZE) {
      LOG_ERROR("Cache file does not use default cache block size!\n")
      return FALSE;
    }
    // Set pointer to block index
    pCacheFileBlockIndex=(pTCacheFileBlockIndex)((void*)pCacheFileHeader+
                          pCacheFileHeader->pBlockIndex);
  } else {
    // New cache file, generate a new block header
    LOG_DEBUG("Cache file is empty. Generating new block header\n");
    // Alloc memory for header and block index
    XMOUNT_MALLOC(pCacheFileHeader,pTCacheFileHeader,CacheFileHeaderSize)
    memset(pCacheFileHeader,0,CacheFileHeaderSize);
    pCacheFileHeader->FileSignature=CACHE_FILE_SIGNATURE;
    pCacheFileHeader->CacheFileVersion=CUR_CACHE_FILE_VERSION;
    pCacheFileHeader->BlockSize=CACHE_BLOCK_SIZE;
    pCacheFileHeader->BlockCount=NeededBlocks;
    //pCacheFileHeader->UsedBlocks=0;
    // The following pointer is only usuable when reading data from cache file
    pCacheFileHeader->pBlockIndex=sizeof(TCacheFileHeader);
    pCacheFileBlockIndex=(pTCacheFileBlockIndex)((void*)pCacheFileHeader+
                         sizeof(TCacheFileHeader));
    pCacheFileHeader->VdiFileHeaderCached=FALSE;
    pCacheFileHeader->pVdiFileHeader=0;
    pCacheFileHeader->VmdkFileCached=FALSE;
    pCacheFileHeader->VmdkFileSize=0;
    pCacheFileHeader->pVmdkFile=0;
    // Write header to file
    if(fwrite(pCacheFileHeader,CacheFileHeaderSize,1,hCacheFile)!=1) {
      free(pCacheFileHeader);
      LOG_ERROR("Couldn't write cache file header to file!\n");
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * Struct containing implemented FUSE functions
 */
static struct fuse_operations xmount_operations = {
//  .access=GetVirtFileAccess,
  .getattr=GetVirtFileAttr,
  .mkdir=CreateVirtDir,
  .mknod=CreateVirtFile,
  .open=OpenVirtFile,
  .readdir=GetVirtFiles,
  .read=ReadVirtFile,
  .rename=RenameVirtFile,
  .rmdir=DeleteVirtDir,
//  .statfs=GetVirtFsStats,
  .unlink=DeleteVirtFile,
  .write=WriteVirtFile
//  .release=mountewf_release,
};

/*
 * Main
 */
int main(int argc, char *argv[])
{
  char **ppInputFilenames=NULL;
  int InputFilenameCount=0;
  int nargc=0;
  char **ppNargv=NULL;
  char *pMountpoint=NULL;
  int ret=1;
  int i=0;

  setbuf(stdout,NULL);
  setbuf(stderr,NULL);

  // Init XMountConfData
  XMountConfData.OrigImageType=TOrigImageType_DD;
  XMountConfData.VirtImageType=TVirtImageType_DD;
  XMountConfData.Debug=FALSE;
  XMountConfData.pVirtualImagePath=NULL;
  XMountConfData.pVirtualVmdkPath=NULL;
  XMountConfData.pVirtualImageInfoPath=NULL;
  XMountConfData.Writable=FALSE;
  XMountConfData.OverwriteCache=FALSE;
  XMountConfData.pCacheFile=NULL;
  XMountConfData.OrigImageSize=0;
  XMountConfData.VirtImageSize=0;
  XMountConfData.InputHashLo=0;
  XMountConfData.InputHashHi=0;

  // Parse command line options
  if(!ParseCmdLine(argc,
                   argv,
                   &nargc,
                   &ppNargv,
                   &InputFilenameCount,
                   &ppInputFilenames,
                   &pMountpoint))
  {
    LOG_ERROR("Error parsing command line options!\n")
    //PrintUsage(argv[0]);
    return 1;
  }

  // Check command line options
  if(nargc<2 /*|| InputFilenameCount==0 || pMountpoint==NULL*/) {
    LOG_ERROR("Couldn't parse command line options!\n")
    PrintUsage(argv[0]);
    return 1;
  }

  if(XMountConfData.Debug==TRUE) {
    LOG_DEBUG("Options passed to FUSE: ")
    for(i=0;i<nargc;i++) { printf("%s ",ppNargv[i]); }
    printf("\n");
  }

#ifdef WITH_LIBEWF
  // Check for valid ewf files
  if(XMountConfData.OrigImageType==TOrigImageType_EWF) {
    for(i=0;i<InputFilenameCount;i++) {
      if(libewf_check_file_signature(ppInputFilenames[i])!=1) {
        LOG_ERROR("File \"%s\" isn't a valid ewf file!\n",ppInputFilenames[i])
        return 1;
      }
    }
  }
#endif

  // TODO: Check if mountpoint is a valid dir

  // Init mutexes
  pthread_mutex_init(&mutex_image_rw,NULL);
  pthread_mutex_init(&mutex_info_read,NULL);

  if(InputFilenameCount==1) {
    LOG_DEBUG("Loading image file \"%s\"...\n",
              ppInputFilenames[0])
  } else {
    LOG_DEBUG("Loading image files \"%s .. %s\"...\n",
              ppInputFilenames[0],
              ppInputFilenames[InputFilenameCount-1])
  }

  // Init random generator
  srand(time(NULL));

  // Open input image
  switch(XMountConfData.OrigImageType) {
    case TOrigImageType_DD:
      // Input image is a DD file
      hDdFile=(FILE*)FOPEN(ppInputFilenames[0],"rb");
      if(hDdFile==NULL) {
        LOG_ERROR("Couldn't open DD file \"%s\"\n",ppInputFilenames[0])
        return 1;
      }
      break;
#ifdef WITH_LIBEWF
    case TOrigImageType_EWF:
      // Input image is an EWF file or glob
      hEwfFile=libewf_open(ppInputFilenames,
                           InputFilenameCount,
                           libewf_get_flags_read());
      if(hEwfFile==NULL) {
        LOG_ERROR("Couldn't open EWF file(s)!\n")
        return 1;
      }
      // Parse EWF header
      if(libewf_parse_header_values(hEwfFile,LIBEWF_DATE_FORMAT_ISO8601)!=1) {
        LOG_ERROR("Couldn't parse ewf header values!\n")
        return 1;
      }
      break;
#endif
#ifdef WITH_LIBAFF
    case TOrigImageType_AFF:
      // Input image is an AFF file
      hAffFile=af_open(ppInputFilenames[0],O_RDONLY,0);
      if(!hAffFile) {
        LOG_ERROR("Couldn't open AFF file!\n")
        return 1;
      }
      if(af_cannot_decrypt(hAffFile)) {
        LOG_ERROR("Encrypted AFF input images aren't supported yet!\n")
        return 1;
      }
      break;
#endif
    default:
      LOG_ERROR("Unsupported input image type specified!\n")
      return 1;
  }
  LOG_DEBUG("Input image file opened successfully\n")

  // Calculate partial MD5 hash of input image file
  if(CalculateInputImageHash(&(XMountConfData.InputHashLo),
                             &(XMountConfData.InputHashHi))==FALSE)
  {
    LOG_ERROR("Couldn't calculate partial hash of input image file!\n")
    return 1;
  }

  if(XMountConfData.Debug==TRUE) {
    LOG_DEBUG("Partial MD5 hash of input image file: ")
    for(i=0;i<8;i++) printf("%02hhx",
                            *(((char*)(&(XMountConfData.InputHashLo)))+i));
    for(i=0;i<8;i++) printf("%02hhx",
                            *(((char*)(&(XMountConfData.InputHashHi)))+i));
    printf("\n");
  }

  if(!ExtractVirtFileNames(ppInputFilenames[0])) {
    LOG_ERROR("Couldn't extract virtual file names!\n");
    return 1;
  }
  LOG_DEBUG("Virtual file names extracted successfully\n")

  // Gather infos for info file
  if(!InitVirtImageInfoFile()) {
    LOG_ERROR("Couldn't gather infos for virtual image info file!\n")
    return 1;
  }
  LOG_DEBUG("Virtual image info file build successfully\n")

  // Do some virtual image type specific initialisations
  switch(XMountConfData.VirtImageType) {
    case TVirtImageType_DD:
      break;
    case TVirtImageType_VDI:
      // When mounting as VDI, we need to construct a vdi header
      if(!InitVirtVdiHeader()) {
        LOG_ERROR("Couldn't initialize virtual VDI file header!\n")
        return 1;
      }
      LOG_DEBUG("Virtual VDI file header build successfully\n")
      break;
    case TVirtImageType_VMDK:
    case TVirtImageType_VMDKS:
      // When mounting as VMDK, we need to construct the VMDK descriptor file
      if(!InitVirtualVmdkFile()) {
        LOG_ERROR("Couldn't initialize virtual VMDK file!\n")
        return 1;
      }
      break;
  }

  if(XMountConfData.Writable) {
    // Init cache file and cache file block index
    if(!InitCacheFile()) {
      LOG_ERROR("Couldn't initialize cache file!\n")
      return 1;
    }
    LOG_DEBUG("Cache file initialized successfully\n")
  }

  // Call fuse_main to do the fuse magic
  ret=fuse_main(nargc,ppNargv,&xmount_operations,NULL);

  // Destroy mutexes
  pthread_mutex_destroy(&mutex_image_rw);
  pthread_mutex_destroy(&mutex_info_read);

  // Close input image
  switch(XMountConfData.OrigImageType) {
    case TOrigImageType_DD:
      fclose(hDdFile);
      break;
#ifdef WITH_LIBEWF
    case TOrigImageType_EWF:
      libewf_close(hEwfFile);
      break;
#endif
#ifdef WITH_LIBAFF
    case TOrigImageType_AFF:
      af_close(hAffFile);
      break;
#endif
    default:
      LOG_ERROR("Couldn't close unsupported input image type!\n");
  }

  if(XMountConfData.Writable) {
    // Write support was enabled, close cache file
    fclose(hCacheFile);
    free(pCacheFileHeader);
  }

  // Free allocated memory
  if(XMountConfData.VirtImageType==TVirtImageType_VDI) {
    // Free constructed VDI header
    free(pVdiFileHeader);
  }
  if(XMountConfData.VirtImageType==TVirtImageType_VMDK ||
     XMountConfData.VirtImageType==TVirtImageType_VMDKS)
  {
    // Free constructed VMDK file
    free(pVirtualVmdkFile);
    free(XMountConfData.pVirtualVmdkPath);
    if(pVirtualVmdkLockFileName!=NULL) free(pVirtualVmdkLockFileName);
    if(pVirtualVmdkLockFileData!=NULL) free(pVirtualVmdkLockFileData);
    if(pVirtualVmdkLockDir!=NULL) free(pVirtualVmdkLockDir);
    if(pVirtualVmdkLockDir2!=NULL) free(pVirtualVmdkLockDir2);
  }
  for(i=0;i<InputFilenameCount;i++) free(ppInputFilenames[i]);
  free(ppInputFilenames);
  for(i=0;i<nargc;i++) free(ppNargv[i]);
  free(ppNargv);
  free(XMountConfData.pVirtualImagePath);
  free(XMountConfData.pVirtualImageInfoPath);
  free(XMountConfData.pCacheFile);

  return ret;
}

/*
  ----- Change history -----
  20090131: v0.1.0 released
            * Some minor things have still to be done.
            * Mounting ewf as dd: Seems to work. Diff didn't complain about
              changes between original dd and emulated dd.
            * Mounting ewf as vdi: Seems to work too. VBox accepts the emulated
              vdi as valid vdi file and I was able to mount the containing fs
              under Debian. INFO: Debian freezed when not using mount -r !!
  20090203: v0.1.1 released
            * Multiple code improvements. For ex. cleaner vdi header allocation.
            * Fixed severe bug in image block calculation. Didn't check for odd
              input in conversion from bytes to megabytes.
            * Added more debug output
  20090210: v0.1.2 released
            * Fixed compilation problem (Typo in image_init_info() function).
            * Fixed some problems with the debian scripts to be able to build
              packages.
            * Added random generator initialisation (Makes it possible to use
              more than one image in VBox at a time).
  20090215: * Added function init_cache_blocks which creates / loads a cache
              file used to implement virtual write capability.
  20090217: * Implemented the fuse write function. Did already some basic tests
              with dd and it seems to work. But there are certainly still some
              bugs left as there are also still some TODO's left.
  20090226: * Changed program name from mountewf to xmount.
            * Began with massive code cleanups to ease full implementation of
              virtual write support and to be able to support multiple input
              image formats (DD, EWF and AFF are planned for now).
            * Added defines for supported input formats so it should be possible
              to compile xmount without supporting all input formats. (DD
              input images are always supported as these do not require any
              additional libs). Input formats should later be en/disabled
              by the configure script in function to which libs it detects.
            * GetOrigImageSize function added to get the size of the original
              image whatever type it is in.
            * GetOrigImageData function added to retrieve data from original
              image file whatever type it is in.
            * GetVirtImageSize function added to get the size of the virtual
              image file.
            * Cleaned function mountewf_getattr and renamed it to
              GetVirtFileAttr
            * Cleaned function mountewf_readdir and renamed it to GetVirtFiles
            * Cleaned function mountewf_open and renamed it to OpenVirtFile
  20090227: * Cleaned function init_info_file and renamed it to
              InitVirtImageInfoFile
  20090228: * Cleaned function init_cache_blocks and renamed it to
              InitCacheFile
            * Added LogMessage function to ease error and debug logging (See
              also LOG_ERROR and LOG_DEBUG macros in xmount.h)
            * Cleaned function init_vdi_header and renamed it to
              InitVirtVdiHeader
            * Added PrintUsage function to print out xmount usage informations
            * Cleaned function parse_cmdline and renamed it to ParseCmdLine
            * Cleaned function main
            * Added ExtractVirtFileNames function to extract virtual file names
              from input image name
            * Added function GetVirtImageData to retrieve data from the virtual
              image file. This includes reading data from cache file if virtual
              write support is enabled.
            * Added function ReadVirtFile to replace mountewf_read
  20090229: * Fixed a typo in virtual file name creation
            * Added function SetVirtImageData to write data to virtual image
              file. This includes writing data to cache file and caching entire
              new blocks
            * Added function WriteVirtFile to replace mountewf_write
  20090305: * Solved a problem that made it impossible to access offsets >32bit
  20090308: * Added SetVdiFileHeaderData function to handle virtual image type
              specific data to be cached. This makes cache files independent
              from virtual image type
  20090316: v0.2.0 released
  20090327: v0.2.1 released
            * Fixed a bug in virtual write support. Checking whether data is
              cached didn't use semaphores. This could corrupt cache files
              when running multi-threaded.
            * Added IsVdiFileHeaderCached function to check whether VDI file
              header was already cached
            * Added IsBlockCached function to check whether a block was already
              cached
  20090331: v0.2.2 released (Internal release)
            * Further changes to semaphores to fix write support bug.
  20090410: v0.2.3 released
            * Reverted most of the fixes from v0.2.1 and v0.2.2 as those did not
              solve the write support bug.
            * Removed all semaphores
            * Added two pthread mutexes to protect virtual image and virtual
              info file.
  20090508: * Configure script will now exit when needed libraries aren't found
            * Added support for newest libewf beta version 20090506 as it seems
              to reduce memory usage when working with EWF files by about 1/2.
            * Added LIBEWF_BETA define to adept source to new libewf API.
            * Added function InitVirtualVmdkFile to build a VmWare virtual disk
              descriptor file.
  20090519: * Added function CreateVirtDir implementing FUSE's mkdir to allow
              VMWare to create his <iname>.vmdk.lck lock folder. Function does
              not allow to create other folders!
            * Changed cache file handling as VMDK caching will need new cache
              file structure incompatible to the old one.
  20090522: v0.3.0 released
            * Added function DeleteVirtFile and DeleteVirtDir so VMWare can
              remove his lock directories and files.
            * Added function RenameVirtFile because VMWare needs to rename his
              lock files.
            * VMDK support should work now but descriptor file won't get cached
              as I didn't implement it yet.
  20090604: * Added --cache commandline parameter doing the same as --rw.
            * Added --owcache commandline parameter doing the same as --rw but
              overwrites any existing cache data. This can be handy for
              debugging and testing purposes.
            * Added "vmdks" output type. Same as "vmdk" but generates a disk
              connected to the SCSI bus rather than the IDE bus.
  20090710: v0.3.1 released
  20090721: * Added function CheckFuseAllowOther to check wether FUSE supports
              the "-o allow_other" option. It is supported when
              "user_allow_other" is set in /etc/fuse.conf or when running
              xmount as root.
            * Automatic addition of FUSE's "-o allow_other" option if it is
              supported.
            * Added special "-o no_allow_other" command line parameter to
              disable automatic addition of the above option.
            * Reorganisation of FUSE's and xmount's command line options
              processing.
            * Added LogWarnMessage function to output a warning message.
  20090722: * Added function CalculateInputImageHash to calculate an MD5 hash
              of the first input image's HASH_AMOUNT bytes of data. This hash is
              used as VDI creation UUID and will later be used to match cache
              files to input images.
  20090724: v0.3.2 released
  20090725: v0.4.0 released
            * Added AFF input image support.
            * Due to various problems with libewf and libaff packages (Mainly
              in Debian and Ubuntu), I decided to include them into xmount's
              source tree and link them in statically. This has the advantage
              that I can use whatever version I want.
  20090727: v0.4.1 released
            * Added again the ability to compile xmount with shared libs as the
              Debian folks don't like the static ones :)
  20090812: * Added TXMountConfData.OrigImageSize and
              TXMountConfData.VirtImageSize to save the size of the input and
              output image in order to avoid regetting it always from disk.
  20090814: * Replaced all malloc and realloc occurences with the two macros
              XMOUNT_MALLOC and XMOUNT_REALLOC.
  20090816: * Replaced where applicable all occurences of str(n)cpy or
              alike with their corresponding macros XMOUNT_STRSET, XMOUNT_STRCPY
              and XMOUNT_STRNCPY pendants.
  20090907: v0.4.2 released
            * Fixed a bug in VMDK lock file access. VirtualVmdkLockFileDataSize
              wasn't reset to 0 when the file was deleted.
            * Fixed a bug in VMDK descriptor file access. Had to add
              VirtualVmdkFileSize to track the size of this file as strlen was
              a bad idea :).
  20100324: v0.4.3 released
            * Changed all header structs to prevent different sizes on i386 and
              amd64. See xmount.h for more details.
  20100810: v0.4.4 released
            * Found a bug in InitVirtVdiHeader(). The 64bit values were
              addressed incorrectly while filled with rand(). This leads to an
              error message when trying to add a VDI file to VirtualBox 3.2.8.
  20110210: * Adding subtype and fsname FUSE options in order to display mounted
              source in mount command output.
  20110211: v0.4.5 released
*/
