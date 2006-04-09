/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "smpd.h"

static SMPD_BOOL UnmapDrive(char *pszDrive, char *pszError, int maxerrlength);
static SMPD_BOOL MapDrive(char *pszDrive, char *pszShare, char *pszAccount, char *pszPassword, char *pszError, int maxerrlength);

typedef struct DriveMapStruct
{
    int nRefCount;
    char pszDrive[10];
    char pszShare[MAX_PATH];
    SMPD_BOOL bUnmap;
    struct DriveMapStruct *pNext;
} DriveMapStruct;

DriveMapStruct * allocate_DriveMapStruct()
{
    DriveMapStruct *p;
    p = (DriveMapStruct*)malloc(sizeof(DriveMapStruct));
    if (p == NULL)
	return NULL;
    p->nRefCount = 1;
    p->pszDrive[0] = '\0';
    p->pNext = NULL;
    p->bUnmap = SMPD_TRUE;
    return p;
}

static DriveMapStruct *g_pDriveList = NULL;

static SMPD_BOOL AlreadyMapped(char *pszDrive, char *pszShare, SMPD_BOOL *pMatched)
{
    DriveMapStruct *p;
    if (g_pDriveList == NULL)
	return SMPD_FALSE;
    p = g_pDriveList;
    while (p)
    {
	if (pszDrive[0] == p->pszDrive[0])
	{
	    if ((stricmp(pszShare, p->pszShare) == 0))
	    {
		p->nRefCount++;
		*pMatched = SMPD_TRUE;
	    }
	    else
		*pMatched = SMPD_FALSE;
	    return SMPD_TRUE;
	}
	p = p->pNext;
    }
    return SMPD_FALSE;
}

static SMPD_BOOL CompareHosts(char *pszHost1, char *pszHost2)
{
    unsigned long ip1, ip2;
    struct hostent *pH;

    pH = gethostbyname(pszHost1);
    if (pH == NULL)
	return SMPD_FALSE;

    ip1 = *(unsigned long*)(pH->h_addr_list[0]);

    pH = gethostbyname(pszHost2);
    if (pH == NULL)
	return SMPD_FALSE;

    ip2 = *(unsigned long*)(pH->h_addr_list[0]);

    return (ip1 == ip2);
}

