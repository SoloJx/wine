/*
 * Win32 device functions
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 1998 Ulrich Weigand
 * Copyright 1998 Patrik Stridvall
 *
 */

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "winbase.h"
#include "winreg.h"
#include "winerror.h"
#include "file.h"
#include "process.h"
#include "mmsystem.h"
#include "heap.h"
#include "winioctl.h"
#include "winnt.h"
#include "msdos.h"
#include "miscemu.h"
#include "server.h"
#include "debugtools.h"

DEFAULT_DEBUG_CHANNEL(win32)


static BOOL DeviceIo_VTDAPI(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped);
static BOOL DeviceIo_MONODEBG(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped);
static BOOL DeviceIo_MMDEVLDR(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped);

static DWORD VxDCall_VMM( DWORD service, va_list args );

static BOOL DeviceIo_IFSMgr(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped);

static BOOL DeviceIo_VWin32(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped);

static BOOL DeviceIo_PCCARD (DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped);
/*
 * VxD names are taken from the Win95 DDK
 */

struct VxDInfo
{
    LPCSTR  name;
    WORD    id;
    DWORD (*vxdcall)(DWORD, va_list);
    BOOL  (*deviceio)(DWORD, LPVOID, DWORD, 
                        LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
};

static const struct VxDInfo VxDList[] = 
{
    /* Standard VxD IDs */
    { "VMM",      0x0001, VxDCall_VMM, NULL },
    { "DEBUG",    0x0002, NULL, NULL },
    { "VPICD",    0x0003, NULL, NULL },
    { "VDMAD",    0x0004, NULL, NULL },
    { "VTD",      0x0005, NULL, NULL },
    { "V86MMGR",  0x0006, NULL, NULL },
    { "PAGESWAP", 0x0007, NULL, NULL },
    { "PARITY",   0x0008, NULL, NULL },
    { "REBOOT",   0x0009, NULL, NULL },
    { "VDD",      0x000A, NULL, NULL },
    { "VSD",      0x000B, NULL, NULL },
    { "VMD",      0x000C, NULL, NULL },
    { "VKD",      0x000D, NULL, NULL },
    { "VCD",      0x000E, NULL, NULL },
    { "VPD",      0x000F, NULL, NULL },
    { "BLOCKDEV", 0x0010, NULL, NULL },
    { "VMCPD",    0x0011, NULL, NULL },
    { "EBIOS",    0x0012, NULL, NULL },
    { "BIOSXLAT", 0x0013, NULL, NULL },
    { "VNETBIOS", 0x0014, NULL, NULL },
    { "DOSMGR",   0x0015, NULL, NULL },
    { "WINLOAD",  0x0016, NULL, NULL },
    { "SHELL",    0x0017, NULL, NULL },
    { "VMPOLL",   0x0018, NULL, NULL },
    { "VPROD",    0x0019, NULL, NULL },
    { "DOSNET",   0x001A, NULL, NULL },
    { "VFD",      0x001B, NULL, NULL },
    { "VDD2",     0x001C, NULL, NULL },
    { "WINDEBUG", 0x001D, NULL, NULL },
    { "TSRLOAD",  0x001E, NULL, NULL },
    { "BIOSHOOK", 0x001F, NULL, NULL },
    { "INT13",    0x0020, NULL, NULL },
    { "PAGEFILE", 0x0021, NULL, NULL },
    { "SCSI",     0x0022, NULL, NULL },
    { "MCA_POS",  0x0023, NULL, NULL },
    { "SCSIFD",   0x0024, NULL, NULL },
    { "VPEND",    0x0025, NULL, NULL },
    { "VPOWERD",  0x0026, NULL, NULL },
    { "VXDLDR",   0x0027, NULL, NULL },
    { "NDIS",     0x0028, NULL, NULL },
    { "BIOS_EXT", 0x0029, NULL, NULL },
    { "VWIN32",   0x002A, NULL, DeviceIo_VWin32 },
    { "VCOMM",    0x002B, NULL, NULL },
    { "SPOOLER",  0x002C, NULL, NULL },
    { "WIN32S",   0x002D, NULL, NULL },
    { "DEBUGCMD", 0x002E, NULL, NULL },

    { "VNB",      0x0031, NULL, NULL },
    { "SERVER",   0x0032, NULL, NULL },
    { "CONFIGMG", 0x0033, NULL, NULL },
    { "DWCFGMG",  0x0034, NULL, NULL },
    { "SCSIPORT", 0x0035, NULL, NULL },
    { "VFBACKUP", 0x0036, NULL, NULL },
    { "ENABLE",   0x0037, NULL, NULL },
    { "VCOND",    0x0038, NULL, NULL },

    { "EFAX",     0x003A, NULL, NULL },
    { "DSVXD",    0x003B, NULL, NULL },
    { "ISAPNP",   0x003C, NULL, NULL },
    { "BIOS",     0x003D, NULL, NULL },
    { "WINSOCK",  0x003E, NULL, NULL },
    { "WSOCK",    0x003E, NULL, NULL },
    { "WSIPX",    0x003F, NULL, NULL },
    { "IFSMgr",   0x0040, NULL, DeviceIo_IFSMgr },
    { "VCDFSD",   0x0041, NULL, NULL },
    { "MRCI2",    0x0042, NULL, NULL },
    { "PCI",      0x0043, NULL, NULL },
    { "PELOADER", 0x0044, NULL, NULL },
    { "EISA",     0x0045, NULL, NULL },
    { "DRAGCLI",  0x0046, NULL, NULL },
    { "DRAGSRV",  0x0047, NULL, NULL },
    { "PERF",     0x0048, NULL, NULL },
    { "AWREDIR",  0x0049, NULL, NULL },

    /* Far East support */
    { "ETEN",     0x0060, NULL, NULL },
    { "CHBIOS",   0x0061, NULL, NULL },
    { "VMSGD",    0x0062, NULL, NULL },
    { "VPPID",    0x0063, NULL, NULL },
    { "VIME",     0x0064, NULL, NULL },
    { "VHBIOSD",  0x0065, NULL, NULL },

    /* Multimedia OEM IDs */
    { "VTDAPI",   0x0442, NULL, DeviceIo_VTDAPI },
    { "MMDEVLDR", 0x044A, NULL, DeviceIo_MMDEVLDR },

    /* Network Device IDs */
    { "VNetSup",  0x0480, NULL, NULL },
    { "VRedir",   0x0481, NULL, NULL },
    { "VBrowse",  0x0482, NULL, NULL },
    { "VSHARE",   0x0483, NULL, NULL },
    { "IFSMgr",   0x0484, NULL, NULL },
    { "MEMPROBE", 0x0485, NULL, NULL },
    { "VFAT",     0x0486, NULL, NULL },
    { "NWLINK",   0x0487, NULL, NULL },
    { "VNWLINK",  0x0487, NULL, NULL },
    { "NWSUP",    0x0487, NULL, NULL },
    { "VTDI",     0x0488, NULL, NULL },
    { "VIP",      0x0489, NULL, NULL },
    { "VTCP",     0x048A, NULL, NULL },
    { "VCache",   0x048B, NULL, NULL },
    { "VUDP",     0x048C, NULL, NULL },
    { "VAsync",   0x048D, NULL, NULL },
    { "NWREDIR",  0x048E, NULL, NULL },
    { "STAT80",   0x048F, NULL, NULL },
    { "SCSIPORT", 0x0490, NULL, NULL },
    { "FILESEC",  0x0491, NULL, NULL },
    { "NWSERVER", 0x0492, NULL, NULL },
    { "SECPROV",  0x0493, NULL, NULL },
    { "NSCL",     0x0494, NULL, NULL },
    { "WSTCP",    0x0495, NULL, NULL },
    { "NDIS2SUP", 0x0496, NULL, NULL },
    { "MSODISUP", 0x0497, NULL, NULL },
    { "Splitter", 0x0498, NULL, NULL },
    { "PPP",      0x0499, NULL, NULL },
    { "VDHCP",    0x049A, NULL, NULL },
    { "VNBT",     0x049B, NULL, NULL },
    { "LOGGER",   0x049D, NULL, NULL },
    { "EFILTER",  0x049E, NULL, NULL },
    { "FFILTER",  0x049F, NULL, NULL },
    { "TFILTER",  0x04A0, NULL, NULL },
    { "AFILTER",  0x04A1, NULL, NULL },
    { "IRLAMP",   0x04A2, NULL, NULL },

    { "PCCARD",   0x097C, NULL, DeviceIo_PCCARD },

    /* WINE additions, ids unknown */
    { "MONODEBG.VXD", 0x4242, NULL, DeviceIo_MONODEBG },

    { NULL,       0,      NULL, NULL }
};

/*
 * VMM VxDCall service names are (mostly) taken from Stan Mitchell's
 * "Inside the Windows 95 File System"
 */

#define N_VMM_SERVICE 41

LPCSTR VMM_Service_Name[N_VMM_SERVICE] =
{ 
    "PageReserve",            /* 0x0000 */
    "PageCommit",             /* 0x0001 */
    "PageDecommit",           /* 0x0002 */
    "PagerRegister",          /* 0x0003 */
    "PagerQuery",             /* 0x0004 */
    "HeapAllocate",           /* 0x0005 */
    "ContextCreate",          /* 0x0006 */
    "ContextDestroy",         /* 0x0007 */
    "PageAttach",             /* 0x0008 */
    "PageFlush",              /* 0x0009 */
    "PageFree",               /* 0x000A */
    "ContextSwitch",          /* 0x000B */
    "HeapReAllocate",         /* 0x000C */
    "PageModifyPerm",         /* 0x000D */
    "PageQuery",              /* 0x000E */
    "GetCurrentContext",      /* 0x000F */
    "HeapFree",               /* 0x0010 */
    "RegOpenKey",             /* 0x0011 */
    "RegCreateKey",           /* 0x0012 */
    "RegCloseKey",            /* 0x0013 */
    "RegDeleteKey",           /* 0x0014 */
    "RegSetValue",            /* 0x0015 */
    "RegDeleteValue",         /* 0x0016 */
    "RegQueryValue",          /* 0x0017 */
    "RegEnumKey",             /* 0x0018 */
    "RegEnumValue",           /* 0x0019 */
    "RegQueryValueEx",        /* 0x001A */
    "RegSetValueEx",          /* 0x001B */
    "RegFlushKey",            /* 0x001C */
    "RegQueryInfoKey",        /* 0x001D */
    "GetDemandPageInfo",      /* 0x001E */
    "BlockOnID",              /* 0x001F */
    "SignalID",               /* 0x0020 */
    "RegLoadKey",             /* 0x0021 */
    "RegUnLoadKey",           /* 0x0022 */
    "RegSaveKey",             /* 0x0023 */
    "RegRemapPreDefKey",      /* 0x0024 */
    "PageChangePager",        /* 0x0025 */
    "RegQueryMultipleValues", /* 0x0026 */
    "RegReplaceKey",          /* 0x0027 */
    "<KERNEL32.101>"          /* 0x0028 -- What does this do??? */
};

HANDLE DEVICE_Open( LPCSTR filename, DWORD access,
                      LPSECURITY_ATTRIBUTES sa )
{
    const struct VxDInfo *info;

    for (info = VxDList; info->name; info++)
        if (!lstrcmpiA( info->name, filename ))
            return FILE_CreateDevice( info->id | 0x10000, access, sa );

    FIXME( "Unknown VxD %s\n", filename);
    return FILE_CreateDevice( 0x10000, access, sa );
}

static const struct VxDInfo *DEVICE_GetInfo( HANDLE handle )
{
    struct get_file_info_request req;
    struct get_file_info_reply reply;

    req.handle = handle;
    CLIENT_SendRequest( REQ_GET_FILE_INFO, -1, 1, &req, sizeof(req) );
    if (!CLIENT_WaitSimpleReply( &reply, sizeof(reply), NULL ) &&
        (reply.type == FILE_TYPE_UNKNOWN) &&
        (reply.attr & 0x10000))
    {
        const struct VxDInfo *info;

        for (info = VxDList; info->name; info++)
            if (info->id == LOWORD(reply.attr)) break;
        return info;
    }
    return NULL;
}

/****************************************************************************
 *		DeviceIoControl (KERNEL32.188)
 * This is one of those big ugly nasty procedure which can do
 * a million and one things when it comes to devices. It can also be
 * used for VxD communication.
 *
 * A return value of FALSE indicates that something has gone wrong which
 * GetLastError can decypher.
 */
BOOL WINAPI DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped)
{
        const struct VxDInfo *info;

        TRACE( "(%d,%ld,%p,%ld,%p,%ld,%p,%p)\n",
               hDevice,dwIoControlCode,lpvInBuffer,cbInBuffer,
               lpvOutBuffer,cbOutBuffer,lpcbBytesReturned,lpOverlapped	);

	if (!(info = DEVICE_GetInfo( hDevice )))
	{
		SetLastError( ERROR_INVALID_PARAMETER );
		return FALSE;
	}

	/* Check if this is a user defined control code for a VxD */
	if( HIWORD( dwIoControlCode ) == 0 )
	{
		if ( info->deviceio )
		{
			return info->deviceio( dwIoControlCode, 
                                        lpvInBuffer, cbInBuffer, 
                                        lpvOutBuffer, cbOutBuffer, 
                                        lpcbBytesReturned, lpOverlapped );
		}
		else
		{
			/* FIXME: Set appropriate error */
			FIXME( "Unimplemented control %ld for VxD device %s\n", 
                               dwIoControlCode, info->name ? info->name : "???" );
		}
	}
	else
	{
		switch( dwIoControlCode )
		{
		case FSCTL_DELETE_REPARSE_POINT:
		case FSCTL_DISMOUNT_VOLUME:
		case FSCTL_GET_COMPRESSION:
		case FSCTL_GET_REPARSE_POINT:
		case FSCTL_LOCK_VOLUME:
		case FSCTL_QUERY_ALLOCATED_RANGES:
		case FSCTL_SET_COMPRESSION:
		case FSCTL_SET_REPARSE_POINT:
		case FSCTL_SET_SPARSE:
		case FSCTL_SET_ZERO_DATA:
		case FSCTL_UNLOCK_VOLUME:
		case IOCTL_DISK_CHECK_VERIFY:
		case IOCTL_DISK_EJECT_MEDIA:
		case IOCTL_DISK_FORMAT_TRACKS:
		case IOCTL_DISK_GET_DRIVE_GEOMETRY:
		case IOCTL_DISK_GET_DRIVE_LAYOUT:
		case IOCTL_DISK_GET_MEDIA_TYPES:
		case IOCTL_DISK_GET_PARTITION_INFO:
		case IOCTL_DISK_LOAD_MEDIA:
		case IOCTL_DISK_MEDIA_REMOVAL:
		case IOCTL_DISK_PERFORMANCE:
		case IOCTL_DISK_REASSIGN_BLOCKS:
		case IOCTL_DISK_SET_DRIVE_LAYOUT:
		case IOCTL_DISK_SET_PARTITION_INFO:
		case IOCTL_DISK_VERIFY:
		case IOCTL_SERIAL_LSRMST_INSERT:
		case IOCTL_STORAGE_CHECK_VERIFY:
		case IOCTL_STORAGE_EJECT_MEDIA:
		case IOCTL_STORAGE_GET_MEDIA_TYPES:
		case IOCTL_STORAGE_LOAD_MEDIA:
		case IOCTL_STORAGE_MEDIA_REMOVAL:
    			FIXME( "unimplemented dwIoControlCode=%08lx\n", dwIoControlCode);
    			SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    			return FALSE;
    			break;
		default:
    			FIXME( "ignored dwIoControlCode=%08lx\n",dwIoControlCode);
    			SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    			return FALSE;
    			break;
		}
	}
   	return FALSE;
}
 
