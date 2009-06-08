/* Editor Settings: expandtabs and use 4 spaces for indentation
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 */

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
 *        ipc_protocol.c
 *
 * Abstract:
 *
 *        Likewise Security and Authentication Subsystem (LSASS)
 *
 *
 *
 * Authors: Krishna Ganugapati (krishnag@likewisesoftware.com)
 *          Sriram Nambakam (snambakam@likewisesoftware.com)
 *          Wei Fu (wfu@likewisesoftware.com)
 *
 */

#include "ipc.h"

static LWMsgTypeSpec gNtlmIpcErrorSpec[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_ERROR),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmAcceptSecCtxt[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_ACCEPT_SEC_CTXT_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmAcquireCreds[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_ACQUIRE_CREDS_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmDecryptMsg[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_DECRYPT_MSG_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmEncryptMsg[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_ENCRYPT_MSG_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmExportSecCtxt[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_EXPORT_SEC_CTXT_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};


static LWMsgTypeSpec gNtlmFreeCreds[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_FREE_CREDS_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmImportSecCtxt[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_IMPORT_SEC_CTXT_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmInitSecCtxt[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_INIT_SEC_CTXT_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmMakeSign[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_MAKE_SIGN_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmQueryCreds[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_QUERY_CREDS_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmQueryCtxt[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_QUERY_CTXT_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgTypeSpec gNtlmVerifySign[] =
{
    LWMSG_STRUCT_BEGIN(NTLM_IPC_VERIFY_SIGN_REQ),
    LWMSG_STRUCT_END,
    LWMSG_TYPE_END
};

static LWMsgProtocolSpec gNtlmIpcSpec[] =
{
    LWMSG_MESSAGE(NTLM_Q_ACCEPT_SEC_CTXT, gNtlmAcceptSecCtxt),
    LWMSG_MESSAGE(NTLM_R_ACCEPT_SEC_CTXT_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_ACCEPT_SEC_CTXT_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_ACQUIRE_CREDS, gNtlmAcquireCreds),
    LWMSG_MESSAGE(NTLM_R_ACQUIRE_CREDS_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_ACQUIRE_CREDS_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_DECRYPT_MSG, gNtlmDecryptMsg),
    LWMSG_MESSAGE(NTLM_R_DECRYPT_MSG_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_DECRYPT_MSG_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_ENCRYPT_MSG, gNtlmEncryptMsg),
    LWMSG_MESSAGE(NTLM_R_ENCRYPT_MSG_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_ENCRYPT_MSG_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_EXPORT_SEC_CTXT, gNtlmExportSecCtxt),
    LWMSG_MESSAGE(NTLM_R_EXPORT_SEC_CTXT_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_EXPORT_SEC_CTXT_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_FREE_CREDS, gNtlmFreeCreds),
    LWMSG_MESSAGE(NTLM_R_FREE_CREDS_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_FREE_CREDS_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_IMPORT_SEC_CTXT, gNtlmImportSecCtxt),
    LWMSG_MESSAGE(NTLM_R_IMPORT_SEC_CTXT_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_IMPORT_SEC_CTXT_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_INIT_SEC_CTXT, gNtlmSecCtxt),
    LWMSG_MESSAGE(NTLM_R_INIT_SEC_CTXT_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_INIT_SEC_CTXT_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_MAKE_SIGN, gNtlmMakeSign),
    LWMSG_MESSAGE(NTLM_R_MAKE_SIGN_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_MAKE_SIGN_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_QUERY_CREDS, gNtlmQueryCreds),
    LWMSG_MESSAGE(NTLM_R_QUERY_CREDS_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_QUERY_CREDS_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_QUERY_CTXT, gNtlmQueryCtxt),
    LWMSG_MESSAGE(NTLM_R_QUERY_CTXT_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_QUERY_CTXT_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_MESSAGE(NTLM_Q_VERIFY_SIGN, gNtlmVerifySign),
    LWMSG_MESSAGE(NTLM_R_VERIFY_SIGN_SUCCESS, NULL),
    LWMSG_MESSAGE(NTLM_R_VERIFY_SIGN_FAILURE, gNtlmIpcErrorSpec),
    LWMSG_PROTOCOL_END
};

LWMsgProtocolSpec*
NtlmIpcGetProtocolSpec(
    void
    )
{
    return gNtlmIpcSpec;
}

DWORD
NtlmMapLwmsgStatus(
    LWMsgStatus status
    )
{
    switch (status)
    {
    default:
        return NTLM_ERROR_INTERNAL;
    case LWMSG_STATUS_SUCCESS:
        return NTLM_ERROR_SUCCESS;
    case LWMSG_STATUS_ERROR:
    case LWMSG_STATUS_MEMORY:
    case LWMSG_STATUS_MALFORMED:
    case LWMSG_STATUS_OVERFLOW:
    case LWMSG_STATUS_UNDERFLOW:
    case LWMSG_STATUS_EOF:
    case LWMSG_STATUS_UNIMPLEMENTED:
    case LWMSG_STATUS_SYSTEM:
        return NTLM_ERROR_INTERNAL;
    case LWMSG_STATUS_INVALID_PARAMETER:
        return EINVAL;
    case LWMSG_STATUS_INVALID_STATE:
        return EINVAL;
    case LWMSG_STATUS_SECURITY:
        return EACCES;
    case LWMSG_STATUS_INTERRUPT:
        return EINTR;
    case LWMSG_STATUS_FILE_NOT_FOUND:
        return ENOENT;
    case LWMSG_STATUS_CONNECTION_REFUSED:
        return ECONNREFUSED;
    case LWMSG_STATUS_PEER_RESET:
        return ECONNRESET;
    case LWMSG_STATUS_PEER_ABORT:
        return ECONNABORTED;
    case LWMSG_STATUS_PEER_CLOSE:
        return EPIPE;
    case LWMSG_STATUS_SESSION_LOST:
        return EPIPE;
    }
}


/*
local variables:
mode: c
c-basic-offset: 4
indent-tabs-mode: nil
tab-width: 4
end:
*/
