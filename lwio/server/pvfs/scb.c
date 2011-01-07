/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4 -*-
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 * Editor Settings: expandtabs and use 4 spaces for indentation */

/*
 * Copyright Likewise Software
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.  You should have received a copy of the GNU General
 * Public License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * LIKEWISE SOFTWARE MAKES THIS SOFTWARE AVAILABLE UNDER OTHER LICENSING
 * TERMS AS WELL.  IF YOU HAVE ENTERED INTO A SEPARATE LICENSE AGREEMENT
 * WITH LIKEWISE SOFTWARE, THEN YOU MAY ELECT TO USE THE SOFTWARE UNDER THE
 * TERMS OF THAT SOFTWARE LICENSE AGREEMENT INSTEAD OF THE TERMS OF THE GNU
 * GENERAL PUBLIC LICENSE, NOTWITHSTANDING THE ABOVE NOTICE.  IF YOU
 * HAVE QUESTIONS, OR WISH TO REQUEST A COPY OF THE ALTERNATE LICENSING
 * TERMS OFFERED BY LIKEWISE SOFTWARE, PLEASE CONTACT LIKEWISE SOFTWARE AT
 * license@likewisesoftware.com
 */

/*
 * Copyright (C) Likewise Software. All rights reserved.
 *
 * Module Name:
 *
 *        scb.c
 *
 * Abstract:
 *
 *        Likewise Posix File System Driver (PVFS)
 *
 *        File Stream Control Block routines
 *
 * Authors: Gerald Carter <gcarter@likewise.com>
 */

#include "pvfs.h"


/*****************************************************************************
 ****************************************************************************/

static
VOID
PvfsFreeSCB(
    PPVFS_SCB pScb
    )
{
    if (pScb)
    {
        if (pScb->pParentScb)
        {
            PvfsReleaseSCB(&pScb->pParentScb);
        }

        if (pScb->pOwnerFcb)
        {
            PvfsRemoveSCBFromFCB(pScb->pOwnerFcb, pScb);
            PvfsReleaseFCB(&pScb->pOwnerFcb);
        }

        RtlCStringFree(&pScb->pszFilename);
        RtlCStringFree(&pScb->pszStreamname);

        pthread_rwlock_destroy(&pScb->rwLock);
        pthread_rwlock_destroy(&pScb->rwCcbLock);
        pthread_rwlock_destroy(&pScb->rwBrlLock);

        PvfsListDestroy(&pScb->pPendingLockQueue);
        PvfsListDestroy(&pScb->pOplockPendingOpsQueue);
        PvfsListDestroy(&pScb->pOplockReadyOpsQueue);
        PvfsListDestroy(&pScb->pOplockList);
        PvfsListDestroy(&pScb->pCcbList);
        PvfsListDestroy(&pScb->pNotifyListIrp);
        PvfsListDestroy(&pScb->pNotifyListBuffer);

        PvfsDestroyCB(&pScb->BaseControlBlock);

        PVFS_FREE(&pScb);

        InterlockedDecrement(&gPvfsScbCount);
    }

    return;
}


/*****************************************************************************
 ****************************************************************************/

static
VOID
PvfsSCBFreeCCB(
    PVOID *ppData
    )
{
    /* This should never be called.  The CCB count has to be 0 when
       we call PvfsFreeSCB() and hence destroy the SCB->pCcbList */

    return;
}


/*****************************************************************************
 ****************************************************************************/

NTSTATUS
PvfsAllocateSCB(
    PPVFS_SCB *ppScb
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    PPVFS_SCB pScb = NULL;

    *ppScb = NULL;

    ntError = PvfsAllocateMemory(
                  (PVOID*)&pScb,
                  sizeof(PVFS_SCB),
                  FALSE);
    BAIL_ON_NT_STATUS(ntError);

    PvfsInitializeCB(&pScb->BaseControlBlock);

    /* Initialize mutexes and refcounts */

    pthread_rwlock_init(&pScb->rwLock, NULL);
    pthread_rwlock_init(&pScb->rwCcbLock, NULL);
    pthread_rwlock_init(&pScb->rwBrlLock, NULL);

    /* Setup FcbList */

    PVFS_INIT_LINKS(&pScb->FcbList);

    /* Setup pendlock byte-range lock queue */

    ntError = PvfsListInit(
                  &pScb->pPendingLockQueue,
                  PVFS_SCB_MAX_PENDING_LOCKS,
                  (PPVFS_LIST_FREE_DATA_FN)PvfsFreePendingLock);
    BAIL_ON_NT_STATUS(ntError);

    /* Oplock pending ops queue */

    ntError = PvfsListInit(
                  &pScb->pOplockPendingOpsQueue,
                  PVFS_SCB_MAX_PENDING_OPERATIONS,
                  (PPVFS_LIST_FREE_DATA_FN)PvfsFreePendingOp);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsListInit(
                  &pScb->pOplockReadyOpsQueue,
                  PVFS_SCB_MAX_PENDING_OPERATIONS,
                  (PPVFS_LIST_FREE_DATA_FN)PvfsFreePendingOp);
    BAIL_ON_NT_STATUS(ntError);

    /* Oplock list and state */

    pScb->bOplockBreakInProgress = FALSE;
    ntError = PvfsListInit(
                  &pScb->pOplockList,
                  0,
                  (PPVFS_LIST_FREE_DATA_FN)PvfsFreeOplockRecord);
    BAIL_ON_NT_STATUS(ntError);

    /* List of CCBs */

    ntError = PvfsListInit(
                  &pScb->pCcbList,
                  0,
                  (PPVFS_LIST_FREE_DATA_FN)PvfsSCBFreeCCB);
    BAIL_ON_NT_STATUS(ntError);

    /* List of Notify requests */

    ntError = PvfsListInit(
                  &pScb->pNotifyListIrp,
                  PVFS_SCB_MAX_PENDING_NOTIFY,
                  (PPVFS_LIST_FREE_DATA_FN)PvfsFreeNotifyRecord);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsListInit(
                  &pScb->pNotifyListBuffer,
                  PVFS_SCB_MAX_PENDING_NOTIFY,
                  (PPVFS_LIST_FREE_DATA_FN)PvfsFreeNotifyRecord);
    BAIL_ON_NT_STATUS(ntError);

    /* Miscellaneous */

    PVFS_CLEAR_FILEID(pScb->FileId);

    pScb->bDeleteOnClose = FALSE;
    pScb->bOplockBreakInProgress = FALSE;
    pScb->pParentScb = NULL;
    pScb->pszFilename = NULL;
    pScb->pszStreamname = NULL;

    *ppScb = pScb;

    InterlockedIncrement(&gPvfsScbCount);

    ntError = STATUS_SUCCESS;

cleanup:
    return ntError;

error:
    if (pScb)
    {
        PvfsFreeSCB(pScb);
    }

    goto cleanup;
}