/***********************************************************************
 *           DeviceIo_VTDAPI
 */
static BOOL DeviceIo_VTDAPI(DWORD dwIoControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped)
{
    BOOL retv = TRUE;

    switch (dwIoControlCode)
    {
    case 5:
        if (lpvOutBuffer && (cbOutBuffer>=4))
            *(DWORD*)lpvOutBuffer = timeGetTime();

        if (lpcbBytesReturned)
            *lpcbBytesReturned = 4;

        break;

    default:
        FIXME( "Control %ld not implemented\n", dwIoControlCode);
        retv = FALSE;
        break;
    }

    return retv;
}





/***********************************************************************
 *           VxDCall                   (KERNEL32.[1-9])
 */
static DWORD VxDCall( DWORD service, ... )
{
    DWORD ret = 0xffffffff; /* FIXME */
    int i;

    TRACE( "(%08lx, ...)\n", service);

    for (i = 0; VxDList[i].name; i++)
        if (VxDList[i].id == HIWORD(service))
            break;

    if (!VxDList[i].name)
        FIXME( "Unknown VxD (%08lx)\n", service);
    else if (!VxDList[i].vxdcall)
        FIXME( "Unimplemented VxD (%08lx)\n", service);
    else
    {
        va_list args;
        va_start( args, service );
        ret = VxDList[i].vxdcall( service, args );
        va_end( args );
    }
    return ret;
}

