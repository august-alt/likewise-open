/* Editor Settings: expandtabs and use 4 spaces for indentation
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 * -*- mode: c, c-basic-offset: 4 -*- */

/*
 * Copyright Likewise Software    2004-2008
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
 *        globals.c
 *
 * Abstract:
 *
 *        Likewise Security and Authentication Subsystem (LSASS)
 *
 *        NTLM Server API Globals
 *
 * Authors: Krishna Ganugapati (krishnag@likewisesoftware.com)
 *          Marc Guy (mguy@likewisesoftware.com)
 */
#include "api.h"

time_t gServerStartTime = 0;

PLSA_AUTH_PROVIDER gpAuthProviderList = NULL;
pthread_rwlock_t gpAuthProviderList_rwlock;

PLSA_RPC_SERVER gpRpcServerList = NULL;
pthread_rwlock_t gpRpcServerList_rwlock;

pthread_rwlock_t gPerfCounters_rwlock;
UINT64 gPerfCounters[LsaMetricSentinel];

pthread_mutex_t    gAPIConfigLock     = PTHREAD_MUTEX_INITIALIZER;
PSTR               gpszConfigFilePath = NULL;
LSA_SRV_API_CONFIG gAPIConfig = {0};

DWORD
LsaSrvApiInit(
    PCSTR pszConfigFilePath
    )
{
    DWORD dwError = 0;
    LSA_SRV_API_CONFIG apiConfig = {0};

    gServerStartTime = time(NULL);

    pthread_rwlock_init(&gPerfCounters_rwlock, NULL);

    memset(&gPerfCounters[0], 0, sizeof(gPerfCounters));

    pthread_rwlock_init(&gpAuthProviderList_rwlock, NULL);

    dwError = LsaSrvApiInitConfig(&gAPIConfig);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvApiReadConfig(
                    pszConfigFilePath,
                    &apiConfig);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvApiTransferConfigContents(
                    &apiConfig,
                    &gAPIConfig);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvInitAuthProviders(pszConfigFilePath);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaInitRpcServers(pszConfigFilePath);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaAllocateString(
                    pszConfigFilePath,
                    &gpszConfigFilePath);
    BAIL_ON_LSA_ERROR(dwError);

cleanup:

    LsaSrvApiFreeConfigContents(&apiConfig);

    return 0;

error:

    goto cleanup;
}

DWORD
LsaSrvApiShutdown(
    VOID
    )
{
    LsaSrvFreeAuthProviders();

    pthread_mutex_lock(&gAPIConfigLock);

    LSA_SAFE_FREE_STRING(gpszConfigFilePath);

    LsaSrvApiFreeConfigContents(&gAPIConfig);

    pthread_mutex_unlock(&gAPIConfigLock);

    return 0;


}



static LWMsgDispatchSpec gNtlmMessageHandlers[] =
{
    LWMSG_DISPATCH(NTLM_Q_ACCEPT_SEC_CTXT, NtlmSrvIpcAcceptSecurityContext),
    LWMSG_DISPATCH(NTLM_Q_ACQUIRE_CREDS, NtlmSrvIpcAcquireCredentialsHandle),
    LWMSG_DISPATCH(NTLM_Q_DECRYPT_MSG, NtlmSrvIpcDecryptMessage),
    LWMSG_DISPATCH(NTLM_Q_ENCRYPT_MSG, NtlmSrvIpcEncryptMessage),
    LWMSG_DISPATCH(NTLM_Q_EXPORT_SEC_CTXT, NtlmSrvIpcExportSecurityContext),
    LWMSG_DISPATCH(NTLM_Q_FREE_CREDS, NtlmSrvIpcFreeCredentialsHandle),
    LWMSG_DISPATCH(NTLM_Q_IMPORT_SEC_CTXT, NtlmSrvIpcImportSecurityContext),
    LWMSG_DISPATCH(NTLM_Q_INIT_SEC_CTXT, NtlmSrvIpcInitializeSecurityContext),
    LWMSG_DISPATCH(NTLM_Q_MAKE_SIGN, NtlmSrvIpcMakeSignature),
    LWMSG_DISPATCH(NTLM_Q_QUERY_CREDS, NtlmSrvIpcQueryCredentialsAttributes),
    LWMSG_DISPATCH(NTLM_Q_QUERY_CTXT, NtlmSrvIpcQuerySecurityContextAttributes),
    LWMSG_DISPATCH(NTLM_Q_VERIFY_SIGN, NtlmSrvIpcVerifySignature),
    LWMSG_DISPATCH_END
};

LWMsgDispatchSpec*
NtlmSrvGetDispatchSpec(
    void
    )
{
    return gNtlmMessageHandlers;
}


pthread_t gRpcSrvWorker;
