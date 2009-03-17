#include "includes.h"


NTSTATUS
SamrSrvInitMemory(
    void
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    int locked = 0;

    GLOBAL_DATA_LOCK(locked);

    if (!bSamrSrvInitialised && !pMemRoot) {
        pMemRoot = talloc(NULL, 0, NULL);
        BAIL_ON_NO_MEMORY(pMemRoot);
    }

cleanup:
    GLOBAL_DATA_UNLOCK(locked);

    return status;

error:
    goto cleanup;
}


NTSTATUS
SamrSrvDestroyMemory(
    void
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    int locked = 0;

    GLOBAL_DATA_LOCK(locked);

    if (bSamrSrvInitialised && pMemRoot) {
        tfree(pMemRoot);
    }

cleanup:
    GLOBAL_DATA_UNLOCK(locked);

    return status;

error:
    goto cleanup;
}


NTSTATUS
SamrSrvAllocateMemory(
    void **ppOut,
    DWORD dwSize,
    void *pDep
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    void *pOut = NULL;
    void *pParent = NULL;
    int locked = 0;

    pParent = (pDep) ? pDep : pMemRoot;

    GLOBAL_DATA_LOCK(locked);

    pOut = talloc(pParent, dwSize, NULL);
    BAIL_ON_NO_MEMORY(pOut);

    memset(pOut, 0, dwSize);

    *ppOut = pOut;

cleanup:
    GLOBAL_DATA_UNLOCK(locked);

    return status;

error:
    *ppOut = NULL;
    goto cleanup;
}


void
SamrSrvFreeMemory(
    void *pPtr
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    int locked = 0;

    GLOBAL_DATA_LOCK(locked);

    tfree(pPtr);

error:
    GLOBAL_DATA_UNLOCK(locked);
}


/*
local variables:
mode: c
c-basic-offset: 4
indent-tabs-mode: nil
tab-width: 4
end:
*/