static BOOL EnumerateDisksFunc(LPNETRESOURCE lpnr, DWORD dwScope, DWORD dwType, char *pszDrive, char *pszShare, SMPD_BOOL *pbFound, SMPD_BOOL *pbMatched)
{
    DWORD dwResult, dwResultEnum;
    HANDLE hEnum;
    DWORD cbBuffer = 16384;      /* 16K is a good size */
    DWORD cEntries = (DWORD)-1;         /* enumerate all possible entries */
    LPNETRESOURCE lpnrLocal;     /* pointer to enumerated structures */
    DWORD i;

    dwResult = WNetOpenEnum(
	dwScope,
	dwType,
	0,        /* enumerate all resources */
	lpnr,     /* NULL first time the function is called */
	&hEnum);  /* handle to the resource */

    if (dwResult != NO_ERROR)
	return FALSE;

    lpnrLocal = (LPNETRESOURCE) GlobalAlloc(GPTR, cbBuffer);

    do
    {  
	ZeroMemory(lpnrLocal, cbBuffer);
	dwResultEnum = WNetEnumResource(
	    hEnum,      /* resource handle */
	    &cEntries,  /* defined locally as -1 */
	    lpnrLocal,  /* LPNETRESOURCE */
	    &cbBuffer); /* buffer size */
	/* If the call succeeds, loop through the structures. */
	if (dwResultEnum == NO_ERROR)
	{
	    for(i = 0; i < cEntries; i++)
	    {
		if (lpnrLocal[i].lpLocalName && lpnrLocal[i].lpRemoteName)
		{
		    if (toupper(*lpnrLocal[i].lpLocalName) == *pszDrive)
		    {
			*pbFound = SMPD_TRUE;
			if ((stricmp(lpnrLocal[i].lpRemoteName, pszShare) == 0))
			{
			    *pbMatched = SMPD_TRUE;
			}
			else
			{
			    char *pPath1, *pPath2;
			    pPath1 = strstr(&lpnrLocal[i].lpRemoteName[2], "\\");
			    if (pPath1 != NULL)
			    {
				pPath1++; /* advance over the \ character */
				pPath2 = strstr(&pszShare[2], "\\");
				if (pPath2 != NULL)
				{
				    pPath2++; /* advance over the \ character */
				    if (stricmp(pPath1, pPath2) == 0)
				    {
					char pszHost1[50], pszHost2[50];
					int nLength1, nLength2;
					nLength1 = (int)(pPath1 - &lpnrLocal[i].lpRemoteName[2] - 1);
					nLength2 = (int)(pPath2 - &pszShare[2] - 1);
					strncpy(pszHost1, &lpnrLocal[i].lpRemoteName[2], nLength1);
					strncpy(pszHost2, &pszShare[2], nLength2);
					pszHost1[nLength1] = '\0';
					pszHost2[nLength2] = '\0';
					if (CompareHosts(pszHost1, pszHost2))
					    *pbMatched = SMPD_TRUE;
				    }
				}
			    }
			}
		    }
		}

		/* If the NETRESOURCE structure represents a container resource,  */
		/*  call the EnumerateDisksFunc function recursively. */
		if (RESOURCEUSAGE_CONTAINER == (lpnrLocal[i].dwUsage & RESOURCEUSAGE_CONTAINER))
		    EnumerateDisksFunc(&lpnrLocal[i], dwScope, dwType, pszDrive, pszShare, pbFound, pbMatched);
	    }
	}
	else if (dwResultEnum != ERROR_NO_MORE_ITEMS)
	{
	    break;
	}
    } while(dwResultEnum != ERROR_NO_MORE_ITEMS);

    GlobalFree((HGLOBAL)lpnrLocal);

    dwResult = WNetCloseEnum(hEnum);

    if (dwResult != NO_ERROR)
	return FALSE;

    return TRUE;
}

static SMPD_BOOL MatchesExistingMapping(char *pszDrive, char *pszShare)
{
    SMPD_BOOL bFound = SMPD_FALSE;
    SMPD_BOOL bMatched = SMPD_FALSE;
    char ch;

    if (pszDrive == NULL || pszShare == NULL)
	return SMPD_FALSE;

    ch = (char)(toupper(*pszDrive));
    EnumerateDisksFunc(NULL, RESOURCE_CONNECTED, RESOURCETYPE_DISK, &ch, pszShare, &bFound, &bMatched);
    if (bMatched)
	return SMPD_TRUE;
    EnumerateDisksFunc(NULL, RESOURCE_REMEMBERED, RESOURCETYPE_DISK, &ch, pszShare, &bFound, &bMatched);
    if (bMatched)
	return SMPD_TRUE;

    /* If it was not found, assume that it matches */
    return !bFound;
}

static void RemoveDriveStruct(char *pszDrive)
{
    DriveMapStruct *p, *pTrailer;

    pTrailer = p = g_pDriveList;
    while (p)
    {
	if (p->pszDrive[0] == pszDrive[0])
	{
	    p->nRefCount--;
	    if (p->nRefCount == 0)
	    {
		if (pTrailer != p)
		    pTrailer->pNext = p->pNext;
		if (g_pDriveList == p)
		    g_pDriveList = g_pDriveList->pNext;
		free(p);
	    }
	    return;
	}
	if (pTrailer != p)
	    pTrailer = pTrailer->pNext;
	p = p->pNext;
    }
}

#undef FCNAME
#define FCNAME "smpd_finalize_drive_maps"
void smpd_finalize_drive_maps()
{
    char err_msg[256];
    char temp[MAX_PATH+2];

    smpd_enter_fn(FCNAME);
    while (g_pDriveList)
    {
	snprintf(temp, MAX_PATH+2, "%c:%s", g_pDriveList->pszDrive, g_pDriveList->pszShare);
	if (!UnmapDrive(temp, err_msg, 256))
	    break;
    }
    while (g_pDriveList)
    {
	RemoveDriveStruct(g_pDriveList->pszDrive);
    }
    smpd_exit_fn(FCNAME);
}

