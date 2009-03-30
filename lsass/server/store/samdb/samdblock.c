#include "includes.h"

DWORD
SamDbBuildDbInstanceLock(
    PSAM_DB_INSTANCE_LOCK* ppLock
    )
{
    DWORD dwError = 0;
    PSAM_DB_INSTANCE_LOCK pLock = NULL;

    dwError = DirectoryAllocateMemory(
                    sizeof(SAM_DB_INSTANCE_LOCK),
                    (PVOID*)&pLock);
    BAIL_ON_SAMDB_ERROR(dwError);

    pLock->refCount = 1;

    pthread_rwlock_init(&pLock->rwLock, NULL);

    *ppLock = pLock;

cleanup:

    return dwError;

error:

    *ppLock = NULL;

    if (pLock)
    {
        SamDbReleaseDbInstanceLock(pLock);
    }

    goto cleanup;
}

DWORD
SamDbAcquireDbInstanceLock(
    PSAM_DB_INSTANCE_LOCK pLock,
    PSAM_DB_INSTANCE_LOCK* ppLock
    )
{
    DWORD dwError = 0;

    InterlockedIncrement(&pLock->refCount);

    *ppLock = pLock;

    return dwError;
}

VOID
SamDbReleaseDbInstanceLock(
    PSAM_DB_INSTANCE_LOCK pLock
    )
{
    if (InterlockedDecrement(&pLock->refCount) == 0)
    {
        SamDbFreeDbInstanceLock(pLock);
    }
}

VOID
SamDbFreeDbInstanceLock(
    PSAM_DB_INSTANCE_LOCK pLock
    )
{
    if (pLock->pRwLock)
    {
        pthread_rwlock_destroy(&pLock->rwLock);
    }

    DirectoryFreeMemory(pLock);
}