/*******************************************************
 ******************************************************/

PPVFS_SCB
PvfsReferenceSCB(
    IN PPVFS_SCB pScb
    )
{
    InterlockedIncrement(&pScb->BaseControlBlock.RefCount);

    return pScb;
}

/***********************************************************************
 Make sure to alkways enter this unfunction with thee pScb->ControlMutex
 locked
 **********************************************************************/

static
NTSTATUS
PvfsExecuteDeleteOnClose(
    PPVFS_SCB pScb
    )
{
    NTSTATUS ntError = STATUS_SUCCESS;

    /* Always reset the delete-on-close state to be safe */

    pScb->bDeleteOnClose = FALSE;

    /* Verify we are deleting the file we think we are */

    ntError = PvfsValidatePathSCB(pScb, &pScb->FileId);
    if (ntError == STATUS_SUCCESS)
    {
        ntError = PvfsSysRemove(pScb->pszFilename);

        /* Reset dev/inode state */

        pScb->FileId.Device = 0;
        pScb->FileId.Inode  = 0;

    }
    BAIL_ON_NT_STATUS(ntError);

cleanup:
    return ntError;

error:
    switch (ntError)
    {
    case STATUS_OBJECT_NAME_NOT_FOUND:
        break;

    default:
        LWIO_LOG_ERROR(
            "%s: Failed to execute delete-on-close on %s (%d,%d) (%s)\n",
            PVFS_LOG_HEADER,
            pScb->pszFilename, pScb->FileId.Device, pScb->FileId.Inode,
            LwNtStatusToName(ntError));
        break;
    }

    goto cleanup;
}


VOID
PvfsReleaseSCB(
    PPVFS_SCB *ppScb
    )
{
    NTSTATUS ntError = STATUS_SUCCESS;
    BOOLEAN bBucketLocked = FALSE;
    BOOLEAN bScbLocked = FALSE;
    BOOLEAN bScbControlLocked = FALSE;
    PVFS_STAT Stat = {0};
    PPVFS_SCB pScb = NULL;
    PPVFS_CB_TABLE_ENTRY pBucket = NULL;
    PSTR fullStreamName = NULL;

    /* Do housekeeping such as setting the last write time or honoring
       DeletOnClose when the CCB handle count reaches 0.  Not necessarily
       when the RefCount reaches 0.  We may have a non-handle open in the
       SCB table for a path component (see PvfsFindParentSCB()). */

    if (ppScb == NULL || *ppScb == NULL)
    {
        goto cleanup;
    }

    pScb = *ppScb;

    LWIO_LOCK_RWMUTEX_SHARED(bScbLocked, &pScb->rwCcbLock);

    if (!PVFS_IS_DEVICE_HANDLE(pScb) && (PvfsListLength(pScb->pCcbList) == 0))
    {
        ntError = PvfsSysStat(pScb->pszFilename, &Stat);
        if (ntError == STATUS_SUCCESS)
        {
            LWIO_LOCK_MUTEX(bScbControlLocked, &pScb->BaseControlBlock.Mutex);

            if (pScb->bDeleteOnClose)
            {
                /* Clear the cache entry and remove the file but ignore any errors */

                ntError = PvfsExecuteDeleteOnClose(pScb);

                /* The locking heirarchy requires that we drop the FCP control
                   block mutex before trying to pick up the ScbTable exclusive
                   lock */

                if (!pScb->BaseControlBlock.Removed)
                {
                    PPVFS_CB_TABLE_ENTRY pBucket = pScb->BaseControlBlock.pBucket;

                    LWIO_UNLOCK_MUTEX(bScbControlLocked, &pScb->BaseControlBlock.Mutex);

                    ntError = PvfsGetFullStreamname(&fullStreamName, pScb);

                    /* Remove the SCB from the Bucket before setting
                       pScb->BaseControlBlock.pBucket to NULL */

                    if (fullStreamName)
                    {
                        PvfsCbTableRemove(pBucket, fullStreamName);

                        LWIO_LOCK_MUTEX(bScbControlLocked, &pScb->BaseControlBlock.Mutex);
                        pScb->BaseControlBlock.Removed = TRUE;
                        pScb->BaseControlBlock.pBucket = NULL;
                        LWIO_UNLOCK_MUTEX(bScbControlLocked, &pScb->BaseControlBlock.Mutex);
                    }
                }
                LWIO_UNLOCK_MUTEX(bScbControlLocked, &pScb->BaseControlBlock.Mutex);

                PvfsPathCacheRemove(pScb->pszFilename);

                if (ntError == STATUS_SUCCESS)
                {
                    PvfsNotifyScheduleFullReport(
                        pScb,
                        S_ISDIR(Stat.s_mode) ?
                            FILE_NOTIFY_CHANGE_DIR_NAME :
                            FILE_NOTIFY_CHANGE_FILE_NAME,
                        FILE_ACTION_REMOVED,
                        pScb->pszFilename);
                }
            }

            LWIO_UNLOCK_MUTEX(bScbControlLocked, &pScb->BaseControlBlock.Mutex);
        }
    }

    LWIO_UNLOCK_RWMUTEX(bScbLocked, &pScb->rwCcbLock);


    // It is important to lock the ScbTable here so that there is no window
    // between the refcount check and the remove. Otherwise another open request
    // could search and locate the SCB in the tree and return free()'d memory.
    // However, if the SCB has no bucket pointer, it has already been removed
    // from the ScbTable so locking is unnecessary.

    pBucket = pScb->BaseControlBlock.pBucket;

    if (pBucket)
    {
        LWIO_LOCK_RWMUTEX_EXCLUSIVE(bBucketLocked, &pBucket->rwLock);
    }

    if (InterlockedDecrement(&pScb->BaseControlBlock.RefCount) == 0)
    {

        if (!pScb->BaseControlBlock.Removed)
        {
            if (!fullStreamName)
            {
                ntError = PvfsGetFullStreamname(&fullStreamName, pScb);
            }

            if (fullStreamName)
            {
                ntError = PvfsCbTableRemove_inlock(pBucket, fullStreamName);

                LWIO_ASSERT(ntError == STATUS_SUCCESS);

                pScb->BaseControlBlock.Removed = TRUE;
                pScb->BaseControlBlock.pBucket = NULL;
            }
        }

        if (pBucket)
        {
            LWIO_UNLOCK_RWMUTEX(bBucketLocked, &pBucket->rwLock);
        }

        // What should we do here if the StreamName allocation failed as we
        // didn't remove the node?  Leak?  If we go ahead and free the SCB, we'll
        // cause a crash later.

        if (pScb->BaseControlBlock.Removed)
        {
            PvfsFreeSCB(pScb);
        }
    }

    if (pBucket)
    {
        LWIO_UNLOCK_RWMUTEX(bBucketLocked, &pBucket->rwLock);
    }

    *ppScb = NULL;

cleanup:
    if (fullStreamName)
    {
        LwRtlCStringFree(&fullStreamName);
    }

    return;
}