static SMPD_BOOL ParseDriveShareAccountPassword(char *str, char *pszDrive, char *pszShare, char *pszAccount, char *pszPassword)
{
    pszDrive[0] = str[0];
    pszDrive[1] = ':';
    pszDrive[2] = '\0';
    while (*str != '\\')
	str++;
    if (strstr(str, ":"))
    {
	while (*str != ':')
	    *pszShare++ = *str++;
	*pszShare = '\0';
	str++;
	if (!strstr(str, ":"))
	    return SMPD_FALSE;
	while (*str != ':')
	    *pszAccount++ = *str++;
	*pszAccount = '\0';
	str++;
	strcpy(pszPassword, str);
    }
    else
    {
	strcpy(pszShare, str);
	*pszAccount = '\0';
    }
    return SMPD_TRUE;
}

#undef FCNAME
#define FCNAME "smpd_map_user_drives"
SMPD_BOOL smpd_map_user_drives(char *pszMap, char *pszAccount, char *pszPassword, char *pszError, int maxerrlength)
{
    char pszDrive[3];
    char pszShare[MAX_PATH];
    char ipszAccount[SMPD_MAX_ACCOUNT_LENGTH];
    char ipszPassword[SMPD_MAX_PASSWORD_LENGTH];
    char *token;
    char *temp = strdup(pszMap);

    smpd_enter_fn(FCNAME);

    token = strtok(temp, ";\n");
    if (token == NULL)
    {
	smpd_exit_fn(FCNAME);
	return SMPD_TRUE;
    }
    while (token != NULL)
    {
	ipszAccount[0] = '\0';
	if (ParseDriveShareAccountPassword(token, pszDrive, pszShare, ipszAccount, ipszPassword))
	{
	    if (ipszAccount[0]  != '\0')
	    {
		if (!MapDrive(pszDrive, pszShare, ipszAccount, ipszPassword, pszError, maxerrlength))
		{
		    free(temp);
		    smpd_err_printf("MapUserDrives: MapDrive(%s, %s, %s, ... ) failed, %s\n", pszDrive, pszShare, ipszAccount, pszError);
		    smpd_exit_fn(FCNAME);
		    return SMPD_FALSE;
		}
	    }
	    else
	    {
		if (!MapDrive(pszDrive, pszShare, pszAccount, pszPassword, pszError, maxerrlength))
		{
		    free(temp);
		    smpd_err_printf("MapUserDrives: MapDrive(%s, %s, %s, ... ) failed, %s\n", pszDrive, pszShare, pszAccount, pszError);
		    smpd_exit_fn(FCNAME);
		    return SMPD_FALSE;
		}
	    }
	}
	token = strtok(NULL, ";\n");
    }
    free(temp);

    smpd_exit_fn(FCNAME);
    return SMPD_TRUE;
}

#undef FCNAME
#define FCNAME "smpd_unmap_user_drives"
SMPD_BOOL smpd_unmap_user_drives(char *pszMap)
{
    char pszError[256];
    char pszDrive[3];
    char pszShare[MAX_PATH];
    char pszAccount[SMPD_MAX_ACCOUNT_LENGTH];
    char pszPassword[SMPD_MAX_PASSWORD_LENGTH];
    char *temp = strdup(pszMap);
    char *token;

    smpd_enter_fn(FCNAME);

    token = strtok(temp, ";\n");
    if (token == NULL)
    {
	free(temp);
	smpd_exit_fn(FCNAME);
	return SMPD_TRUE;
    }
    while (token != NULL)
    {
	if (ParseDriveShareAccountPassword(token, pszDrive, pszShare, pszAccount, pszPassword))
	{
	    if (!UnmapDrive(pszDrive, pszError, 256))
	    {
		free(temp);
		smpd_exit_fn(FCNAME);
		return SMPD_FALSE;
	    }
	}
	token = strtok(NULL, ";\n");
    }
    free(temp);

    smpd_exit_fn(FCNAME);
    return SMPD_FALSE;
}