DWORD WINAPI VxDCall0( DWORD service )
{
    return VxDCall( service );
}

DWORD WINAPI VxDCall1( DWORD service, DWORD arg1 )
{
    return VxDCall( service, arg1 );
}

DWORD WINAPI VxDCall2( DWORD service, DWORD arg1, DWORD arg2 )
{
    return VxDCall( service, arg1, arg2 );
}

DWORD WINAPI VxDCall3( DWORD service, DWORD arg1, DWORD arg2, DWORD arg3 )
{
    return VxDCall( service, arg1, arg2, arg3 );
}

DWORD WINAPI VxDCall4( DWORD service, DWORD arg1, DWORD arg2, DWORD arg3, DWORD arg4 )
{
    return VxDCall( service, arg1, arg2, arg3, arg4 );
}

DWORD WINAPI VxDCall5( DWORD service, DWORD arg1, DWORD arg2, DWORD arg3,
                       DWORD arg4, DWORD arg5 )
{
    return VxDCall( service, arg1, arg2, arg3, arg4, arg5 );
}

DWORD WINAPI VxDCall6( DWORD service, DWORD arg1, DWORD arg2, DWORD arg3,
                       DWORD arg4, DWORD arg5, DWORD arg6 )
{
    return VxDCall( service, arg1, arg2, arg3, arg4, arg5, arg6 );
}