/***********************************************************************
 **********************************************************************/

static
NTSTATUS
PvfsFindParentSCB(
    PPVFS_SCB *ppParentScb,
    PCSTR pszFullStreamname
    );

static
NTSTATUS
PvfsFindOwnerFCB(
    PPVFS_FCB *ppOwnerFcb,
    PSTR pszFullStreamname
    );

NTSTATUS
PvfsGetFullStreamname(
    PSTR *ppszFullStreamname,
    PPVFS_SCB pScb
    )
{
    if (!pScb)
    {
        return STATUS_INVALID_PARAMETER;
    }

    LWIO_ASSERT(pScb->pOwnerFcb);

    switch (pScb->StreamType)
    {
        case PVFS_STREAM_TYPE_DATA:

            return RtlCStringAllocatePrintf(ppszFullStreamname,
                                            "%s:%s:%s",
                                            pScb->pOwnerFcb->pszFilename,
                                            pScb->pszStreamname,
                                            PVFS_STREAM_DEFAULT_TYPE_S);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}


NTSTATUS
PvfsCreateSCB(
    OUT PPVFS_SCB *ppScb,
    IN PSTR pszFullStreamname,
    IN BOOLEAN bCheckShareAccess,
    IN FILE_SHARE_FLAGS SharedAccess,
    IN ACCESS_MASK DesiredAccess
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    PPVFS_SCB pScb = NULL;
    PPVFS_FCB pOwnerFcb = NULL;
    BOOLEAN bBucketLocked = FALSE;
    PPVFS_CB_TABLE_ENTRY pBucket = NULL;
    PPVFS_SCB pParentScb = NULL;
    BOOLEAN bScbLocked = FALSE;

    PSTR pszOwnerFilename = NULL;
    PSTR pszFilePath = NULL;
    PSTR pszStreamname = NULL;
    PVFS_STREAM_TYPE StreamType = PVFS_STREAM_TYPE_DATA;

    ntError = PvfsFindParentSCB(&pParentScb, pszFullStreamname);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsParseStreamname(&pszOwnerFilename,
                                  &pszFilePath,
                                  &pszStreamname,
                                  &StreamType,
                                  pszFullStreamname);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsFindOwnerFCB(&pOwnerFcb, pszOwnerFilename);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsCbTableGetBucket(&pBucket, &gScbTable, pszFullStreamname);
    BAIL_ON_NT_STATUS(ntError);

    /* Protect against adding a duplicate */

    LWIO_LOCK_RWMUTEX_EXCLUSIVE(bBucketLocked, &pBucket->rwLock);

    ntError = PvfsCbTableLookup_inlock(
                  (PPVFS_CONTROL_BLOCK*)&pScb,
                  pBucket,
                  pszFullStreamname);
    if (ntError == STATUS_SUCCESS)
    {
        LWIO_UNLOCK_RWMUTEX(bBucketLocked, &pBucket->rwLock);

        if (bCheckShareAccess)
        {
            ntError = PvfsEnforceShareMode(
                          pScb,
                          SharedAccess,
                          DesiredAccess);
        }

        /* If we have success, then we are good.  If we have a sharing
           violation, give the caller a chance to break the oplock and
           we'll try again when the create is resumed. */

        if (ntError == STATUS_SUCCESS ||
            ntError == STATUS_SHARING_VIOLATION)
        {
            *ppScb = PvfsReferenceSCB(pScb);
        }

        goto cleanup;
    }

    ntError = PvfsAllocateSCB(&pScb);
    BAIL_ON_NT_STATUS(ntError);

    ntError = RtlCStringDuplicate(&pScb->pszFilename, pszOwnerFilename);
    BAIL_ON_NT_STATUS(ntError);

    ntError = RtlCStringDuplicate(&pScb->pszStreamname, pszStreamname);
    BAIL_ON_NT_STATUS(ntError);

    pScb->StreamType = StreamType;

    pScb->pParentScb = pParentScb ? PvfsReferenceSCB(pParentScb) : NULL;

    ntError = PvfsAddSCBToFCB(pOwnerFcb, pScb);
    BAIL_ON_NT_STATUS(ntError);

    /* Add to the file handle table */

    LWIO_LOCK_MUTEX(bScbLocked, &pScb->BaseControlBlock.Mutex);
    ntError = PvfsCbTableAdd_inlock(pBucket, pszFullStreamname, (PVOID)pScb);
    if (ntError == STATUS_SUCCESS)
    {
        pScb->BaseControlBlock.pBucket = pBucket;
    }
    LWIO_UNLOCK_MUTEX(bScbLocked, &pScb->BaseControlBlock.Mutex);
    BAIL_ON_NT_STATUS(ntError);

    /* Return a reference to the SCB */

    *ppScb = PvfsReferenceSCB(pScb);
    ntError = STATUS_SUCCESS;

cleanup:
    LWIO_UNLOCK_RWMUTEX(bBucketLocked, &pBucket->rwLock);

    if (pParentScb)
    {
        PvfsReleaseSCB(&pParentScb);
    }

    if (pScb)
    {
        PvfsReleaseSCB(&pScb);
    }

    if (pOwnerFcb)
    {
        PvfsReleaseFCB(&pOwnerFcb);
    }

    RTL_FREE(&pszOwnerFilename);
    RTL_FREE(&pszFilePath);

    return ntError;

error:

    goto cleanup;
}

/***********************************************************************
 **********************************************************************/

static
NTSTATUS
PvfsFindParentSCB(
    PPVFS_SCB *ppParentScb,
    PCSTR pszFullStreamname
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    PPVFS_SCB pScb = NULL;
    PSTR pszDirname = NULL;
    PPVFS_CB_TABLE_ENTRY pBucket = NULL;
    PSTR pszDefaultDirStreamname = NULL;


    if (LwRtlCStringIsEqual(pszFullStreamname, "/::$DATA", TRUE))
    {
        ntError = STATUS_SUCCESS;
        *ppParentScb = NULL;

        goto cleanup;
    }

    ntError = PvfsFileDirname(&pszDirname, pszFullStreamname);
    BAIL_ON_NT_STATUS(ntError);

    ntError = RtlCStringAllocatePrintf(
                       &pszDefaultDirStreamname,
                       "%s::$DATA",
                       pszDirname);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsCbTableGetBucket(&pBucket,
                                   &gScbTable,
                                   (PVOID)pszDefaultDirStreamname);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsCbTableLookup(
                  (PPVFS_CONTROL_BLOCK*)&pScb,
                  pBucket,
                  (PVOID)pszDefaultDirStreamname);
    if (ntError == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        ntError = PvfsCreateSCB(
                      &pScb,
                      pszDefaultDirStreamname,
                      FALSE,
                      0,
                      0);
    }
    BAIL_ON_NT_STATUS(ntError);

    *ppParentScb = PvfsReferenceSCB(pScb);

cleanup:
    if (pScb)
    {
        PvfsReleaseSCB(&pScb);
    }

    if (pszDirname)
    {
        LwRtlCStringFree(&pszDirname);
    }

    if (pszDefaultDirStreamname)
    {
        LwRtlCStringFree(&pszDefaultDirStreamname);
    }


    return ntError;

error:

    goto cleanup;
}

/*******************************************************
 ******************************************************/


static
NTSTATUS
PvfsFindOwnerFCB(
    PPVFS_FCB *ppOwnerFcb,
    PSTR pszFullStreamname
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;

    PPVFS_FCB pFcb = NULL;
    PPVFS_CB_TABLE_ENTRY pBucket = NULL;

    ntError = PvfsCbTableGetBucket(&pBucket, &gFcbTable, (PVOID)pszFullStreamname);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsCbTableLookup(
                  (PPVFS_CONTROL_BLOCK*)&pFcb,
                  pBucket,
                  pszFullStreamname);
    if (ntError == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        ntError = PvfsCreateFCB(
                      &pFcb,
                      pszFullStreamname,
                      FALSE,
                      0,
                      0);
    }
    BAIL_ON_NT_STATUS(ntError);

    *ppOwnerFcb = PvfsReferenceFCB(pFcb);

cleanup:
    if (pFcb)
    {
        PvfsReleaseFCB(&pFcb);
    }

    return ntError;

error:

    goto cleanup;
}



/*******************************************************
 ******************************************************/

NTSTATUS
PvfsAddCCBToSCB(
    PPVFS_SCB pScb,
    PPVFS_CCB pCcb
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    BOOLEAN scbWriteLocked = FALSE;
    BOOLEAN fcbWriteLocked = FALSE;

    LWIO_LOCK_RWMUTEX_EXCLUSIVE(scbWriteLocked, &pScb->rwCcbLock);
    LWIO_LOCK_RWMUTEX_EXCLUSIVE(fcbWriteLocked, &pScb->pOwnerFcb->rwScbLock);

    ntError = PvfsListAddTail(pScb->pCcbList, &pCcb->ScbList);
    BAIL_ON_NT_STATUS(ntError);

    pCcb->pScb = PvfsReferenceSCB(pScb);

    InterlockedIncrement(&pScb->pOwnerFcb->OpenFileHandleCount);

    ntError = STATUS_SUCCESS;

cleanup:
    LWIO_UNLOCK_RWMUTEX(fcbWriteLocked, &pScb->pOwnerFcb->rwScbLock);
    LWIO_UNLOCK_RWMUTEX(scbWriteLocked, &pScb->rwCcbLock);

    return ntError;

error:
    goto cleanup;
}

/*******************************************************
 ******************************************************/

NTSTATUS
PvfsRemoveCCBFromSCB(
    PPVFS_SCB pScb,
    PPVFS_CCB pCcb
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    BOOLEAN scbWriteLocked = FALSE;
    BOOLEAN fcbWriteLocked = FALSE;

    LWIO_LOCK_RWMUTEX_EXCLUSIVE(scbWriteLocked, &pScb->rwCcbLock);
    LWIO_LOCK_RWMUTEX_EXCLUSIVE(fcbWriteLocked, &pScb->pOwnerFcb->rwScbLock);

    ntError = PvfsListRemoveItem(pScb->pCcbList, &pCcb->ScbList);
    BAIL_ON_NT_STATUS(ntError);

    InterlockedDecrement(&pScb->pOwnerFcb->OpenFileHandleCount);

cleanup:
    LWIO_UNLOCK_RWMUTEX(fcbWriteLocked, &pScb->pOwnerFcb->rwScbLock);
    LWIO_UNLOCK_RWMUTEX(scbWriteLocked, &pScb->rwCcbLock);

    return ntError;

error:
    goto cleanup;
}


/*****************************************************************************
 ****************************************************************************/

NTSTATUS
PvfsPendOplockBreakTest(
    IN PPVFS_SCB pScb,
    IN PPVFS_IRP_CONTEXT pIrpContext,
    IN PPVFS_CCB pCcb,
    IN PPVFS_OPLOCK_PENDING_COMPLETION_CALLBACK pfnCompletion,
    IN PPVFS_OPLOCK_PENDING_COMPLETION_FREE_CTX pfnFreeContext,
    IN PVOID pCompletionContext
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    PPVFS_PENDING_OPLOCK_BREAK_TEST pTestCtx = NULL;

    BAIL_ON_INVALID_PTR(pScb, ntError);
    BAIL_ON_INVALID_PTR(pfnCompletion, ntError);

    ntError = PvfsCreateOplockBreakTestContext(
                  &pTestCtx,
                  pScb,
                  pIrpContext,
                  pCcb,
                  pfnCompletion,
                  pfnFreeContext,
                  pCompletionContext);
    BAIL_ON_NT_STATUS(ntError);

    // Returns STATUS_PENDING on success

    ntError = PvfsAddItemPendingOplockBreakAck(
                  pScb,
                  pIrpContext,
                  (PPVFS_OPLOCK_PENDING_COMPLETION_CALLBACK)
                      PvfsOplockPendingBreakIfLocked,
                  (PPVFS_OPLOCK_PENDING_COMPLETION_FREE_CTX)
                      PvfsFreeOplockBreakTestContext,
                  (PVOID)pTestCtx);
    if ((ntError != STATUS_SUCCESS) &&
        (ntError != STATUS_PENDING))
    {
        BAIL_ON_NT_STATUS(ntError);
    }

cleanup:
    return ntError;

error:
    PvfsFreeOplockBreakTestContext(&pTestCtx);

    goto cleanup;
}

/*****************************************************************************
 ****************************************************************************/

NTSTATUS
PvfsAddItemPendingOplockBreakAck(
    IN PPVFS_SCB pScb,
    IN PPVFS_IRP_CONTEXT pIrpContext,
    IN PPVFS_OPLOCK_PENDING_COMPLETION_CALLBACK pfnCompletion,
    IN PPVFS_OPLOCK_PENDING_COMPLETION_FREE_CTX pfnFreeContext,
    IN PVOID pCompletionContext
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    BOOLEAN bLocked = FALSE;
    PPVFS_OPLOCK_PENDING_OPERATION pPendingOp = NULL;

    BAIL_ON_INVALID_PTR(pScb, ntError);
    BAIL_ON_INVALID_PTR(pfnCompletion, ntError);

    LWIO_LOCK_MUTEX(bLocked, &pScb->BaseControlBlock.Mutex);

    if (!pScb->bOplockBreakInProgress)
    {
        LWIO_UNLOCK_MUTEX(bLocked, &pScb->BaseControlBlock.Mutex);

        ntError = pfnCompletion(pCompletionContext);
        goto cleanup;
    }

    ntError = PvfsAllocateMemory(
                  (PVOID*)&pPendingOp,
                  sizeof(PVFS_OPLOCK_PENDING_OPERATION),
                  FALSE);
    BAIL_ON_NT_STATUS(ntError);

    PVFS_INIT_LINKS(&pPendingOp->PendingOpList);

    pPendingOp->pIrpContext = PvfsReferenceIrpContext(pIrpContext);
    pPendingOp->pfnCompletion = pfnCompletion;
    pPendingOp->pfnFreeContext = pfnFreeContext;
    pPendingOp->pCompletionContext = pCompletionContext;

    ntError = PvfsListAddTail(
                  pScb->pOplockPendingOpsQueue,
                  (PVOID)pPendingOp);
    BAIL_ON_NT_STATUS(ntError);

    pIrpContext->QueueType = PVFS_QUEUE_TYPE_PENDING_OPLOCK_BREAK;

    pIrpContext->pScb = PvfsReferenceSCB(pScb);

    PvfsIrpMarkPending(
        pIrpContext,
        PvfsQueueCancelIrp,
        pIrpContext);

    /* Set the request in a cancellable state */

    PvfsIrpContextClearFlag(pIrpContext, PVFS_IRP_CTX_FLAG_ACTIVE);

    ntError = STATUS_PENDING;

cleanup:
    LWIO_UNLOCK_MUTEX(bLocked, &pScb->BaseControlBlock.Mutex);

    return ntError;

error:
    LWIO_UNLOCK_MUTEX(bLocked, &pScb->BaseControlBlock.Mutex);

    if (pPendingOp)
    {
        PvfsFreePendingOp(&pPendingOp);
    }

    goto cleanup;
}


/*****************************************************************************
 ****************************************************************************/

BOOLEAN
PvfsStreamHasOtherOpens(
    IN PPVFS_SCB pScb,
    IN PPVFS_CCB pCcb
    )
{
    PLW_LIST_LINKS pCursor = NULL;
    PPVFS_CCB pCurrentCcb = NULL;
    BOOLEAN bNonSelfOpen = FALSE;
    BOOLEAN bScbReadLocked = FALSE;

    LWIO_LOCK_RWMUTEX_SHARED(bScbReadLocked, &pScb->rwCcbLock);

    while((pCursor = PvfsListTraverse(pScb->pCcbList, pCursor)) != NULL)
    {
        pCurrentCcb = LW_STRUCT_FROM_FIELD(
                          pCursor,
                          PVFS_CCB,
                          ScbList);

        if (pCcb != pCurrentCcb)
        {
            bNonSelfOpen = TRUE;
            break;
        }
    }

    LWIO_UNLOCK_RWMUTEX(bScbReadLocked, &pScb->rwCcbLock);

    return bNonSelfOpen;
}

/*****************************************************************************
 ****************************************************************************/

BOOLEAN
PvfsStreamIsOplocked(
    IN PPVFS_SCB pScb
    )
{
    return !PvfsListIsEmpty(pScb->pOplockList);
}

/*****************************************************************************
 ****************************************************************************/

BOOLEAN
PvfsStreamIsOplockedExclusive(
    IN PPVFS_SCB pScb
    )
{
    BOOLEAN bExclusiveOplock = FALSE;
    PPVFS_OPLOCK_RECORD pOplock = NULL;
    PLW_LIST_LINKS pOplockLink = NULL;

    if (PvfsListIsEmpty(pScb->pOplockList))
    {
        return FALSE;
    }

    /* We only need to check for the first non-cancelled oplock record
       in the list since the list itself must be consistent and
       non-conflicting */

    while ((pOplockLink = PvfsListTraverse(pScb->pOplockList, pOplockLink)) != NULL)
    {
        pOplock = LW_STRUCT_FROM_FIELD(
                      pOplockLink,
                      PVFS_OPLOCK_RECORD,
                      OplockList);

        if (PvfsIrpContextCheckFlag(
                pOplock->pIrpContext,
                PVFS_IRP_CTX_FLAG_CANCELLED))
        {
            pOplock = NULL;
            continue;
        }

        /* Found first non-cancelled oplock */
        break;
    }

    if (pOplock &&
        ((pOplock->OplockType == IO_OPLOCK_REQUEST_OPLOCK_BATCH) ||
         (pOplock->OplockType == IO_OPLOCK_REQUEST_OPLOCK_LEVEL_1)))
    {
        bExclusiveOplock = TRUE;
    }

    return bExclusiveOplock;
}

/*****************************************************************************
 ****************************************************************************/

BOOLEAN
PvfsStreamIsOplockedShared(
    IN PPVFS_SCB pScb
    )
{
    return !PvfsStreamIsOplockedExclusive(pScb);
}

/*****************************************************************************
 ****************************************************************************/

NTSTATUS
PvfsAddOplockRecord(
    IN OUT PPVFS_SCB pScb,
    IN     PPVFS_IRP_CONTEXT pIrpContext,
    IN     PPVFS_CCB pCcb,
    IN     ULONG OplockType
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    PPVFS_OPLOCK_RECORD pOplock = NULL;

    ntError = PvfsAllocateMemory(
                  (PVOID*)&pOplock,
                  sizeof(PVFS_OPLOCK_RECORD),
                  FALSE);
    BAIL_ON_NT_STATUS(ntError);

    PVFS_INIT_LINKS(&pOplock->OplockList);

    pOplock->OplockType = OplockType;
    pOplock->pCcb = PvfsReferenceCCB(pCcb);
    pOplock->pIrpContext = PvfsReferenceIrpContext(pIrpContext);

    ntError = PvfsListAddTail(pScb->pOplockList, &pOplock->OplockList);
    BAIL_ON_NT_STATUS(ntError);

cleanup:
    return ntError;

error:
    if (pOplock)
    {
        PvfsFreeOplockRecord(&pOplock);
    }

    goto cleanup;
}



/*****************************************************************************
 ****************************************************************************/

VOID
PvfsFreeOplockRecord(
    PPVFS_OPLOCK_RECORD *ppOplockRec
    )
{
    PPVFS_OPLOCK_RECORD pOplock = NULL;

    if (ppOplockRec && *ppOplockRec)
    {
        pOplock = *ppOplockRec;

        if (pOplock->pIrpContext)
        {
            PvfsReleaseIrpContext(&pOplock->pIrpContext);
        }

        if (pOplock->pCcb)
        {
            PvfsReleaseCCB(pOplock->pCcb);
        }

        PVFS_FREE(ppOplockRec);
    }

    return;
}


/*****************************************************************************
 ****************************************************************************/

static
NTSTATUS
PvfsOplockCleanPendingOpQueue(
    PVOID pContext
    );

static
VOID
PvfsOplockCleanPendingOpFree(
    PVOID *ppContext
    );

NTSTATUS
PvfsScheduleCancelPendingOp(
    PPVFS_IRP_CONTEXT pIrpContext
    )
{
    NTSTATUS ntError = STATUS_UNSUCCESSFUL;
    PPVFS_WORK_CONTEXT pWorkCtx = NULL;
    PPVFS_IRP_CONTEXT pIrpCtx = NULL;

    BAIL_ON_INVALID_PTR(pIrpContext->pScb, ntError);

    pIrpCtx = PvfsReferenceIrpContext(pIrpContext);

    ntError = PvfsCreateWorkContext(
                  &pWorkCtx,
                  FALSE,
                  pIrpCtx,
                  (PPVFS_WORK_CONTEXT_CALLBACK)PvfsOplockCleanPendingOpQueue,
                  (PPVFS_WORK_CONTEXT_FREE_CTX)PvfsOplockCleanPendingOpFree);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsAddWorkItem(gpPvfsInternalWorkQueue, (PVOID)pWorkCtx);
    BAIL_ON_NT_STATUS(ntError);

cleanup:
    return ntError;

error:
    if (pIrpCtx)
    {
        PvfsReleaseIrpContext(&pIrpCtx);
    }

    PvfsFreeWorkContext(&pWorkCtx);

    goto cleanup;
}


/*****************************************************************************
 ****************************************************************************/

static
NTSTATUS
PvfsOplockCleanPendingOpInternal(
    PPVFS_SCB pScb,
    PPVFS_LIST pQueue,
    PPVFS_IRP_CONTEXT pIrpContext,
    BOOLEAN bCancelIfNotFound
    );

static
NTSTATUS
PvfsOplockCleanPendingOpQueue(
    PVOID pContext
    )
{
    NTSTATUS ntError = STATUS_SUCCESS;
    PPVFS_IRP_CONTEXT pIrpCtx = (PPVFS_IRP_CONTEXT)pContext;
    PPVFS_SCB pScb = PvfsReferenceSCB(pIrpCtx->pScb);

    /* We have to check both the "pending" and the "ready" queues.
       Although, it is possible that the "ready" queue processed a
       cancelled IRP before we get to it here. */

    ntError = PvfsOplockCleanPendingOpInternal(
                  pScb,
                  pScb->pOplockPendingOpsQueue,
                  pIrpCtx,
                  FALSE);
    if (ntError != STATUS_SUCCESS)
    {
        ntError = PvfsOplockCleanPendingOpInternal(
                      pScb,
                      pScb->pOplockReadyOpsQueue,
                      pIrpCtx,
                      TRUE);
    }
    BAIL_ON_NT_STATUS(ntError);

cleanup:
    if (pScb)
    {
        PvfsReleaseSCB(&pScb);
    }

    if (pIrpCtx)
    {
        PvfsReleaseIrpContext(&pIrpCtx);
    }

    return ntError;

error:
    goto cleanup;
}


/*****************************************************************************
 ****************************************************************************/

static
NTSTATUS
PvfsOplockCleanPendingOpInternal(
    PPVFS_SCB pScb,
    PPVFS_LIST pQueue,
    PPVFS_IRP_CONTEXT pIrpContext,
    BOOLEAN bCancelIfNotFound
    )
{
    NTSTATUS ntError = STATUS_NOT_FOUND;
    PPVFS_OPLOCK_PENDING_OPERATION pOperation = NULL;
    PLW_LIST_LINKS pOpLink = NULL;
    PLW_LIST_LINKS pNextLink = NULL;
    BOOLEAN bFound = FALSE;
    BOOLEAN bScbLocked = FALSE;

    LWIO_LOCK_MUTEX(bScbLocked, &pScb->BaseControlBlock.Mutex);

    pOpLink = PvfsListTraverse(pQueue, NULL);

    while (pOpLink)
    {
        pOperation = LW_STRUCT_FROM_FIELD(
                         pOpLink,
                         PVFS_OPLOCK_PENDING_OPERATION,
                         PendingOpList);

        pNextLink = PvfsListTraverse(pQueue, pOpLink);

        if (pOperation->pIrpContext != pIrpContext)
        {
            pOpLink = pNextLink;
            continue;
        }

        bFound = TRUE;

        PvfsListRemoveItem(pQueue, pOpLink);
        pOpLink = NULL;

        LWIO_UNLOCK_MUTEX(bScbLocked, &pScb->BaseControlBlock.Mutex);

        pOperation->pIrpContext->pIrp->IoStatusBlock.Status = STATUS_CANCELLED;

        PvfsAsyncIrpComplete(pOperation->pIrpContext);

        PvfsFreePendingOp(&pOperation);

        /* Can only be one IrpContext match so we are done */
        ntError = STATUS_SUCCESS;
    }
    LWIO_UNLOCK_MUTEX(bScbLocked, &pScb->BaseControlBlock.Mutex);

    if (!bFound && bCancelIfNotFound)
    {
        pIrpContext->pIrp->IoStatusBlock.Status = STATUS_CANCELLED;

        PvfsAsyncIrpComplete(pIrpContext);
    }

    return ntError;
}


/*****************************************************************************
 ****************************************************************************/

static
 VOID
PvfsOplockCleanPendingOpFree(
    PVOID *ppContext
    )
{
    /* No op -- context released in PvfsOplockCleanPendingOpQueue */
    return;
}


/*****************************************************************************
 ****************************************************************************/

NTSTATUS
PvfsRenameSCB(
    PPVFS_SCB pScb,
    PPVFS_CCB pCcb,
    PSTR pszNewFilename
    )
{
    NTSTATUS ntError = STATUS_SUCCESS;
    PPVFS_SCB pNewParentScb = NULL;
    PPVFS_SCB pOldParentScb = NULL;
    PPVFS_SCB pTargetScb = NULL;
    PPVFS_CB_TABLE_ENTRY pTargetBucket = NULL;
    PPVFS_CB_TABLE_ENTRY pCurrentBucket = NULL;
    BOOLEAN bCurrentScbControl = FALSE;
    BOOLEAN bTargetScbControl = FALSE;
    BOOLEAN bTargetBucketLocked = FALSE;
    BOOLEAN bCurrentBucketLocked = FALSE;
    BOOLEAN bScbRwLocked = FALSE;
    BOOLEAN bCcbLocked = FALSE;
    BOOLEAN bRenameLock = FALSE;
    PSTR targetFullStreamName = NULL;
    PSTR newFullStreamName = NULL;
    PSTR currentFullStreamName = NULL;

    ntError = PvfsValidatePathSCB(pCcb->pScb, &pCcb->FileId);
    BAIL_ON_NT_STATUS(ntError);

    /* If the target has an existing SCB, remove it from the Table and let
       the existing ref counters play out (e.g. pending change notifies. */

    ntError = PvfsFindParentSCB(&pNewParentScb, pszNewFilename);
    BAIL_ON_NT_STATUS(ntError);

    ntError = PvfsCbTableGetBucket(&pTargetBucket, &gScbTable, pszNewFilename);
    BAIL_ON_NT_STATUS(ntError);

    /* Locks - gScbTable(Excl) */
    LWIO_LOCK_RWMUTEX_EXCLUSIVE(bRenameLock, &gScbTable.rwLock);

    /* Locks - gScbTable(Excl),
               pTargetBucket(Excl) */

    LWIO_LOCK_RWMUTEX_EXCLUSIVE(bTargetBucketLocked, &pTargetBucket->rwLock);

    ntError = PvfsCbTableLookup_inlock(
                  (PPVFS_CONTROL_BLOCK*)&pTargetScb,
                  pTargetBucket,
                  pszNewFilename);
    if (ntError == STATUS_SUCCESS)
    {
        /* Make sure we have a different SCB */

        if (pTargetScb != pScb)
        {
            LWIO_LOCK_MUTEX(bTargetScbControl, &pTargetScb->BaseControlBlock.Mutex);
            if (!pTargetScb->BaseControlBlock.Removed)
            {
                pTargetScb->BaseControlBlock.Removed = TRUE;
                pTargetScb->BaseControlBlock.pBucket = NULL;

                LWIO_UNLOCK_MUTEX(bTargetScbControl, &pTargetScb->BaseControlBlock.Mutex);

                ntError = PvfsGetFullStreamname(&targetFullStreamName, pTargetScb);
                if (ntError == STATUS_SUCCESS)
                {
                    ntError = PvfsCbTableRemove_inlock(pTargetBucket, targetFullStreamName);

                    LWIO_ASSERT(ntError == STATUS_SUCCESS);

                    LwRtlCStringFree(&targetFullStreamName);
                }
            }
            LWIO_UNLOCK_MUTEX(bTargetScbControl, &pTargetScb->BaseControlBlock.Mutex);
        }
    }

    /* Locks - gScbTable(Excl),
               pTargetBucket(Excl),
               pScb->rwLock(Excl) */

    LWIO_LOCK_RWMUTEX_EXCLUSIVE(bScbRwLocked, &pScb->rwLock);

    ntError = PvfsSysRename(pCcb->pScb->pszFilename, pszNewFilename);
    BAIL_ON_NT_STATUS(ntError);

    /* Clear the cache entry but ignore any errors */

    ntError = PvfsPathCacheRemove(pScb->pszFilename);

    /* Remove the SCB from the table, update the lookup key, and then re-add.
       Otherwise you will get memory corruption as a freed pointer gets left
       in the Table because if cannot be located using the current (updated)
       filename. Another reason to use the dev/inode pair instead if we could
       solve the "Create New File" issue. */

    pCurrentBucket = pScb->BaseControlBlock.pBucket;

    LWIO_LOCK_MUTEX(bCurrentScbControl, &pScb->BaseControlBlock.Mutex);
    pScb->BaseControlBlock.Removed = TRUE;
    pScb->BaseControlBlock.pBucket = NULL;
    LWIO_UNLOCK_MUTEX(bCurrentScbControl, &pScb->BaseControlBlock.Mutex);

    /* Locks - gScbTable(Excl)
               pTargetBucket(Excl)
               pScb->rwLock(Excl),
               pCurrentBucket(Excl) */

    if (pCurrentBucket != pTargetBucket)
    {
        LWIO_LOCK_RWMUTEX_EXCLUSIVE(bCurrentBucketLocked, &pCurrentBucket->rwLock);
    }

    ntError = PvfsGetFullStreamname(&currentFullStreamName, pScb);
    if (ntError == STATUS_SUCCESS)
    {
        ntError = PvfsCbTableRemove_inlock(pCurrentBucket, currentFullStreamName);

        LWIO_ASSERT(ntError == STATUS_SUCCESS);

        LwRtlCStringFree(&currentFullStreamName);
    }
    LWIO_UNLOCK_RWMUTEX(bCurrentBucketLocked, &pCurrentBucket->rwLock);
    BAIL_ON_NT_STATUS(ntError);

    /* Have to update the parent links as well */

    if (pNewParentScb != pScb->pParentScb)
    {
        pOldParentScb = pScb->pParentScb;
        pScb->pParentScb = pNewParentScb;
        pNewParentScb = NULL;
    }

    // Update File and Stream names

    LwRtlCStringFree(&pScb->pszFilename);

    ntError = LwRtlCStringDuplicate(&pScb->pszFilename, pszNewFilename);
    BAIL_ON_NT_STATUS(ntError);

    // FIXME!!  Here is where we would change the stream name itself
    // ntError = PvfsParseStreamname();

    ntError = PvfsGetFullStreamname(&newFullStreamName, pScb);
    BAIL_ON_NT_STATUS(ntError);

    /* Locks - gScbTable(Excl),
               pTargetBucket(Excl),
               pScb->rwLock(Excl),
               pScb->BaseControlBlock.Mutex */

    LWIO_LOCK_MUTEX(bCurrentScbControl, &pScb->BaseControlBlock.Mutex);
    ntError = PvfsCbTableAdd_inlock(
                  pTargetBucket,
                  newFullStreamName,
                  (PPVFS_CONTROL_BLOCK)pScb);
    if (ntError == STATUS_SUCCESS)
    {
        pScb->BaseControlBlock.Removed = FALSE;
        pScb->BaseControlBlock.pBucket = pTargetBucket;
    }
    LWIO_UNLOCK_MUTEX(bCurrentScbControl, &pScb->BaseControlBlock.Mutex);
    BAIL_ON_NT_STATUS(ntError);

    LWIO_UNLOCK_RWMUTEX(bScbRwLocked, &pScb->rwLock);
    LWIO_UNLOCK_RWMUTEX(bTargetBucketLocked, &pTargetBucket->rwLock);


    LWIO_LOCK_MUTEX(bCcbLocked, &pCcb->ControlBlock);

        PVFS_FREE(&pCcb->pszFilename);
        ntError = LwRtlCStringDuplicate(&pCcb->pszFilename, pszNewFilename);
        BAIL_ON_NT_STATUS(ntError);

    LWIO_UNLOCK_MUTEX(bCcbLocked, &pCcb->ControlBlock);

cleanup:
    LWIO_UNLOCK_RWMUTEX(bTargetBucketLocked, &pTargetBucket->rwLock);
    LWIO_UNLOCK_RWMUTEX(bCurrentBucketLocked, &pCurrentBucket->rwLock);
    LWIO_UNLOCK_RWMUTEX(bRenameLock, &gScbTable.rwLock);
    LWIO_UNLOCK_RWMUTEX(bScbRwLocked, &pScb->rwLock);
    LWIO_UNLOCK_MUTEX(bCurrentScbControl, &pScb->BaseControlBlock.Mutex);
    LWIO_UNLOCK_MUTEX(bCcbLocked, &pCcb->ControlBlock);

    if (pNewParentScb)
    {
        PvfsReleaseSCB(&pNewParentScb);
    }

    if (pOldParentScb)
    {
        PvfsReleaseSCB(&pOldParentScb);
    }

    if (pTargetScb)
    {
        PvfsReleaseSCB(&pTargetScb);
    }

    return ntError;

error:
    goto cleanup;
}


/*****************************************************************************
 ****************************************************************************/

BOOLEAN
PvfsScbIsPendingDelete(
    PPVFS_SCB pScb
    )
{
    BOOLEAN bPendingDelete = FALSE;
    BOOLEAN bIsLocked = FALSE;

    LWIO_LOCK_MUTEX(bIsLocked, &pScb->BaseControlBlock.Mutex);
    bPendingDelete = pScb->bDeleteOnClose;
    LWIO_UNLOCK_MUTEX(bIsLocked, &pScb->BaseControlBlock.Mutex);

    return bPendingDelete;
}

/*****************************************************************************
 ****************************************************************************/

VOID
PvfsScbSetPendingDelete(
    PPVFS_SCB pScb,
    BOOLEAN bPendingDelete
    )
{
    BOOLEAN bIsLocked = FALSE;

    LWIO_LOCK_MUTEX(bIsLocked, &pScb->BaseControlBlock.Mutex);
    pScb->bDeleteOnClose = bPendingDelete;
    LWIO_UNLOCK_MUTEX(bIsLocked, &pScb->BaseControlBlock.Mutex);
}

/*****************************************************************************
 ****************************************************************************/

PPVFS_SCB
PvfsGetParentSCB(
    PPVFS_SCB pScb
    )
{
    PPVFS_SCB pParent = NULL;
    BOOLEAN bLocked = FALSE;

    if (pScb)
    {
        LWIO_LOCK_RWMUTEX_SHARED(bLocked, &pScb->rwLock);
        if (pScb->pParentScb)
        {
            pParent = PvfsReferenceSCB(pScb->pParentScb);
        }
        LWIO_UNLOCK_RWMUTEX(bLocked, &pScb->rwLock);
    }

    return pParent;
}