static SMPD_BOOL MapDrive(char *pszDrive, char *pszShare, char *pszAccount, char *pszPassword, char *pszError, int maxerrlength)
{
    char pszDriveLetter[3];
    DWORD dwResult;
    char pszName[1024];
    char pszProvider[256];
    NETRESOURCE net;
    SMPD_BOOL bMatched;

    if (pszDrive == NULL)
    {
	strcpy(pszError, "Invalid drive string");
	return SMPD_FALSE;
    }
    pszDriveLetter[0] = pszDrive[0];
    pszDriveLetter[1] = ':';
    pszDriveLetter[2] = '\0';

    memset(&net, 0, sizeof(NETRESOURCE));
    net.lpLocalName = pszDriveLetter;
    net.lpRemoteName = pszShare;
    net.dwType = RESOURCETYPE_DISK;
    net.lpProvider = NULL;

    if (AlreadyMapped(pszDriveLetter, pszShare, &bMatched))
    {
	if (bMatched)
	    return SMPD_TRUE;
	sprintf(pszError, "Drive %s already mapped.", pszDrive);
	smpd_err_printf("MapDrive failed, drive is already mapped\n");
	return SMPD_FALSE;
    }

    if (pszAccount != NULL)
    {
	if (*pszAccount == '\0')
	{
	    /* Change empty username to NULL pointer to work with WNetAddConnection2. */
	    pszAccount = NULL;
	    pszPassword = NULL;
	}
    }

    dwResult = WNetAddConnection2(&net, pszPassword, pszAccount, CONNECT_REDIRECT);

    if (dwResult == NO_ERROR)
    {
	DriveMapStruct *p = allocate_DriveMapStruct();
	if (p == NULL)
	{
	    smpd_err_printf("unable to allocate a drive map structure.\n");
	    return SMPD_FALSE;
	}
	strcpy(p->pszDrive, pszDriveLetter);
	strncpy(p->pszShare, pszShare, MAX_PATH);
	p->pNext = g_pDriveList;
	g_pDriveList = p;
	return SMPD_TRUE;
    }

    switch (dwResult)
    {
    case ERROR_ACCESS_DENIED:
	strcpy(pszError, "Access to the network resource was denied.");
	break;
    case ERROR_ALREADY_ASSIGNED:
	if (MatchesExistingMapping(pszDriveLetter, pszShare))
	{
	    DriveMapStruct *p = allocate_DriveMapStruct();
	    strcpy(p->pszDrive, pszDriveLetter);
	    strncpy(p->pszShare, pszShare, MAX_PATH);
	    p->bUnmap = SMPD_FALSE; /* don't unmap this drive since it was mapped outside mpd */
	    p->pNext = g_pDriveList;
	    g_pDriveList = p;
	    return SMPD_TRUE;
	}
	else
	{
	    sprintf(pszError, "The local device '%s' is already connected to a network resource.", pszDriveLetter);
	}
	break;
    case ERROR_BAD_DEV_TYPE:
	strcpy(pszError, "The type of local device and the type of network resource do not match.");
	break;
    case ERROR_BAD_DEVICE:
	sprintf(pszError, "The value '%s' is invalid.", pszDriveLetter);
	break;
    case ERROR_BAD_NET_NAME:
	sprintf(pszError, "The value '%s' is not acceptable to any network resource provider because the resource name is invalid, or because the named resource cannot be located.", pszShare);
	break;
    case ERROR_BAD_PROFILE:
	strcpy(pszError, "The user profile is in an incorrect format.");
	break;
    case ERROR_BAD_PROVIDER:
	strcpy(pszError, "The value specified by the lpProvider member does not match any provider.");
	break;
    case ERROR_BUSY:
	strcpy(pszError, "The router or provider is busy, possibly initializing. The caller should retry.");
	break;
    case ERROR_CANCELLED:
	strcpy(pszError, "The attempt to make the connection was canceled by the user through a dialog box from one of the network resource providers, or by a called resource.");
	break;
    case ERROR_CANNOT_OPEN_PROFILE:
	strcpy(pszError, "The system is unable to open the user profile to process persistent connections.");
	break;
    case ERROR_DEVICE_ALREADY_REMEMBERED:
	if (MatchesExistingMapping(pszDriveLetter, pszShare))
	{
	    DriveMapStruct *p = allocate_DriveMapStruct();
	    strcpy(p->pszDrive, pszDriveLetter);
	    strncpy(p->pszShare, pszShare, MAX_PATH);
	    p->bUnmap = SMPD_FALSE; /* don't unmap this drive since it was mapped outside mpd */
	    p->pNext = g_pDriveList;
	    g_pDriveList = p;
	    return SMPD_TRUE;
	}
	else
	{
	    sprintf(pszError, "An entry for the device '%s' is already in the user profile.", pszDriveLetter);
	}
	break;
    case ERROR_EXTENDED_ERROR:
	if (WNetGetLastError(&dwResult, pszName, 1024, pszProvider, 256) == NO_ERROR)
	{
	    sprintf(pszError, "'%s' returned this error: %d, %s", pszProvider, dwResult, pszName);
	}
	else
	{
	    strcpy(pszError, "A network-specific error occurred.");
	}
	break;
    case ERROR_INVALID_PASSWORD:
	strcpy(pszError, "The specified password is invalid.");
	break;
    case ERROR_NO_NET_OR_BAD_PATH:
	strcpy(pszError, "The operation could not be completed, either because a network component is not started, or because the specified resource name is not recognized.");
	break;
    case ERROR_NO_NETWORK:
	strcpy(pszError, "The network is unavailable.");
	break;
    default:
	smpd_translate_win_error(dwResult, pszError, maxerrlength, NULL);
	smpd_err_printf("MapDrive: unknown error %d\n", dwResult);
	break;
    }

    smpd_err_printf("MapDrive failed, error: %s\n", pszError);
    return SMPD_FALSE;
}