DWORD WINAPI VxDCall7( DWORD service, DWORD arg1, DWORD arg2, DWORD arg3,
                       DWORD arg4, DWORD arg5, DWORD arg6, DWORD arg7 )
{
    return VxDCall( service, arg1, arg2, arg3, arg4, arg5, arg6, arg7 );
}

DWORD WINAPI VxDCall8( DWORD service, DWORD arg1, DWORD arg2, DWORD arg3,
                       DWORD arg4, DWORD arg5, DWORD arg6, DWORD arg7, DWORD arg8 )
{
    return VxDCall( service, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8 );
}

/***********************************************************************
 *           VxDCall_VMM
 */
static DWORD VxDCall_VMM( DWORD service, va_list args )
{
    switch ( LOWORD(service) )
    {
    case 0x0011:  /* RegOpenKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszSubKey = va_arg( args, LPCSTR );
        LPHKEY  retkey     = va_arg( args, LPHKEY );
        return RegOpenKeyA( hkey, lpszSubKey, retkey );
    }

    case 0x0012:  /* RegCreateKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszSubKey = va_arg( args, LPCSTR );
        LPHKEY  retkey     = va_arg( args, LPHKEY );
        return RegCreateKeyA( hkey, lpszSubKey, retkey );
    }

    case 0x0013:  /* RegCloseKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        return RegCloseKey( hkey );
    }

    case 0x0014:  /* RegDeleteKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszSubKey = va_arg( args, LPCSTR );
        return RegDeleteKeyA( hkey, lpszSubKey );
    }

    case 0x0015:  /* RegSetValue */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszSubKey = va_arg( args, LPCSTR );
        DWORD   dwType     = va_arg( args, DWORD );
        LPCSTR  lpszData   = va_arg( args, LPCSTR );
        DWORD   cbData     = va_arg( args, DWORD );
        return RegSetValueA( hkey, lpszSubKey, dwType, lpszData, cbData );
    }

    case 0x0016:  /* RegDeleteValue */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPSTR   lpszValue  = va_arg( args, LPSTR );
        return RegDeleteValueA( hkey, lpszValue );
    }

    case 0x0017:  /* RegQueryValue */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPSTR   lpszSubKey = va_arg( args, LPSTR );
        LPSTR   lpszData   = va_arg( args, LPSTR );
        LPDWORD lpcbData   = va_arg( args, LPDWORD );
        return RegQueryValueA( hkey, lpszSubKey, lpszData, lpcbData );
    }

    case 0x0018:  /* RegEnumKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        DWORD   iSubkey    = va_arg( args, DWORD );
        LPSTR   lpszName   = va_arg( args, LPSTR );
        DWORD   lpcchName  = va_arg( args, DWORD );
        return RegEnumKeyA( hkey, iSubkey, lpszName, lpcchName );
    }

    case 0x0019:  /* RegEnumValue */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        DWORD   iValue     = va_arg( args, DWORD );
        LPSTR   lpszValue  = va_arg( args, LPSTR );
        LPDWORD lpcchValue = va_arg( args, LPDWORD );
        LPDWORD lpReserved = va_arg( args, LPDWORD );
        LPDWORD lpdwType   = va_arg( args, LPDWORD );
        LPBYTE  lpbData    = va_arg( args, LPBYTE );
        LPDWORD lpcbData   = va_arg( args, LPDWORD );
        return RegEnumValueA( hkey, iValue, lpszValue, lpcchValue, 
                              lpReserved, lpdwType, lpbData, lpcbData );
    }

    case 0x001A:  /* RegQueryValueEx */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPSTR   lpszValue  = va_arg( args, LPSTR );
        LPDWORD lpReserved = va_arg( args, LPDWORD );
        LPDWORD lpdwType   = va_arg( args, LPDWORD );
        LPBYTE  lpbData    = va_arg( args, LPBYTE );
        LPDWORD lpcbData   = va_arg( args, LPDWORD );
        return RegQueryValueExA( hkey, lpszValue, lpReserved, 
                                 lpdwType, lpbData, lpcbData );
    }

    case 0x001B:  /* RegSetValueEx */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPSTR   lpszValue  = va_arg( args, LPSTR );
        DWORD   dwReserved = va_arg( args, DWORD );
        DWORD   dwType     = va_arg( args, DWORD );
        LPBYTE  lpbData    = va_arg( args, LPBYTE );
        DWORD   cbData     = va_arg( args, DWORD );
        return RegSetValueExA( hkey, lpszValue, dwReserved, 
                               dwType, lpbData, cbData );
    }

    case 0x001C:  /* RegFlushKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        return RegFlushKey( hkey );
    }

    case 0x001D:  /* RegQueryInfoKey */
    {
        /* NOTE: This VxDCall takes only a subset of the parameters that the
                 corresponding Win32 API call does. The implementation in Win95
                 ADVAPI32 sets all output parameters not mentioned here to zero. */

        HKEY    hkey              = va_arg( args, HKEY );
        LPDWORD lpcSubKeys        = va_arg( args, LPDWORD );
        LPDWORD lpcchMaxSubKey    = va_arg( args, LPDWORD );
        LPDWORD lpcValues         = va_arg( args, LPDWORD );
        LPDWORD lpcchMaxValueName = va_arg( args, LPDWORD );
        LPDWORD lpcchMaxValueData = va_arg( args, LPDWORD );
        return RegQueryInfoKeyA( hkey, NULL, NULL, NULL, lpcSubKeys, lpcchMaxSubKey,
                                 NULL, lpcValues, lpcchMaxValueName, lpcchMaxValueData,
                                 NULL, NULL );
    }

    case 0x0021:  /* RegLoadKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszSubKey = va_arg( args, LPCSTR );
        LPCSTR  lpszFile   = va_arg( args, LPCSTR );
        return RegLoadKeyA( hkey, lpszSubKey, lpszFile );
    }

    case 0x0022:  /* RegUnLoadKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszSubKey = va_arg( args, LPCSTR );
        return RegUnLoadKeyA( hkey, lpszSubKey );
    }

    case 0x0023:  /* RegSaveKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszFile   = va_arg( args, LPCSTR );
        LPSECURITY_ATTRIBUTES sa = va_arg( args, LPSECURITY_ATTRIBUTES );
        return RegSaveKeyA( hkey, lpszFile, sa );
    }

#if 0 /* Functions are not yet implemented in misc/registry.c */
    case 0x0024:  /* RegRemapPreDefKey */
    case 0x0026:  /* RegQueryMultipleValues */
#endif

    case 0x0027:  /* RegReplaceKey */
    {
        HKEY    hkey       = va_arg( args, HKEY );
        LPCSTR  lpszSubKey = va_arg( args, LPCSTR );
        LPCSTR  lpszNewFile= va_arg( args, LPCSTR );
        LPCSTR  lpszOldFile= va_arg( args, LPCSTR );
        return RegReplaceKeyA( hkey, lpszSubKey, lpszNewFile, lpszOldFile );
    }

    default:
        if (LOWORD(service) < N_VMM_SERVICE)
            FIXME( "Unimplemented service %s (%08lx)\n",
                          VMM_Service_Name[LOWORD(service)], service);
        else
            FIXME( "Unknown service %08lx\n", service);
        break;
    }

    return 0xffffffff;  /* FIXME */
}

/***********************************************************************
 *           DeviceIo_IFSMgr
 * NOTES
 *   The ioctls is used by 'MSNET32.DLL'.
 *
 *   I have been unable to uncover any documentation about the ioctls so 
 *   the implementation of the cases IFS_IOCTL_21 and IFS_IOCTL_2F are
 *   based on a resonable guesses on information found in the Windows 95 DDK.
 *   
 */

/*
 * IFSMgr DeviceIO service
 */

#define IFS_IOCTL_21                100
#define IFS_IOCTL_2F                101
#define	IFS_IOCTL_GET_RES           102
#define IFS_IOCTL_GET_NETPRO_NAME_A 103