static SMPD_BOOL UnmapDrive(char *pszDrive, char *pszError, int maxerrlength)
{
    char pszName[1024];
    char pszProvider[256];
    char pszDriveLetter[3];
    DWORD dwResult;
    DriveMapStruct *p = g_pDriveList;

    if (pszDrive == NULL)
    {
	return SMPD_FALSE;
    }
    pszDriveLetter[0] = pszDrive[0];
    pszDriveLetter[1] = ':';
    pszDriveLetter[2] = '\0';

    while (p)
    {
	if (p->pszDrive[0] == pszDrive[0])
	{
	    if (p->nRefCount > 1)
	    {
		p->nRefCount--;
		return SMPD_TRUE;
	    }
	    break;
	}
	p = p->pNext;
    }

    if (p->bUnmap)
    {
	dwResult = WNetCancelConnection2(pszDriveLetter, 
	    CONNECT_UPDATE_PROFILE, /* This option makes sure that the connection is not re-established at the next user logon */
	    TRUE);
    }
    else
    {
	dwResult = NO_ERROR;
    }

    if (dwResult == NO_ERROR)
    {
	RemoveDriveStruct(pszDriveLetter);
	return SMPD_TRUE;
    }

    switch (dwResult)
    {
    case ERROR_BAD_PROFILE:
	strcpy(pszError, "The user profile is in an incorrect format.");
	break;
    case ERROR_CANNOT_OPEN_PROFILE:
	strcpy(pszError, "The system is unable to open the user profile to process persistent connections.");
	break;
    case ERROR_DEVICE_IN_USE:
	strcpy(pszError, "The device is in use by an active process and cannot be disconnected.");
	break;
    case ERROR_EXTENDED_ERROR:
	if (WNetGetLastError(&dwResult, pszName, 1024, pszProvider, 256) == NO_ERROR)
	{
	    sprintf(pszError, "'%s' returned this error: %d, %s", pszProvider, dwResult, pszName);
	}
	else
	{
	    strcpy(pszError, "A network-specific error occurred.");
	}
	break;
    case ERROR_NOT_CONNECTED:
	sprintf(pszError, "'%s' is not a redirected device, or the system is not currently connected to '%s'.", pszDriveLetter, pszDriveLetter);
	break;
    case ERROR_OPEN_FILES:
	strcpy(pszError, "There are open files, the drive cannot be disconnected.");
	break;
    default:
	smpd_translate_win_error(dwResult, pszError, maxerrlength, NULL);
	break;
    }

    return SMPD_FALSE;
}