struct win32apireq {
	unsigned long 	ar_proid;
	unsigned long  	ar_eax;		
	unsigned long  	ar_ebx;	
	unsigned long  	ar_ecx;	
	unsigned long  	ar_edx;	
	unsigned long  	ar_esi;	
	unsigned long  	ar_edi;
	unsigned long  	ar_ebp;		
	unsigned short 	ar_error;
	unsigned short  ar_pad;
};

static void win32apieq_2_CONTEXT(struct win32apireq *pIn,CONTEXT *pCxt)
{
	memset(pCxt,0,sizeof(CONTEXT));

	pCxt->ContextFlags=CONTEXT_INTEGER|CONTEXT_CONTROL;
	pCxt->Eax = pIn->ar_eax;
	pCxt->Ebx = pIn->ar_ebx;
	pCxt->Ecx = pIn->ar_ecx;
	pCxt->Edx = pIn->ar_edx;
	pCxt->Esi = pIn->ar_esi;
	pCxt->Edi = pIn->ar_edi;

	/* FIXME: Only partial CONTEXT_CONTROL */
	pCxt->Ebp = pIn->ar_ebp;

	/* FIXME: pIn->ar_proid ignored */
	/* FIXME: pIn->ar_error ignored */
	/* FIXME: pIn->ar_pad ignored */
}

static void CONTEXT_2_win32apieq(CONTEXT *pCxt,struct win32apireq *pOut)
{
	memset(pOut,0,sizeof(struct win32apireq));

	pOut->ar_eax = pCxt->Eax;
	pOut->ar_ebx = pCxt->Ebx;
	pOut->ar_ecx = pCxt->Ecx;
	pOut->ar_edx = pCxt->Edx;
	pOut->ar_esi = pCxt->Esi;
	pOut->ar_edi = pCxt->Edi;

	/* FIXME: Only partial CONTEXT_CONTROL */
	pOut->ar_ebp = pCxt->Ebp;

	/* FIXME: pOut->ar_proid ignored */
	/* FIXME: pOut->ar_error ignored */
	/* FIXME: pOut->ar_pad ignored */
}

static BOOL DeviceIo_IFSMgr(DWORD dwIoControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped)
{
    BOOL retv = TRUE;
	TRACE("(%ld,%p,%ld,%p,%ld,%p,%p): stub\n",
			dwIoControlCode,
			lpvInBuffer,cbInBuffer,
			lpvOutBuffer,cbOutBuffer,
			lpcbBytesReturned,
			lpOverlapped);

    switch (dwIoControlCode)
    {
	case IFS_IOCTL_21:
	case IFS_IOCTL_2F:{
		CONTEXT cxt;
		struct win32apireq *pIn=(struct win32apireq *) lpvInBuffer;
		struct win32apireq *pOut=(struct win32apireq *) lpvOutBuffer;

		TRACE(
			"Control '%s': "
			"proid=0x%08lx, eax=0x%08lx, ebx=0x%08lx, ecx=0x%08lx, "
			"edx=0x%08lx, esi=0x%08lx, edi=0x%08lx, ebp=0x%08lx, "
			"error=0x%04x, pad=0x%04x\n",
			(dwIoControlCode==IFS_IOCTL_21)?"IFS_IOCTL_21":"IFS_IOCTL_2F",
			pIn->ar_proid, pIn->ar_eax, pIn->ar_ebx, pIn->ar_ecx,
			pIn->ar_edx, pIn->ar_esi, pIn->ar_edi, pIn->ar_ebp,
			pIn->ar_error, pIn->ar_pad
		);				
		
		win32apieq_2_CONTEXT(pIn,&cxt);

		if(dwIoControlCode==IFS_IOCTL_21)
		{
			DOS3Call(&cxt); /* Call int 21h */
		} else {
			INT_Int2fHandler(&cxt); /* Call int 2Fh */
		}
		
		CONTEXT_2_win32apieq(&cxt,pOut);
			
        retv = TRUE;
	} break;
	case IFS_IOCTL_GET_RES:{
        FIXME( "Control 'IFS_IOCTL_GET_RES' not implemented\n");
        retv = FALSE;
	} break;
	case IFS_IOCTL_GET_NETPRO_NAME_A:{
        FIXME( "Control 'IFS_IOCTL_GET_NETPRO_NAME_A' not implemented\n");
        retv = FALSE;
	} break;	 
    default:
        FIXME( "Control %ld not implemented\n", dwIoControlCode);
        retv = FALSE;
    }

    return retv;
}

 
/***********************************************************************
 *           DeviceIo_VWin32
 */

static void DIOCRegs_2_CONTEXT( DIOC_REGISTERS *pIn, CONTEXT *pCxt )
{
    memset( pCxt, 0, sizeof(CONTEXT) );
    /* Note: segment registers == 0 means that CTX_SEG_OFF_TO_LIN
             will interpret 32-bit register contents as linear pointers */

    pCxt->ContextFlags=CONTEXT_INTEGER|CONTEXT_CONTROL;
    pCxt->Eax = pIn->reg_EAX;
    pCxt->Ebx = pIn->reg_EBX;
    pCxt->Ecx = pIn->reg_ECX;
    pCxt->Edx = pIn->reg_EDX;
    pCxt->Esi = pIn->reg_ESI;
    pCxt->Edi = pIn->reg_EDI;

    /* FIXME: Only partial CONTEXT_CONTROL */
    pCxt->EFlags = pIn->reg_Flags;
}

static void CONTEXT_2_DIOCRegs( CONTEXT *pCxt, DIOC_REGISTERS *pOut )
{
    memset( pOut, 0, sizeof(DIOC_REGISTERS) );

    pOut->reg_EAX = pCxt->Eax;
    pOut->reg_EBX = pCxt->Ebx;
    pOut->reg_ECX = pCxt->Ecx;
    pOut->reg_EDX = pCxt->Edx;
    pOut->reg_ESI = pCxt->Esi;
    pOut->reg_EDI = pCxt->Edi;

    /* FIXME: Only partial CONTEXT_CONTROL */
    pOut->reg_Flags = pCxt->EFlags;
}


static BOOL DeviceIo_VWin32(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped)
{
    BOOL retv = TRUE;

    switch (dwIoControlCode)
    {
    case VWIN32_DIOC_DOS_IOCTL:
    case VWIN32_DIOC_DOS_INT13:
    case VWIN32_DIOC_DOS_INT25:
    case VWIN32_DIOC_DOS_INT26:
    {
        CONTEXT cxt;
        DIOC_REGISTERS *pIn  = (DIOC_REGISTERS *)lpvInBuffer;
        DIOC_REGISTERS *pOut = (DIOC_REGISTERS *)lpvOutBuffer;

        TRACE( "Control '%s': "
               "eax=0x%08lx, ebx=0x%08lx, ecx=0x%08lx, "
               "edx=0x%08lx, esi=0x%08lx, edi=0x%08lx ",
               (dwIoControlCode == VWIN32_DIOC_DOS_IOCTL)? "VWIN32_DIOC_DOS_IOCTL" :
               (dwIoControlCode == VWIN32_DIOC_DOS_INT13)? "VWIN32_DIOC_DOS_INT13" :
               (dwIoControlCode == VWIN32_DIOC_DOS_INT25)? "VWIN32_DIOC_DOS_INT25" :
               (dwIoControlCode == VWIN32_DIOC_DOS_INT26)? "VWIN32_DIOC_DOS_INT26" : "???",
               pIn->reg_EAX, pIn->reg_EBX, pIn->reg_ECX,
               pIn->reg_EDX, pIn->reg_ESI, pIn->reg_EDI );

        DIOCRegs_2_CONTEXT( pIn, &cxt );

        switch (dwIoControlCode)
        {
        case VWIN32_DIOC_DOS_IOCTL: DOS3Call( &cxt ); break; /* Call int 21h */
        case VWIN32_DIOC_DOS_INT13: INT_Int13Handler( &cxt ); break;
        case VWIN32_DIOC_DOS_INT25: INT_Int25Handler( &cxt ); break;
        case VWIN32_DIOC_DOS_INT26: INT_Int26Handler( &cxt ); break;
        }

        CONTEXT_2_DIOCRegs( &cxt, pOut );
    }
    break;

    case VWIN32_DIOC_SIMCTRLC:
        FIXME( "Control VWIN32_DIOC_SIMCTRLC not implemented\n");
        retv = FALSE;
        break;

    default:
        FIXME( "Unknown Control %ld\n", dwIoControlCode);
        retv = FALSE;
        break;
    }

    return retv;
}

/* this is the main multimedia device loader */
static BOOL DeviceIo_MMDEVLDR(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped)
{
	FIXME("(%ld,%p,%ld,%p,%ld,%p,%p): stub\n",
	    dwIoControlCode,
	    lpvInBuffer,cbInBuffer,
	    lpvOutBuffer,cbOutBuffer,
	    lpcbBytesReturned,
	    lpOverlapped
	);
	switch (dwIoControlCode) {
	case 5:
		/* Hmm. */
		*(DWORD*)lpvOutBuffer=0;
		*lpcbBytesReturned=4;
		return TRUE;
	}
	return FALSE;
}
/* this is used by some Origin games */
static BOOL DeviceIo_MONODEBG(DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped)
{
	switch (dwIoControlCode) {
	case 1:	/* version */
		*(LPDWORD)lpvOutBuffer = 0x20004; /* WC SecretOps */
		break;
	case 9: /* debug output */
		fprintf(stderr,"MONODEBG: %s\n",debugstr_a(lpvInBuffer));
		break;
	default:
		FIXME("(%ld,%p,%ld,%p,%ld,%p,%p): stub\n",
			dwIoControlCode,
			lpvInBuffer,cbInBuffer,
			lpvOutBuffer,cbOutBuffer,
			lpcbBytesReturned,
			lpOverlapped
		);
		break;
	}
	return TRUE;
}
/* pccard */
static BOOL DeviceIo_PCCARD (DWORD dwIoControlCode, 
			      LPVOID lpvInBuffer, DWORD cbInBuffer,
			      LPVOID lpvOutBuffer, DWORD cbOutBuffer,
			      LPDWORD lpcbBytesReturned,
			      LPOVERLAPPED lpOverlapped)
{
	switch (dwIoControlCode) {
	case 0x0000: /* PCCARD_Get_Version */
	case 0x0001: /* PCCARD_Card_Services */
	default:
		FIXME( "(%ld,%p,%ld,%p,%ld,%p,%p): stub\n",
			dwIoControlCode,
			lpvInBuffer,cbInBuffer,
			lpvOutBuffer,cbOutBuffer,
			lpcbBytesReturned,
			lpOverlapped
		);
		break;
	}
	return FALSE;
}

DWORD	WINAPI	OpenVxDHandle(DWORD pmt)
{
	FIXME( "(0x%08lx) stub!\n", pmt);
	return 0;
}
