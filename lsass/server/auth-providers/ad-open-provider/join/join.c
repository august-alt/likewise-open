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
 *        join.c
 *
 * Abstract:
 *
 *        Likewise Security and Authentication Subsystem (LSASS)
 *
 *        Join to Active Directory
 *
 * Authors: Krishna Ganugapati (krishnag@likewisesoftware.com)
 *          Sriram Nambakam (snambakam@likewisesoftware.com)
 */

#include "includes.h"

#define LSA_JOIN_OU_PREFIX "OU="
#define LSA_JOIN_CN_PREFIX "CN="
#define LSA_JOIN_DC_PREFIX "DC="

#define LSA_JOIN_MAX_ALLOWED_CLOCK_DRIFT_SECONDS 60

#define MACHPASS_LEN  (16)


static
DWORD
LsaJoinDomainInternal(
    PWSTR  pwszHostname,
    PWSTR  pwszDnsDomain,
    PWSTR  pwszDomain,
    PWSTR  pwszAccountOu,
    PWSTR  pwszAccount,
    PWSTR  pwszPassword,
    DWORD  dwJoinFlags,
    PWSTR  pwszOsName,
    PWSTR  pwszOsVersion,
    PWSTR  pwszOsServicePack
    );

static
DWORD
LsaBuildOrgUnitDN(
    PCSTR pszDomain,
    PCSTR pszOU,
    PSTR* ppszOU_DN
    );

static
DWORD
LsaRandBytes(
    PBYTE pBuffer,
    DWORD dwCount
    );

static
DWORD
LsaGenerateMachinePassword(
    PWSTR  pwszPassword,
    size_t sPasswordLen
    );


DWORD
LsaGenerateRandomString(
    PSTR    pszBuffer,
    size_t  sBufferLen
    );

static
DWORD
LsaGetAccountName(
    const wchar16_t *machname,
    const wchar16_t *domain_controller_name,
    const wchar16_t *dns_domain_name,
    wchar16_t       **account_name
    );


static
NTSTATUS
LsaCreateMachineAccount(
    PWSTR          pwszDCName,
    LW_PIO_CREDS   pCreds,
    PWSTR          pwszMachineAccountName,
    PWSTR          pwszMachinePassword,
    DWORD          dwJoinFlags,
    PWSTR         *ppwszDomainName,
    PSID          *ppDomainSid
    );


static
NTSTATUS
LsaEncryptPasswordBufferEx(
    PBYTE  pPasswordBuffer,
    DWORD  dwPasswordBufferSize,
    PWSTR  pwszPassword,
    DWORD  dwPasswordLen,
    PBYTE  pSessionKey,
    DWORD  dwSessionKeyLen
    );


static
NTSTATUS
LsaEncodePasswordBuffer(
    IN  PCWSTR  pwszPassword,
    OUT PBYTE   pBlob,
    IN  DWORD   dwBlobSize
    );


static
DWORD
LsaSaveMachinePassword(
    PCWSTR  pwszMachineName,
    PCWSTR  pwszMachineAccountName,
    PCWSTR  pwszMachineDnsDomain,
    PCWSTR  pwszDomainName,
    PCWSTR  pwszDnsDomainName,
    PCWSTR  pwszDCName,
    PCWSTR  pwszSidStr,
    PCWSTR  pwszPassword
    );


static
DWORD
LsaSavePrincipalKey(
    PCWSTR  pwszName,
    PCWSTR  pwszPassword,
    DWORD   dwPasswordLen,
    PCWSTR  pwszRealm,
    PCWSTR  pwszSalt,
    PCWSTR  pwszDCName,
    DWORD   dwKvno
    );


static
DWORD
LsaDirectoryConnect(
    const wchar16_t *domain,
    LDAP **ldconn,
    wchar16_t **dn_context
    );


static
DWORD
LsaDirectoryDisconnect(
    LDAP *ldconn
    );


static
DWORD
LsaMachAcctCreate(
    LDAP *ld,
    const wchar16_t *machine_name,
    const wchar16_t *machacct_name,
    const wchar16_t *ou,
    int rejoin
    );


static
DWORD
LsaMachDnsNameSearch(
    LDAP *ldconn,
    const wchar16_t *name,
    const wchar16_t *dn_context,
    const wchar16_t *dns_domain_name,
    wchar16_t **samacct
    );


static
DWORD
LsaMachAcctSearch(
    LDAP *ldconn,
    const wchar16_t *name,
    const wchar16_t *dn_context,
    wchar16_t **pDn,
    wchar16_t **pDnsName
    );


static
DWORD
LsaMachAcctSetAttribute(
    LDAP *ldconn,
    const wchar16_t *dn,
    const wchar16_t *attr_name,
    const wchar16_t **attr_val,
    int new
    );


static
DWORD
LsaGetNtPasswordHash(
    IN  PCWSTR  pwszPassword,
    OUT PBYTE   pNtHash,
    IN  DWORD   dwNtHashSize
    );


static
DWORD
LsaEncryptNtHashVerifier(
    IN  PBYTE    pNewNtHash,
    IN  DWORD    dwNewNtHashLen,
    IN  PBYTE    pOldNtHash,
    IN  DWORD    dwOldNtHashLen,
    OUT PBYTE    pNtVerifier,
    IN  DWORD    dwNtVerifierSize
    );


static
DWORD
LsaPrepareDesKey(
    IN  PBYTE  pInput,
    OUT PBYTE  pOutput
    );


DWORD
LsaJoinDomain(
    PCSTR pszHostname,
    PCSTR pszHostDnsDomain,
    PCSTR pszDomain,
    PCSTR pszOU,
    PCSTR pszUsername,
    PCSTR pszPassword,
    PCSTR pszOSName,
    PCSTR pszOSVersion,
    PCSTR pszOSServicePack,
    LSA_NET_JOIN_FLAGS dwFlags
    )
{
    DWORD dwError = 0;
    PSTR  pszOU_DN = NULL;
    PWSTR pwszHostname = NULL;
    PWSTR pwszHostDnsDomain = NULL;
    PWSTR pwszDomain = NULL;
    PWSTR pwszOU = NULL;
    PWSTR pwszOSName = NULL;
    PWSTR pwszOSVersion = NULL;
    PWSTR pwszOSServicePack = NULL;
    DWORD dwOptions = (LSAJOIN_JOIN_DOMAIN |
                       LSAJOIN_ACCT_CREATE |
                       LSAJOIN_DOMAIN_JOIN_IF_JOINED);
    PLSA_CREDS_FREE_INFO pAccessInfo = NULL;

    BAIL_ON_INVALID_STRING(pszHostname);
    BAIL_ON_INVALID_STRING(pszDomain);
    BAIL_ON_INVALID_STRING(pszUsername);

    if (geteuid() != 0) {
        dwError = LW_ERROR_ACCESS_DENIED;
        BAIL_ON_LSA_ERROR(dwError);
    }

    if ( !(dwFlags & LSA_NET_JOIN_DOMAIN_NOTIMESYNC) )
    {
        dwError = LsaSyncTimeToDC(pszDomain);
        BAIL_ON_LSA_ERROR(dwError);
    }

    // TODO-2011/01/13-dalmeida -- Ensure we use UPN.
    // Otherwise, whether this works depends on krb5.conf.
    // Can cons up UPN if needed since we have domain.
    dwError = LsaSetSMBCreds(
                pszUsername,
                pszPassword,
                TRUE,
                &pAccessInfo);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwMbsToWc16s(
                    pszHostname,
                    &pwszHostname);
    BAIL_ON_LSA_ERROR(dwError);

    if (!LW_IS_NULL_OR_EMPTY_STR(pszHostDnsDomain))
    {
        dwError = LwMbsToWc16s(
                        pszHostDnsDomain,
                        &pwszHostDnsDomain);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LwMbsToWc16s(
                    pszDomain,
                    &pwszDomain);
    BAIL_ON_LSA_ERROR(dwError);

    if (!LW_IS_NULL_OR_EMPTY_STR(pszOU)) {

        dwError = LsaBuildOrgUnitDN(
                    pszDomain,
                    pszOU,
                    &pszOU_DN);
        BAIL_ON_LSA_ERROR(dwError);

        dwError = LwMbsToWc16s(
                    pszOU_DN,
                    &pwszOU);
        BAIL_ON_LSA_ERROR(dwError);
    }

    if (!LW_IS_NULL_OR_EMPTY_STR(pszOSName)) {
        dwError = LwMbsToWc16s(
                    pszOSName,
                    &pwszOSName);
        BAIL_ON_LSA_ERROR(dwError);
    }

    if (!LW_IS_NULL_OR_EMPTY_STR(pszOSVersion)) {
        dwError = LwMbsToWc16s(
                    pszOSVersion,
                    &pwszOSVersion);
        BAIL_ON_LSA_ERROR(dwError);
    }

    if (!LW_IS_NULL_OR_EMPTY_STR(pszOSServicePack)) {
        dwError = LwMbsToWc16s(
                    pszOSServicePack,
                    &pwszOSServicePack);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaJoinDomainInternal(
            pwszHostname,
            pwszHostDnsDomain,
            pwszDomain,
            pwszOU,
            NULL,
            NULL,
            dwOptions,
            pwszOSName,
            pwszOSVersion,
            pwszOSServicePack);
    BAIL_ON_LSA_ERROR(dwError);

cleanup:

    LsaFreeSMBCreds(&pAccessInfo);

    LW_SAFE_FREE_STRING(pszOU_DN);
    LW_SAFE_FREE_MEMORY(pwszHostname);
    LW_SAFE_FREE_MEMORY(pwszHostDnsDomain);
    LW_SAFE_FREE_MEMORY(pwszDomain);
    LW_SAFE_FREE_MEMORY(pwszOU);
    LW_SAFE_FREE_MEMORY(pwszOSName);
    LW_SAFE_FREE_MEMORY(pwszOSVersion);
    LW_SAFE_FREE_MEMORY(pwszOSServicePack);

    return dwError;

error:

    goto cleanup;
}

static
DWORD
LsaBuildOrgUnitDN(
    PCSTR pszDomain,
    PCSTR pszOU,
    PSTR* ppszOU_DN
    )
{
    DWORD dwError = 0;
    PSTR  pszOuDN = NULL;
    // Do not free
    PSTR  pszOutputPos = NULL;
    PCSTR pszInputPos = NULL;
    PCSTR pszInputSectionEnd = NULL;
    size_t sOutputDnLen = 0;
    size_t sSectionLen = 0;
    DWORD nDomainParts = 0;

    BAIL_ON_INVALID_STRING(pszDomain);
    BAIL_ON_INVALID_STRING(pszOU);

    // Figure out the length required to write the OU DN
    pszInputPos = pszOU;

    // skip leading slashes
    sSectionLen = strspn(pszInputPos, "/");
    pszInputPos += sSectionLen;

    while ((sSectionLen = strcspn(pszInputPos, "/")) != 0) {
        sOutputDnLen += sizeof(LSA_JOIN_OU_PREFIX) - 1;
        sOutputDnLen += sSectionLen;
        // For the separating comma
        sOutputDnLen++;

        pszInputPos += sSectionLen;

        sSectionLen = strspn(pszInputPos, "/");
        pszInputPos += sSectionLen;
    }

    // Figure out the length required to write the Domain DN
    pszInputPos = pszDomain;
    while ((sSectionLen = strcspn(pszInputPos, ".")) != 0) {
        sOutputDnLen += sizeof(LSA_JOIN_DC_PREFIX) - 1;
        sOutputDnLen += sSectionLen;
        nDomainParts++;

        pszInputPos += sSectionLen;

        sSectionLen = strspn(pszInputPos, ".");
        pszInputPos += sSectionLen;
    }

    // Add in space for the separating commas
    if (nDomainParts > 1)
    {
        sOutputDnLen += nDomainParts - 1;
    }

    dwError = LwAllocateMemory(
                    sizeof(CHAR) * (sOutputDnLen + 1),
                    (PVOID*)&pszOuDN);
    BAIL_ON_LSA_ERROR(dwError);

    pszOutputPos = pszOuDN;
    // Iterate through pszOU backwards and write to pszOuDN forwards
    pszInputPos = pszOU + strlen(pszOU) - 1;

    while(TRUE)
    {
        // strip trailing slashes
        while (pszInputPos >= pszOU && *pszInputPos == '/')
        {
            pszInputPos--;
        }

        if (pszInputPos < pszOU)
        {
            break;
        }

        // Find the end of this section (so that we can copy it to
        // the output string in forward order).
        pszInputSectionEnd = pszInputPos;
        while (pszInputPos >= pszOU && *pszInputPos != '/')
        {
            pszInputPos--;
        }
        sSectionLen = pszInputSectionEnd - pszInputPos;

        // Only "Computers" as the first element is a CN.
        if ((pszOutputPos ==  pszOuDN) &&
            (sSectionLen == sizeof("Computers") - 1) &&
            !strncasecmp(pszInputPos + 1, "Computers", sizeof("Computers") - 1))
        {
            // Add CN=<name>,
            memcpy(pszOutputPos, LSA_JOIN_CN_PREFIX,
                    sizeof(LSA_JOIN_CN_PREFIX) - 1);
            pszOutputPos += sizeof(LSA_JOIN_CN_PREFIX) - 1;
        }
        else
        {
            // Add OU=<name>,
            memcpy(pszOutputPos, LSA_JOIN_OU_PREFIX,
                    sizeof(LSA_JOIN_OU_PREFIX) - 1);
            pszOutputPos += sizeof(LSA_JOIN_OU_PREFIX) - 1;
        }

        memcpy(pszOutputPos,
                pszInputPos + 1,
                sSectionLen);
        pszOutputPos += sSectionLen;

        *pszOutputPos++ = ',';
    }

    // Make sure to overwrite any initial "CN=Computers" as "OU=Computers".
    // Note that it is safe to always set "OU=" as the start of the DN
    // unless the DN so far is exacly "CN=Computers,".
    if (strcasecmp(pszOuDN, LSA_JOIN_CN_PREFIX "Computers,"))
    {
        memcpy(pszOuDN, LSA_JOIN_OU_PREFIX, sizeof(LSA_JOIN_OU_PREFIX) - 1);
    }

    // Read the domain name foward in sections and write it back out
    // forward.
    pszInputPos = pszDomain;
    while (TRUE)
    {
        sSectionLen = strcspn(pszInputPos, ".");

        memcpy(pszOutputPos,
                LSA_JOIN_DC_PREFIX,
                sizeof(LSA_JOIN_DC_PREFIX) - 1);
        pszOutputPos += sizeof(LSA_JOIN_DC_PREFIX) - 1;

        memcpy(pszOutputPos, pszInputPos, sSectionLen);
        pszOutputPos += sSectionLen;

        pszInputPos += sSectionLen;

        sSectionLen = strspn(pszInputPos, ".");
        pszInputPos += sSectionLen;

        if (*pszInputPos != 0)
        {
            // Add a comma for the next entry
            *pszOutputPos++ = ',';
        }
        else
            break;

    }

    assert(pszOutputPos == pszOuDN + sizeof(CHAR) * (sOutputDnLen));
    *pszOutputPos = 0;

    *ppszOU_DN = pszOuDN;

cleanup:

    return dwError;

error:

    *ppszOU_DN = NULL;

    LW_SAFE_FREE_STRING(pszOuDN);

    goto cleanup;
}

DWORD
LsaSyncTimeToDC(
    PCSTR  pszDomain
    )
{
    DWORD dwError = 0;
    LWNET_UNIX_TIME_T dcTime = 0;
    time_t ttDCTime = 0;

    dwError = LWNetGetDCTime(
                    pszDomain,
                    &dcTime);
    BAIL_ON_LSA_ERROR(dwError);

    ttDCTime = (time_t) dcTime;

    if (labs(ttDCTime - time(NULL)) > LSA_JOIN_MAX_ALLOWED_CLOCK_DRIFT_SECONDS) {
        dwError = LwSetSystemTime(ttDCTime);
        BAIL_ON_LSA_ERROR(dwError);
    }

cleanup:

    return dwError;

error:

    goto cleanup;
}


static
DWORD
LsaJoinDomainInternal(
    PWSTR  pwszHostname,
    PWSTR  pwszDnsDomain,
    PWSTR  pwszDomain,
    PWSTR  pwszAccountOu,
    PWSTR  pwszAccount,
    PWSTR  pwszPassword,
    DWORD  dwJoinFlags,
    PWSTR  pwszOsName,
    PWSTR  pwszOsVersion,
    PWSTR  pwszOsServicePack
    )
{
    const DWORD dwLsaAccess = LSA_ACCESS_LOOKUP_NAMES_SIDS |
                              LSA_ACCESS_VIEW_POLICY_INFO;

    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD dwError = ERROR_SUCCESS;
    LSA_BINDING hLsaBinding = NULL;
    POLICY_HANDLE hLsaPolicy = NULL;
    LsaPolicyInformation *pLsaPolicyInfo = NULL;
    PWSTR pwszMachineName = NULL;
    PWSTR pwszMachineAcctName = NULL;
    PWSTR pwszMachinePassword[MACHPASS_LEN+1] = {0};
    PWSTR pwszDomainName = NULL;
    PSID pDomainSid = NULL;
    PWSTR pwszDnsDomainName = NULL;
    PWSTR pwszDCName = NULL;
    LDAP *pLdap = NULL;
    PWSTR pwszMachineNameLc = NULL;    /* lower cased machine name */
    PWSTR pwszBaseDn = NULL;
    PWSTR pwszDn = NULL;
    PWSTR pwszDnsAttrName = NULL;
    PWSTR pwszDnsAttrVal[2] = {0};
    PWSTR pwszSpnAttrName = NULL;
    PWSTR pwszSpnAttrVal[3] = {0};
    PWSTR pwszDescAttrName = NULL;
    PWSTR pwszDescAttrVal[2] = {0};
    PWSTR pwszOSNameAttrName = NULL;
    PWSTR pwszOSNameAttrVal[2] = {0};
    PWSTR pwszOSVersionAttrName = NULL;
    PWSTR pwszOSVersionAttrVal[2] = {0};
    PWSTR pwszOSServicePackAttrName = NULL;
    PWSTR pwszOSServicePackAttrVal[2] = {0};
    PWSTR pwszSidStr = NULL;
    LW_PIO_CREDS pCreds = NULL;

    dwError = LwAllocateWc16String(&pwszMachineName,
                                   pwszHostname);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwWc16sToUpper(pwszMachineName);
    BAIL_ON_LSA_ERROR(dwError)

    dwError = LsaGetRwDcName(pwszDomain,
                             TRUE,
                             &pwszDCName);
    BAIL_ON_LSA_ERROR(dwError);

    if (pwszAccount && pwszPassword)
    {
        ntStatus = LwIoCreatePlainCredsW(pwszAccount,
                                         pwszDomain,
                                         pwszPassword,
                                         &pCreds);
        BAIL_ON_NT_STATUS(ntStatus);
    }
    else
    {
        ntStatus = LwIoGetActiveCreds(NULL,
                                      &pCreds);
        BAIL_ON_NT_STATUS(ntStatus);
    }

    ntStatus = LsaInitBindingDefault(&hLsaBinding,
                                     pwszDCName,
                                     pCreds);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = LsaOpenPolicy2(hLsaBinding,
                              pwszDCName,
                              NULL,
                              dwLsaAccess,
                              &hLsaPolicy);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = LsaQueryInfoPolicy2(hLsaBinding,
                                   hLsaPolicy,
                                   LSA_POLICY_INFO_DNS,
                                   &pLsaPolicyInfo);
    BAIL_ON_NT_STATUS(ntStatus);

    dwError = LwAllocateWc16StringFromUnicodeString(
                                  &pwszDnsDomainName,
                                  &pLsaPolicyInfo->dns.dns_domain);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaGetAccountName(
                             pwszMachineName,
                             pwszDCName,
                             pwszDnsDomain ? pwszDnsDomain : pwszDnsDomainName,
                             &pwszMachineAcctName);
    BAIL_ON_LSA_ERROR(dwError);

    /* If account_ou is specified pre-create disabled machine
       account object in given branch of directory. It will
       be reset afterwards by means of rpc calls */
    if (pwszAccountOu)
    {
        dwError = LsaDirectoryConnect(pwszDCName, &pLdap, &pwszBaseDn);
        BAIL_ON_LSA_ERROR(dwError);

        dwError = LsaMachAcctCreate(pLdap, pwszMachineName, pwszMachineAcctName, pwszAccountOu,
                                    (dwJoinFlags & LSAJOIN_DOMAIN_JOIN_IF_JOINED));
        BAIL_ON_LSA_ERROR(dwError);

        dwError = LsaDirectoryDisconnect(pLdap);
        pLdap = NULL;
        BAIL_ON_LSA_ERROR(dwError);

        LW_SAFE_FREE_MEMORY(pwszBaseDn);
    }

    dwError = LsaGenerateMachinePassword(
               (PWSTR)pwszMachinePassword,
               sizeof(pwszMachinePassword)/sizeof(pwszMachinePassword[0]));
    BAIL_ON_LSA_ERROR(dwError);

    if (pwszMachinePassword[0] == '\0')
    {
        BAIL_ON_NT_STATUS(STATUS_INTERNAL_ERROR);
    }

    ntStatus = LsaCreateMachineAccount(pwszDCName,
                                       pCreds,
                                       pwszMachineAcctName,
                                       (PWSTR)pwszMachinePassword,
                                       dwJoinFlags,
                                       &pwszDomainName,
                                       &pDomainSid);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = RtlAllocateWC16StringFromSid(&pwszSidStr,
                                            pDomainSid);
    BAIL_ON_NT_STATUS(ntStatus);

    // Make sure we can access the account
    dwError = LsaDirectoryConnect(pwszDCName, &pLdap, &pwszBaseDn);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaMachAcctSearch(
                  pLdap,
                  pwszMachineAcctName,
                  pwszBaseDn,
                  &pwszDn,
                  NULL);
    if (dwError == ERROR_INVALID_PARAMETER)
    {
        dwError = LW_ERROR_LDAP_INSUFFICIENT_ACCESS;
    }
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSaveMachinePassword(
              pwszMachineName,
              pwszMachineAcctName,
              pwszDnsDomain ? pwszDnsDomain : pwszDnsDomainName,
              pwszDomainName,
              pwszDnsDomainName,
              pwszDCName,
              pwszSidStr,
              (PWSTR)pwszMachinePassword);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * Open connection to directory server if it's going to be needed
     */
    if (!(dwJoinFlags & LSAJOIN_DEFER_SPN_SET) ||
        pwszOsName || pwszOsVersion || pwszOsServicePack)
    {

        /*
         * Set SPN and dnsHostName attributes unless this part is to be deferred
         */
        if (!(dwJoinFlags & LSAJOIN_DEFER_SPN_SET))
        {
            PWSTR pwszDnsHostName = NULL;

            dwError = LwAllocateWc16String(&pwszMachineNameLc,
                                           pwszMachineName);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LwWc16sToLower(pwszMachineNameLc);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LwMbsToWc16s("dNSHostName", &pwszDnsAttrName);
            BAIL_ON_LSA_ERROR(dwError);

            pwszDnsAttrVal[0] = LdapAttrValDnsHostName(pwszMachineNameLc,
                                  pwszDnsDomain ? pwszDnsDomain : pwszDnsDomainName);
            pwszDnsAttrVal[1] = NULL;
            pwszDnsHostName = pwszDnsAttrVal[0];

            dwError = LsaMachAcctSetAttribute(pLdap, pwszDn,
                                       pwszDnsAttrName,
                                       (const wchar16_t**)pwszDnsAttrVal, 0);
            if (dwError == ERROR_DS_CONSTRAINT_VIOLATION)
            {
                dwError = ERROR_DS_NAME_ERROR_NO_MAPPING;
            }
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LwMbsToWc16s("servicePrincipalName",
                                   &pwszSpnAttrName);
            BAIL_ON_LSA_ERROR(dwError);

            pwszSpnAttrVal[0] = LdapAttrValSvcPrincipalName(pwszDnsHostName);
            pwszSpnAttrVal[1] = LdapAttrValSvcPrincipalName(pwszHostname);
            pwszSpnAttrVal[2] = NULL;

            dwError = LsaMachAcctSetAttribute(pLdap, pwszDn, pwszSpnAttrName,
                                              (const wchar16_t**)pwszSpnAttrVal, 0);
            BAIL_ON_LSA_ERROR(dwError);
        }

        if (wc16scmp(pwszMachineName, pwszMachineAcctName))
        {
            dwError = LwMbsToWc16s("description",
                                   &pwszDescAttrName);
            BAIL_ON_LSA_ERROR(dwError);

            pwszDescAttrVal[0] = LdapAttrValDnsHostName(
                                     pwszMachineNameLc,
                                     pwszDnsDomain ?
                                     pwszDnsDomain :
                                     pwszDnsDomainName);

            dwError = LsaMachAcctSetAttribute(
                      pLdap,
                      pwszDn,
                      pwszDescAttrName,
                      (const wchar16_t**)pwszDescAttrVal,
                      0);
            BAIL_ON_LSA_ERROR(dwError);
        }

        /*
         * Set operating system name and version attributes if specified
         */
        if (pwszOsName)
        {
            dwError = LwMbsToWc16s("operatingSystem",
                                   &pwszOSNameAttrName);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LwAllocateWc16String(&pwszOSNameAttrVal[0],
                                           pwszOsName);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LsaMachAcctSetAttribute(pLdap, pwszDn,
                                       pwszOSNameAttrName,
                                       (const wchar16_t**)pwszOSNameAttrVal,
                                       0);
            if (dwError == LW_ERROR_LDAP_INSUFFICIENT_ACCESS)
            {
                /* The user must be a non-admin. In this case, we cannot
                 * set the attribute.
                 */
                dwError = ERROR_SUCCESS;
            }
            else
            {
                BAIL_ON_LSA_ERROR(dwError);
            }
        }

        if (pwszOsVersion)
        {
            dwError = LwMbsToWc16s("operatingSystemVersion",
                                   &pwszOSVersionAttrName);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LwAllocateWc16String(&pwszOSVersionAttrVal[0],
                                           pwszOsVersion);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LsaMachAcctSetAttribute(pLdap, pwszDn,
                                       pwszOSVersionAttrName,
                                       (const wchar16_t**)pwszOSVersionAttrVal,
                                       0);
            if (dwError == LW_ERROR_LDAP_INSUFFICIENT_ACCESS)
            {
                dwError = ERROR_SUCCESS;
            }
            else
            {
                BAIL_ON_LSA_ERROR(dwError);
            }
        }

        if (pwszOsServicePack)
        {
            dwError = LwMbsToWc16s("operatingSystemServicePack",
                                   &pwszOSServicePackAttrName);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LwAllocateWc16String(&pwszOSServicePackAttrVal[0],
                                           pwszOsServicePack);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LsaMachAcctSetAttribute(pLdap, pwszDn,
                                       pwszOSServicePackAttrName,
                                       (const wchar16_t**)pwszOSServicePackAttrVal,
                                       0);
            if (dwError == LW_ERROR_LDAP_INSUFFICIENT_ACCESS)
            {
                dwError = ERROR_SUCCESS;
            }
            else
            {
                BAIL_ON_LSA_ERROR(dwError);
            }
        }
    }

cleanup:
    if (hLsaBinding && hLsaPolicy)
    {
        LsaClose(hLsaBinding, hLsaPolicy);

        LsaFreeBinding(&hLsaBinding);
    }

    if (pLsaPolicyInfo)
    {
        LsaRpcFreeMemory(pLsaPolicyInfo);
    }

    if (pCreds)
    {
        LwIoDeleteCreds(pCreds);
    }

    if (pLdap)
    {
        LsaDirectoryDisconnect(pLdap);
    }

    LW_SAFE_FREE_MEMORY(pwszDomainName);
    RTL_FREE(&pDomainSid);
    RTL_FREE(&pwszSidStr);
    LW_SAFE_FREE_MEMORY(pwszDnsDomainName);
    LW_SAFE_FREE_MEMORY(pwszMachineName);
    LW_SAFE_FREE_MEMORY(pwszMachineAcctName);
    LW_SAFE_FREE_MEMORY(pwszMachineNameLc);
    LW_SAFE_FREE_MEMORY(pwszBaseDn);
    LW_SAFE_FREE_MEMORY(pwszDn);
    LW_SAFE_FREE_MEMORY(pwszDnsAttrName);
    LW_SAFE_FREE_MEMORY(pwszDnsAttrVal[0]);
    LW_SAFE_FREE_MEMORY(pwszSpnAttrName);
    LW_SAFE_FREE_MEMORY(pwszSpnAttrVal[0]);
    LW_SAFE_FREE_MEMORY(pwszSpnAttrVal[1]);
    LW_SAFE_FREE_MEMORY(pwszDescAttrName);
    LW_SAFE_FREE_MEMORY(pwszDescAttrVal[0]);
    LW_SAFE_FREE_MEMORY(pwszOSNameAttrName);
    LW_SAFE_FREE_MEMORY(pwszOSNameAttrVal[0]);
    LW_SAFE_FREE_MEMORY(pwszOSVersionAttrName);
    LW_SAFE_FREE_MEMORY(pwszOSVersionAttrVal[0]);
    LW_SAFE_FREE_MEMORY(pwszOSServicePackAttrName);
    LW_SAFE_FREE_MEMORY(pwszOSServicePackAttrVal[0]);
    LW_SAFE_FREE_MEMORY(pwszDCName);

    if (dwError == ERROR_SUCCESS &&
        ntStatus != STATUS_SUCCESS)
    {
        dwError = NtStatusToWin32Error(ntStatus);
    }

    return dwError;

error:
    goto cleanup;
}


static
DWORD
LsaGetAccountName(
    const wchar16_t *machname,
    const wchar16_t *domain_controller_name,
    const wchar16_t *dns_domain_name,
    wchar16_t       **account_name
    )
{
    int err = ERROR_SUCCESS;
    LDAP *ld = NULL;
    wchar16_t *base_dn = NULL;
    wchar16_t *dn = NULL;
    wchar16_t *machname_lc = NULL;
    wchar16_t *samname = NULL;     /* short name valid for SAM account */
    wchar16_t *dnsname = NULL;
    wchar16_t *hashstr = NULL;
    wchar16_t *samacctname = NULL; /* account name (with trailing '$') */
    UINT32    hash = 0;
    UINT32    offset = 0;
    UINT32    hashoffset = 0;
    wchar16_t newname[16] = {0};
    wchar16_t searchname[17] = {0};
    size_t    hashstrlen = 0;
    size_t    machname_len = 0;
    size_t    samacctname_len = 0;


    err = LwWc16sLen(machname, &machname_len);
    BAIL_ON_LSA_ERROR(err);

    err = LwAllocateWc16String(&machname_lc, machname);
    BAIL_ON_LSA_ERROR(err);

    wc16slower(machname_lc);

    /* look for an existing account using the dns_host_name attribute */
    err = LsaDirectoryConnect(domain_controller_name, &ld, &base_dn);
    BAIL_ON_LSA_ERROR(err);

    err = LsaMachDnsNameSearch(ld, machname_lc, base_dn, dns_domain_name, &samname);
    if (err == ERROR_SUCCESS)
    {
        size_t samname_len = 0;

        err = LwWc16sLen(samname, &samname_len);
        BAIL_ON_LSA_ERROR(err);

        samname[samname_len - 1] = 0;
    }
    else
    {
        err = ERROR_SUCCESS;
    }

    if (!samname)
    {
        /* the host name is short enough to use as is */
        if (machname_len < 16)
        {
            if (sw16printfw(searchname,
                            sizeof(searchname)/sizeof(wchar16_t),
                            L"%ws$",
                            machname) < 0)
            {
                err = ErrnoToWin32Error(errno);
                BAIL_ON_LSA_ERROR(err);
            }

            err = LsaMachAcctSearch(ld, searchname, base_dn, NULL, &dnsname);
            if ( err != ERROR_SUCCESS || !dnsname)
            {
                err = ERROR_SUCCESS;

                err = LwAllocateWc16String(&samname, machname);
                BAIL_ON_LSA_ERROR(err);
            }
            LW_SAFE_FREE_MEMORY(dnsname);
        }
    }

     /*
      * No account was found and the name is too long so hash the host
      * name and combine with as much of the existing name as will fit
      * in the available space.  Search for an existing account with
      * that name and if a collision is detected, increment the hash
      * and try again.
      */
    if (!samname)
    {
        dnsname = LdapAttrValDnsHostName(
                      machname_lc,
                      dns_domain_name);

        if (dnsname)
        {
            err = LsaWc16sHash(dnsname, &hash);
            BAIL_ON_LSA_ERROR(err);

            LW_SAFE_FREE_MEMORY(dnsname);
        }
        else
        {
            err = LsaWc16sHash(machname_lc, &hash);
            BAIL_ON_LSA_ERROR(err);
        }

        for (offset = 0 ; offset < 100 ; offset++)
        {
            err = LsaHashToWc16s(hash + offset, &hashstr);
            BAIL_ON_LSA_ERROR(err);

            err = LwWc16sLen(hashstr, &hashstrlen);
            BAIL_ON_LSA_ERROR(err);


            // allow for '-' + hash
            hashoffset = 15 - (hashstrlen + 1);
            if (hashoffset > machname_len)
            {
                hashoffset = machname_len;
            }
            wc16sncpy(newname, machname, hashoffset);
            newname[hashoffset++] = (WCHAR)'-';
            wc16sncpy(newname + hashoffset, hashstr, hashstrlen + 1);

            LW_SAFE_FREE_MEMORY(hashstr);

            if (sw16printfw(searchname,
                            sizeof(searchname)/sizeof(wchar16_t),
                            L"%ws$",
                            newname) < 0)
            {
                err = ErrnoToWin32Error(errno);
                BAIL_ON_LSA_ERROR(err);
            }

            err = LsaMachAcctSearch(ld, searchname, base_dn, NULL, &dnsname);
            if ( err != ERROR_SUCCESS || !dnsname)
            {
                err = ERROR_SUCCESS;

                err = LwAllocateWc16String(&samname, newname);
                BAIL_ON_LSA_ERROR(err);

                break;
            }
            LW_SAFE_FREE_MEMORY(dnsname);
        }
        if (offset == 10)
        {
            err = ERROR_DUP_NAME;
            goto error;
        }
    }

    err = LwWc16sLen(samname, &samacctname_len);
    BAIL_ON_LSA_ERROR(err);

    samacctname_len += 2;

    err = LwAllocateMemory(sizeof(samacctname[0]) * samacctname_len,
                           OUT_PPVOID(&samacctname));
    BAIL_ON_LSA_ERROR(err);

    if (sw16printfw(samacctname,
                    samacctname_len,
                    L"%ws$",
                    samname) < 0)
    {
        err = ErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(err);
    }

    // Upper case the sam account name in case it was incorrectly created in AD
    // by a previous version.
    LwWc16sToUpper(samacctname);

    *account_name = samacctname;

cleanup:
    if (ld)
    {
        LsaDirectoryDisconnect(ld);
    }

    LW_SAFE_FREE_MEMORY(machname_lc);
    LW_SAFE_FREE_MEMORY(hashstr);
    LW_SAFE_FREE_MEMORY(dn);
    LW_SAFE_FREE_MEMORY(dnsname);
    LW_SAFE_FREE_MEMORY(samname);
    LW_SAFE_FREE_MEMORY(base_dn);

    return err;

error:
    LW_SAFE_FREE_MEMORY(samacctname);
    *account_name = NULL;

    goto cleanup;
}


static
DWORD
LsaGenerateMachinePassword(
    PWSTR  pwszPassword,
    size_t sPasswordLen
    )
{
    DWORD dwError = 0;
    DWORD i = 0;
    PSTR pszPassword = NULL;

    BAIL_ON_INVALID_POINTER(pwszPassword);
    pwszPassword[0] = (WCHAR) '\0';

    dwError = LwAllocateMemory(sizeof(pszPassword[0]) * sPasswordLen,
                               OUT_PPVOID(&pszPassword));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaGenerateRandomString(pszPassword, sPasswordLen);
    BAIL_ON_LSA_ERROR(dwError);

    /* "Cast" A string to W string */
    for (i=0; i<sPasswordLen; i++)
    {
        pwszPassword[i] = (WCHAR) pszPassword[i];
    }
cleanup:
    LW_SECURE_FREE_STRING(pszPassword);
    return dwError;

error:
    goto cleanup;
}


static const CHAR RandomCharsLc[] = "abcdefghijklmnopqrstuvwxyz";
static const CHAR RandomCharsUc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const CHAR RandomCharsDigits[] = "0123456789";
static const CHAR RandomCharsPunct[] = "-+/*,.;:!<=>%'&()";

static
DWORD
LsaRandBytes(
    PBYTE pBuffer,
    DWORD dwCount
    )
{
    DWORD dwError = 0;
    unsigned long dwRandError = 0;
    const char *pszRandFile = NULL;
    int iRandLine = 0;
    char *pszData = NULL;
    int iFlags = 0;

    if (!RAND_bytes(pBuffer, dwCount))
    {
        dwRandError = ERR_get_error_line_data(
                            &pszRandFile,
                            &iRandLine,
                            (const char **)&pszData,
                            &iFlags);
        if (iFlags & ERR_TXT_STRING)
        {
            LSA_LOG_DEBUG("RAND_bytes failed with message '%s' and error code %ld at %s:%d",
                    pszData, dwRandError, pszRandFile, iRandLine);
        }
        else
        {
            LSA_LOG_DEBUG("RAND_bytes failed with error code %ld at %s:%d",
                    dwRandError, pszRandFile, iRandLine);
        }
        dwError = ERROR_ENCRYPTION_FAILED;
        BAIL_ON_LSA_ERROR(dwError);
    }

cleanup:
    if (iFlags & ERR_TXT_MALLOCED)
    {
        LW_SAFE_FREE_STRING(pszData);
    }
    return dwError;

error:
    goto cleanup;
}

DWORD
LsaGenerateRandomString(
    PSTR    pszBuffer,
    size_t  sBufferLen
    )
{
    DWORD dwError = ERROR_SUCCESS;
    PBYTE pBuffer = NULL;
    PBYTE pClassBuffer = NULL;
    DWORD i = 0;
    DWORD iClass = 0;
    CHAR iChar = 0;
    DWORD iLcCount = 0;
    DWORD iUcCount = 0;
    DWORD iDigitsCount = 0;
    DWORD iPunctCount = 0;

    dwError = LwAllocateMemory(sizeof(pBuffer[0]) * sBufferLen,
                               OUT_PPVOID(&pBuffer));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateMemory(sizeof(pClassBuffer[0]) * sBufferLen,
                               OUT_PPVOID(&pClassBuffer));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaRandBytes(pBuffer, sBufferLen);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaRandBytes((unsigned char*)pClassBuffer, (int)sBufferLen);
    BAIL_ON_LSA_ERROR(dwError);

    for (i = 0; i < sBufferLen-1; i++)
    {
        /*
         * Check for missing character class,
         * and force selection of the missing class.
         * The two missing classes will be at the end of
         * the string, which may be a password weakness
         * issue.
         */
        if (i >= sBufferLen-3)
        {
            if (iLcCount == 0)
            {
                iClass = 0;
            }
            else if (iUcCount == 0)
            {
                iClass = 1;
            }
            else if (iDigitsCount == 0)
            {
                iClass = 2;
            }
            else if (iPunctCount == 0)
            {
                iClass = 3;
            }
        }
        else
        {
            iClass = pClassBuffer[i] % 4;
        }

        switch (iClass)
        {
            case 0:
                iChar = RandomCharsLc[
                            pBuffer[i] % (sizeof(RandomCharsLc) - 1)];
                iLcCount++;
                break;
            case 1:
                iChar = RandomCharsUc[
                            pBuffer[i] % (sizeof(RandomCharsUc) - 1)];
                iUcCount++;
                break;
            case 2:
                iChar = RandomCharsDigits[
                            pBuffer[i] % (sizeof(RandomCharsDigits) - 1)];
                iDigitsCount++;
                break;
            case 3:
                iChar = RandomCharsPunct[
                            pBuffer[i] % (sizeof(RandomCharsPunct) - 1)];
                iPunctCount++;
                break;
        }
        pszBuffer[i] = iChar;
    }

    pszBuffer[sBufferLen-1] = '\0';

cleanup:
    LW_SECURE_FREE_MEMORY(pBuffer, sBufferLen);
    LW_SECURE_FREE_MEMORY(pClassBuffer, sBufferLen);

    return dwError;

error:
    memset(pszBuffer, 0, sizeof(pszBuffer[0]) * sBufferLen);

    goto cleanup;
}


static
NTSTATUS
LsaCreateMachineAccount(
    PWSTR          pwszDCName,
    LW_PIO_CREDS   pCreds,
    PWSTR          pwszMachineAccountName,
    PWSTR          pwszMachinePassword,
    DWORD          dwJoinFlags,
    PWSTR         *ppwszDomainName,
    PSID          *ppDomainSid
    )
{
    const DWORD dwConnAccess = SAMR_ACCESS_OPEN_DOMAIN |
                               SAMR_ACCESS_ENUM_DOMAINS;

    const DWORD dwDomainAccess = DOMAIN_ACCESS_ENUM_ACCOUNTS |
                                 DOMAIN_ACCESS_OPEN_ACCOUNT |
                                 DOMAIN_ACCESS_LOOKUP_INFO_2 |
                                 DOMAIN_ACCESS_CREATE_USER;

    const DWORD dwUserAccess = USER_ACCESS_GET_ATTRIBUTES |
                               USER_ACCESS_SET_ATTRIBUTES |
                               USER_ACCESS_SET_PASSWORD;

    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD dwError = ERROR_SUCCESS;
    unsigned32 rpcStatus = 0;
    SAMR_BINDING hSamrBinding = NULL;
    CONNECT_HANDLE hConnect = NULL;
    rpc_transport_info_handle_t hTransportInfo = NULL;
    DWORD dwProtSeq = 0;
    PBYTE pSessionKey = NULL;
    DWORD dwSessionKeyLen = 0;
    unsigned16 sessionKeyLen = 0;
    PSID pBuiltinSid = NULL;
    DWORD dwResume = 0;
    DWORD dwSize = 256;
    PWSTR *ppwszDomainNames = NULL;
    DWORD i = 0;
    PWSTR pwszDomainName = NULL;
    DWORD dwNumEntries = 0;
    PSID pSid = NULL;
    PSID pDomainSid = NULL;
    DOMAIN_HANDLE hDomain = NULL;
    BOOLEAN bNewAccount = FALSE;
    PDWORD pdwRids = NULL;
    PDWORD pdwTypes = NULL;
    ACCOUNT_HANDLE hUser = NULL;
    DWORD dwUserAccessGranted = 0;
    DWORD dwRid = 0;
    DWORD dwLevel = 0;
    UserInfo *pInfo = NULL;
    DWORD dwFlagsEnable = 0;
    DWORD dwFlagsDisable = 0;
    UserInfo Info;
    size_t sMachinePasswordLen = 0;
    UserInfo PassInfo;
    PWSTR pwszMachineName = NULL;
    PUNICODE_STRING pFullName = NULL;

    memset(&Info, 0, sizeof(Info));
    memset(&PassInfo, 0, sizeof(PassInfo));

    ntStatus = SamrInitBindingDefault(&hSamrBinding,
                                      pwszDCName,
                                      pCreds);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = SamrConnect2(hSamrBinding,
                            pwszDCName,
                            dwConnAccess,
                            &hConnect);
    BAIL_ON_NT_STATUS(ntStatus);

    rpc_binding_inq_transport_info(hSamrBinding,
                                   &hTransportInfo,
                                   &rpcStatus);
    if (rpcStatus)
    {
        ntStatus = LwRpcStatusToNtStatus(rpcStatus);
        BAIL_ON_NT_STATUS(ntStatus);
    }

    rpc_binding_inq_prot_seq(hSamrBinding,
                             (unsigned32*)&dwProtSeq,
                             &rpcStatus);
    if (rpcStatus)
    {
        ntStatus = LwRpcStatusToNtStatus(rpcStatus);
        BAIL_ON_NT_STATUS(ntStatus);
    }

    if (dwProtSeq == rpc_c_protseq_id_ncacn_np)
    {
        rpc_smb_transport_info_inq_session_key(
                                   hTransportInfo,
                                   (unsigned char**)&pSessionKey,
                                   &sessionKeyLen);
        dwSessionKeyLen = (DWORD)sessionKeyLen;
    }
    else
    {
        ntStatus = STATUS_INVALID_CONNECTION;
        BAIL_ON_NT_STATUS(ntStatus);
    }

    dwError = LwAllocateWellKnownSid(WinBuiltinDomainSid,
                                   NULL,
                                   &pBuiltinSid,
                                   NULL);
    BAIL_ON_LSA_ERROR(dwError);

    do
    {
        ntStatus = SamrEnumDomains(hSamrBinding,
                                   hConnect,
                                   &dwResume,
                                   dwSize,
                                   &ppwszDomainNames,
                                   &dwNumEntries);
        BAIL_ON_NT_STATUS(ntStatus);

        if (ntStatus != STATUS_SUCCESS &&
            ntStatus != STATUS_MORE_ENTRIES)
        {
            BAIL_ON_NT_STATUS(ntStatus);
        }

        for (i = 0; pDomainSid == NULL && i < dwNumEntries; i++)
        {
            ntStatus = SamrLookupDomain(hSamrBinding,
                                        hConnect,
                                        ppwszDomainNames[i],
                                        &pSid);
            BAIL_ON_NT_STATUS(ntStatus);

            if (!RtlEqualSid(pSid, pBuiltinSid))
            {
                dwError = LwAllocateWc16String(&pwszDomainName,
                                               ppwszDomainNames[i]);
                BAIL_ON_LSA_ERROR(dwError);

                ntStatus = RtlDuplicateSid(&pDomainSid, pSid);
                BAIL_ON_NT_STATUS(ntStatus);
            }


            SamrFreeMemory(pSid);
            pSid = NULL;
        }

        if (ppwszDomainNames)
        {
            SamrFreeMemory(ppwszDomainNames);
            ppwszDomainNames = NULL;
        }
    }
    while (ntStatus == STATUS_MORE_ENTRIES);

    ntStatus = SamrOpenDomain(hSamrBinding,
                              hConnect,
                              dwDomainAccess,
                              pDomainSid,
                              &hDomain);
    BAIL_ON_NT_STATUS(ntStatus);

    /* for start, let's assume the account already exists */
    bNewAccount = FALSE;

    ntStatus = SamrLookupNames(hSamrBinding,
                               hDomain,
                               1,
                               &pwszMachineAccountName,
                               &pdwRids,
                               &pdwTypes,
                               NULL);
    if (ntStatus == STATUS_NONE_MAPPED)
    {
        if (!(dwJoinFlags & LSAJOIN_ACCT_CREATE)) goto error;

        ntStatus = SamrCreateUser2(hSamrBinding,
                                   hDomain,
                                   pwszMachineAccountName,
                                   ACB_WSTRUST,
                                   dwUserAccess,
                                   &hUser,
                                   &dwUserAccessGranted,
                                   &dwRid);
        BAIL_ON_NT_STATUS(ntStatus);

        bNewAccount = TRUE;

    }
    else if (ntStatus == STATUS_SUCCESS &&
             !(dwJoinFlags & LSAJOIN_DOMAIN_JOIN_IF_JOINED))
    {
        BAIL_ON_LSA_ERROR(NERR_SetupAlreadyJoined);
    }
    else
    {
        ntStatus = SamrOpenUser(hSamrBinding,
                                hDomain,
                                dwUserAccess,
                                pdwRids[0],
                                &hUser);
        BAIL_ON_NT_STATUS(ntStatus);
    }

    /*
     * Flip ACB_DISABLED flag - this way password timeout counter
     * gets restarted
     */

    dwLevel = 16;

    ntStatus = SamrQueryUserInfo(hSamrBinding,
                                 hUser,
                                 dwLevel,
                                 &pInfo);
    BAIL_ON_NT_STATUS(ntStatus);

    dwFlagsEnable = pInfo->info16.account_flags & (~ACB_DISABLED);
    dwFlagsDisable = pInfo->info16.account_flags | ACB_DISABLED;

    Info.info16.account_flags = dwFlagsEnable;
    ntStatus = SamrSetUserInfo2(hSamrBinding,
                                hUser,
                                dwLevel,
                                &Info);
    BAIL_ON_NT_STATUS(ntStatus);

    Info.info16.account_flags = dwFlagsDisable;
    ntStatus = SamrSetUserInfo2(hSamrBinding,
                                hUser,
                                dwLevel,
                                &Info);
    BAIL_ON_NT_STATUS(ntStatus);

    Info.info16.account_flags = dwFlagsEnable;
    ntStatus = SamrSetUserInfo2(hSamrBinding,
                                hUser,
                                dwLevel,
                                &Info);
    BAIL_ON_NT_STATUS(ntStatus);

    dwError = LwWc16sLen(pwszMachinePassword,
                         &sMachinePasswordLen);
    BAIL_ON_LSA_ERROR(dwError);

    if (bNewAccount)
    {
        UserInfo25 *pInfo25 = &PassInfo.info25;

        ntStatus = LsaEncryptPasswordBufferEx(pInfo25->password.data,
                                              sizeof(pInfo25->password.data),
                                              pwszMachinePassword,
                                              sMachinePasswordLen,
                                              pSessionKey,
                                              dwSessionKeyLen);
        BAIL_ON_NT_STATUS(ntStatus);

        dwError = LwAllocateWc16String(&pwszMachineName,
                                       pwszMachineAccountName);
        BAIL_ON_LSA_ERROR(dwError);

        pwszMachineName[sMachinePasswordLen - 1] = '\0';

        pInfo25->info.account_flags = ACB_WSTRUST;

        pFullName = &pInfo25->info.full_name;
        dwError = LwAllocateUnicodeStringFromWc16String(
                                      pFullName,
                                      pwszMachineName);
        BAIL_ON_LSA_ERROR(dwError);

        pInfo25->info.fields_present = SAMR_FIELD_FULL_NAME |
                                       SAMR_FIELD_ACCT_FLAGS |
                                       SAMR_FIELD_PASSWORD;
        dwLevel = 25;
    }
    else
    {
        UserInfo26 *pInfo26 = &PassInfo.info26;

        ntStatus = LsaEncryptPasswordBufferEx(pInfo26->password.data,
                                              sizeof(pInfo26->password.data),
                                              pwszMachinePassword,
                                              sMachinePasswordLen,
                                              pSessionKey,
                                              dwSessionKeyLen);
        BAIL_ON_NT_STATUS(ntStatus);

        pInfo26->password_len = sMachinePasswordLen;

        dwLevel = 26;
    }

    ntStatus = SamrSetUserInfo2(hSamrBinding,
                                hUser,
                                dwLevel,
                                &PassInfo);
    BAIL_ON_NT_STATUS(ntStatus);

    *ppwszDomainName = pwszDomainName;
    *ppDomainSid     = pDomainSid;

cleanup:
    if (hSamrBinding && hUser)
    {
        SamrClose(hSamrBinding, hUser);
    }

    if (hSamrBinding && hDomain)
    {
        SamrClose(hSamrBinding, hDomain);
    }

    if (hSamrBinding && hConnect)
    {
        SamrClose(hSamrBinding, hConnect);
    }

    if (hSamrBinding)
    {
        SamrFreeBinding(&hSamrBinding);
    }

    if (pFullName)
    {
        LwFreeUnicodeString(pFullName);
    }

    if (pInfo)
    {
        SamrFreeMemory(pInfo);
    }

    if (pdwRids)
    {
        SamrFreeMemory(pdwRids);
    }

    if (pdwTypes)
    {
        SamrFreeMemory(pdwTypes);
    }

    if (ppwszDomainNames)
    {
        SamrFreeMemory(ppwszDomainNames);
    }

    LW_SAFE_FREE_MEMORY(pBuiltinSid);
    LW_SAFE_FREE_MEMORY(pwszMachineName);

    if (ntStatus == STATUS_SUCCESS &&
        dwError != ERROR_SUCCESS)
    {
        ntStatus = LwWin32ErrorToNtStatus(dwError);
    }

    return ntStatus;

error:
    LW_SAFE_FREE_MEMORY(pwszDomainName);
    RTL_FREE(&pDomainSid);

    *ppwszDomainName = NULL;
    *ppDomainSid     = NULL;

    goto cleanup;
}


static
NTSTATUS
LsaEncryptPasswordBufferEx(
    PBYTE  pPasswordBuffer,
    DWORD  dwPasswordBufferSize,
    PWSTR  pwszPassword,
    DWORD  dwPasswordLen,
    PBYTE  pSessionKey,
    DWORD  dwSessionKeyLen
    )
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD dwError = ERROR_SUCCESS;
    MD5_CTX ctx;
    RC4_KEY rc4_key;
    BYTE InitValue[16] = {0};
    BYTE DigestedSessKey[16] = {0};
    BYTE PasswordBuffer[532] = {0};

    BAIL_ON_INVALID_POINTER(pPasswordBuffer);
    BAIL_ON_INVALID_POINTER(pwszPassword);
    BAIL_ON_INVALID_POINTER(pSessionKey);

    if (dwPasswordBufferSize < sizeof(PasswordBuffer))
    {
        dwError = ERROR_INSUFFICIENT_BUFFER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(&rc4_key, 0, sizeof(rc4_key));

    ntStatus = LsaEncodePasswordBuffer(pwszPassword,
                                       PasswordBuffer,
                                       sizeof(PasswordBuffer));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaRandBytes((unsigned char*)InitValue, sizeof(InitValue));
    BAIL_ON_LSA_ERROR(dwError);

    /* Session key should be 16 bytes according to Microsoft's MS-SAMR.pdf
     * in section 3.2.2.2 MD5 Usage. RC4 happens to have a 16 byte session key.
     */
    MD5_Init(&ctx);
    MD5_Update(&ctx, InitValue, 16);
    MD5_Update(&ctx, pSessionKey, dwSessionKeyLen > 16 ? 16 : dwSessionKeyLen);
    MD5_Final(DigestedSessKey, &ctx);

    RC4_set_key(&rc4_key, 16, (unsigned char*)DigestedSessKey);
    RC4(&rc4_key, 516, PasswordBuffer, PasswordBuffer);

    memcpy((PVOID)&PasswordBuffer[516], InitValue, 16);

    memcpy(pPasswordBuffer, PasswordBuffer, sizeof(PasswordBuffer));

cleanup:
    memset(PasswordBuffer, 0, sizeof(PasswordBuffer));

    if (ntStatus == STATUS_SUCCESS &&
        dwError != ERROR_SUCCESS)
    {
        ntStatus = LwWin32ErrorToNtStatus(dwError);
    }

    return ntStatus;

error:
    goto cleanup;
}


static
NTSTATUS
LsaEncodePasswordBuffer(
    IN  PCWSTR  pwszPassword,
    OUT PBYTE   pBlob,
    IN  DWORD   dwBlobSize
    )
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD dwError = ERROR_SUCCESS;
    size_t sPasswordLen = 0;
    DWORD dwPasswordSize = 0;
    PWSTR pwszPasswordLE = NULL;
    BYTE PasswordBlob[516] = {0};
    BYTE BlobInit[512] = {0};
    DWORD iByte = 0;

    BAIL_ON_INVALID_POINTER(pwszPassword);
    BAIL_ON_INVALID_POINTER(pBlob);

    if (dwBlobSize < sizeof(PasswordBlob))
    {
        dwError = ERROR_INSUFFICIENT_BUFFER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LwWc16sLen(pwszPassword, &sPasswordLen);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * Sanity check - password cannot be longer than the buffer size
     */
    if ((sPasswordLen * sizeof(pwszPassword[0])) >
        (sizeof(PasswordBlob) - sizeof(dwPasswordSize)))
    {
        dwError = ERROR_INVALID_PASSWORD;
        BAIL_ON_LSA_ERROR(dwError);
    }

    /* size doesn't include terminating zero here */
    dwPasswordSize = sPasswordLen * sizeof(pwszPassword[0]);

    /*
     * Make sure encoded password is 2-byte little-endian
     */
    dwError = LwAllocateMemory(dwPasswordSize + sizeof(pwszPassword[0]),
                               OUT_PPVOID(&pwszPasswordLE));
    BAIL_ON_LSA_ERROR(dwError);

    wc16stowc16les(pwszPasswordLE, pwszPassword, sPasswordLen);

    /*
     * Encode the password length (in bytes) in the last 4 bytes
     * as little-endian number
     */
    iByte = sizeof(PasswordBlob);
    PasswordBlob[--iByte] = (BYTE)((dwPasswordSize >> 24) & 0xff);
    PasswordBlob[--iByte] = (BYTE)((dwPasswordSize >> 16) & 0xff);
    PasswordBlob[--iByte] = (BYTE)((dwPasswordSize >> 8) & 0xff);
    PasswordBlob[--iByte] = (BYTE)((dwPasswordSize) & 0xff);

    /*
     * Copy the password and the initial random bytes
     */
    iByte -= dwPasswordSize;
    memcpy(&(PasswordBlob[iByte]), pwszPasswordLE, dwPasswordSize);

    /*
     * Fill the rest of the buffer with (pseudo) random mess
     * to increase security.
     */
    dwError = LsaRandBytes((unsigned char*)BlobInit, iByte);
    BAIL_ON_LSA_ERROR(dwError);

    memcpy(PasswordBlob, BlobInit, iByte);

    memcpy(pBlob, PasswordBlob, sizeof(PasswordBlob));

cleanup:
    memset(PasswordBlob, 0, sizeof(PasswordBlob));

    LW_SECURE_FREE_WSTRING(pwszPasswordLE);

    if (ntStatus == STATUS_SUCCESS &&
        dwError != ERROR_SUCCESS)
    {
        ntStatus = LwWin32ErrorToNtStatus(dwError);
    }

    return ntStatus;

error:
    if (pBlob)
    {
        memset(pBlob, 0, dwBlobSize);
    }

    goto cleanup;
}


static
DWORD
LsaSaveMachinePassword(
    PCWSTR  pwszMachineName,
    PCWSTR  pwszMachineAccountName,
    PCWSTR  pwszMachineDnsDomain,
    PCWSTR  pwszDomainName,
    PCWSTR  pwszDnsDomainName,
    PCWSTR  pwszDCName,
    PCWSTR  pwszSidStr,
    PCWSTR  pwszPassword
    )
{
    wchar_t wszHostFqdnFmt[] = L"host/%ws.%ws";
    wchar_t wszHostFmt[] = L"host/%ws";
    wchar_t wszCifsFqdnFmt[] = L"cifs/%ws.%ws";

    DWORD dwError = ERROR_SUCCESS;
    PWSTR pwszAccount = NULL;
    PWSTR pwszDomain = NULL;
    PWSTR pwszAdDnsDomainNameLc = NULL;
    PWSTR pwszAdDnsDomainNameUc = NULL;
    PWSTR pwszMachDnsDomainNameUc = NULL;
    PWSTR pwszMachDnsDomainNameLc = NULL;
    size_t sMachDnsDomainNameLcLen = 0;
    PWSTR pwszSid = NULL;
    PWSTR pwszHostnameUc = NULL;
    PWSTR pwszHostnameLc = NULL;
    size_t sHostnameLcLen = 0;
    PWSTR pwszPass = NULL;
    LSA_MACHINE_PASSWORD_INFO_W passwordInfo = { { 0 } };
    size_t sPassLen = 0;
    DWORD dwKvno = 0;
    PWSTR pwszBaseDn = NULL;
    PWSTR pwszSalt = NULL;
    /* various forms of principal name for keytab */
    PWSTR pwszHostMachineUc = NULL;
    PWSTR pwszHostMachineLc = NULL;
    PWSTR pwszHostMachineFqdn = NULL;
    size_t sHostMachineFqdnSize = 0;
    PWSTR pwszCifsMachineFqdn = NULL;
    size_t sCifsMachineFqdnSize = 0;
    PWSTR pwszPrincipal = NULL;
    PWSTR pwszFqdn = NULL;

    dwError = LwAllocateWc16String(&pwszAccount,
                                   pwszMachineAccountName);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateWc16String(&pwszDomain,
                                   pwszDomainName);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateWc16String(&pwszAdDnsDomainNameLc,
                                   pwszDnsDomainName);
    BAIL_ON_LSA_ERROR(dwError);

    LwWc16sToLower(pwszAdDnsDomainNameLc);

    dwError = LwAllocateWc16String(&pwszAdDnsDomainNameUc,
                                   pwszDnsDomainName);
    BAIL_ON_LSA_ERROR(dwError);

    LwWc16sToUpper(pwszAdDnsDomainNameUc);

    dwError = LwAllocateWc16String(&pwszMachDnsDomainNameUc,
                                   pwszMachineDnsDomain);
    BAIL_ON_LSA_ERROR(dwError);

    LwWc16sToUpper(pwszMachDnsDomainNameUc);

    dwError = LwAllocateWc16String(&pwszMachDnsDomainNameLc,
                                   pwszMachineDnsDomain);
    BAIL_ON_LSA_ERROR(dwError);

    LwWc16sToLower(pwszMachDnsDomainNameLc);

    dwError = LwAllocateWc16String(&pwszSid,
                                   pwszSidStr);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateWc16String(&pwszHostnameUc,
                                   pwszMachineName);
    BAIL_ON_LSA_ERROR(dwError);

    LwWc16sToUpper(pwszHostnameUc);

    dwError = LwAllocateWc16String(&pwszHostnameLc,
                                   pwszMachineName);
    BAIL_ON_LSA_ERROR(dwError);

    LwWc16sToLower(pwszHostnameLc);

    dwError = LwAllocateWc16sPrintfW(
                    &pwszFqdn,
                    L"%ws.%ws",
                    pwszHostnameLc,
                    pwszMachDnsDomainNameLc);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateWc16String(&pwszPass,
                                   pwszPassword);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * Find the current key version number for machine account
     */

    dwError = KtKrb5FormatPrincipalW(pwszAccount,
                                     pwszAdDnsDomainNameUc,
                                     &pwszPrincipal);
    BAIL_ON_LSA_ERROR(dwError);

    /* Get the directory base naming context first */
    dwError = KtLdapGetBaseDnW(pwszDCName, &pwszBaseDn);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = KtLdapGetKeyVersionW(pwszDCName,
                                   pwszBaseDn,
                                   pwszPrincipal,
                                   &dwKvno);
    if (dwError == ERROR_FILE_NOT_FOUND)
    {
        /*
         * This is probably win2k DC we're talking to, because it doesn't
         * store kvno in directory. In such case return default key version
         */
        dwKvno = 0;
        dwError = ERROR_SUCCESS;
    }
    else
    {
        BAIL_ON_LSA_ERROR(dwError);
    }

    /*
     * Store the machine password
     */

    passwordInfo.Account.DnsDomainName = pwszAdDnsDomainNameUc;
    passwordInfo.Account.NetbiosDomainName = pwszDomain;
    passwordInfo.Account.DomainSid = pwszSid;
    passwordInfo.Account.SamAccountName = pwszAccount;
    passwordInfo.Account.AccountFlags = LSA_MACHINE_ACCOUNT_TYPE_WORKSTATION;
    passwordInfo.Account.KeyVersionNumber = dwKvno;
    passwordInfo.Account.Fqdn = pwszFqdn;
    passwordInfo.Account.LastChangeTime = 0;
    passwordInfo.Password = pwszPass;

    dwError = LsaPstoreSetPasswordInfoW(&passwordInfo);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwWc16sLen(pwszPass, &sPassLen);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = KtKrb5GetSaltingPrincipalW(pwszMachineName,
                                         pwszAccount,
                                         pwszAdDnsDomainNameLc,
                                         pwszAdDnsDomainNameUc,
                                         pwszDCName,
                                         pwszBaseDn,
                                         &pwszSalt);
    BAIL_ON_LSA_ERROR(dwError);

    if (pwszSalt == NULL)
    {
        dwError = LwAllocateWc16String(&pwszSalt, pwszPrincipal);
        BAIL_ON_LSA_ERROR(dwError);
    }

    /*
     * Update keytab records with various forms of machine principal
     */

    /*
     * MACHINE$@DOMAIN.NET
     */
    dwError = LsaSavePrincipalKey(pwszAccount,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * host/MACHINE@DOMAIN.NET
     */

    dwError = LwAllocateWc16sPrintfW(&pwszHostMachineUc,
                                     wszHostFmt,
                                     pwszHostnameUc);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSavePrincipalKey(pwszHostMachineUc,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwWc16sLen(pwszHostnameLc,
                         &sHostnameLcLen);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwWc16sLen(pwszMachDnsDomainNameLc,
                         &sMachDnsDomainNameLcLen);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * host/machine.domain.net@DOMAIN.NET
     */
    sHostMachineFqdnSize = sHostnameLcLen + sMachDnsDomainNameLcLen +
                           (sizeof(wszHostFqdnFmt)/sizeof(wszHostFqdnFmt[0]));

    dwError = LwAllocateMemory(
                        sizeof(pwszHostMachineFqdn[0]) * sHostMachineFqdnSize,
                        OUT_PPVOID(&pwszHostMachineFqdn));
    BAIL_ON_LSA_ERROR(dwError);

    if (sw16printfw(
                pwszHostMachineFqdn,
                sHostMachineFqdnSize,
                wszHostFqdnFmt,
                pwszHostnameLc,
                pwszMachDnsDomainNameLc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszHostMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * host/MACHINE.DOMAIN.NET@DOMAIN.NET
     */
    if (sw16printfw(
                pwszHostMachineFqdn,
                sHostMachineFqdnSize,
                wszHostFqdnFmt,
                pwszHostnameUc,
                pwszMachDnsDomainNameUc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszHostMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * host/MACHINE.domain.net@DOMAIN.NET
     */
    if (sw16printfw(
                pwszHostMachineFqdn,
                sHostMachineFqdnSize,
                wszHostFqdnFmt,
                pwszHostnameUc,
                pwszMachDnsDomainNameLc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszHostMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * host/machine.DOMAIN.NET@DOMAIN.NET
     */
    if (sw16printfw(
                pwszHostMachineFqdn,
                sHostMachineFqdnSize,
                wszHostFqdnFmt,
                pwszHostnameLc,
                pwszMachDnsDomainNameUc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszHostMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * host/machine@DOMAIN.NET
     */
    dwError = LwAllocateWc16sPrintfW(&pwszHostMachineLc,
                                     wszHostFmt,
                                     pwszHostnameLc);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSavePrincipalKey(pwszHostMachineLc,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    sCifsMachineFqdnSize = sHostnameLcLen + sMachDnsDomainNameLcLen +
                           (sizeof(wszCifsFqdnFmt)/sizeof(wszCifsFqdnFmt[0]));

    /*
     * cifs/machine.domain.net@DOMAIN.NET
     */
    dwError = LwAllocateMemory(
                       sizeof(pwszCifsMachineFqdn[0]) * sCifsMachineFqdnSize,
                       OUT_PPVOID(&pwszCifsMachineFqdn));
    BAIL_ON_LSA_ERROR(dwError);

    if (sw16printfw(
                pwszCifsMachineFqdn,
                sCifsMachineFqdnSize,
                wszCifsFqdnFmt,
                pwszHostnameLc,
                pwszMachDnsDomainNameLc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszCifsMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * cifs/MACHINE.DOMAIN.NET@DOMAIN.NET
     */
    if (sw16printfw(
                pwszCifsMachineFqdn,
                sCifsMachineFqdnSize,
                wszCifsFqdnFmt,
                pwszHostnameUc,
                pwszMachDnsDomainNameUc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszCifsMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * cifs/MACHINE.domain.net@DOMAIN.NET
     */
    if (sw16printfw(
                pwszCifsMachineFqdn,
                sCifsMachineFqdnSize,
                wszCifsFqdnFmt,
                pwszHostnameUc,
                pwszMachDnsDomainNameLc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszCifsMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszAdDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * cifs/machine.DOMAIN.NET@DOMAIN.NET
     */
    if (sw16printfw(
                pwszCifsMachineFqdn,
                sCifsMachineFqdnSize,
                wszCifsFqdnFmt,
                pwszHostnameLc,
                pwszMachDnsDomainNameUc) < 0)
    {
        dwError = LwErrnoToWin32Error(errno);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaSavePrincipalKey(pwszCifsMachineFqdn,
                                  pwszPass,
                                  sPassLen,
                                  pwszMachDnsDomainNameUc,
                                  pwszSalt,
                                  pwszDCName,
                                  dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

cleanup:
    LW_SAFE_FREE_MEMORY(pwszBaseDn);
    LW_SAFE_FREE_MEMORY(pwszSalt);
    LW_SAFE_FREE_MEMORY(pwszDomain);
    LW_SAFE_FREE_MEMORY(pwszAdDnsDomainNameLc);
    LW_SAFE_FREE_MEMORY(pwszAdDnsDomainNameUc);
    LW_SAFE_FREE_MEMORY(pwszMachDnsDomainNameLc);
    LW_SAFE_FREE_MEMORY(pwszMachDnsDomainNameUc);
    LW_SAFE_FREE_MEMORY(pwszSid);
    LW_SAFE_FREE_MEMORY(pwszHostnameLc);
    LW_SAFE_FREE_MEMORY(pwszHostnameUc);
    LW_SAFE_FREE_MEMORY(pwszPass);
    LW_SAFE_FREE_MEMORY(pwszAccount);
    LW_SAFE_FREE_MEMORY(pwszHostMachineUc);
    LW_SAFE_FREE_MEMORY(pwszHostMachineLc);
    LW_SAFE_FREE_MEMORY(pwszHostMachineFqdn);
    LW_SAFE_FREE_MEMORY(pwszCifsMachineFqdn);
    LW_SAFE_FREE_MEMORY(pwszPrincipal);
    LW_SAFE_FREE_MEMORY(pwszFqdn);

    return dwError;

error:
    goto cleanup;
}


static
DWORD
LsaSavePrincipalKey(
    PCWSTR  pwszName,
    PCWSTR  pwszPassword,
    DWORD   dwPasswordLen,
    PCWSTR  pwszRealm,
    PCWSTR  pwszSalt,
    PCWSTR  pwszDCName,
    DWORD   dwKvno
    )
{
    DWORD dwError = ERROR_SUCCESS;
    PWSTR pwszPrincipal = NULL;

    BAIL_ON_INVALID_POINTER(pwszName);
    BAIL_ON_INVALID_POINTER(pwszPassword);
    BAIL_ON_INVALID_POINTER(pwszDCName);

    dwError = KtKrb5FormatPrincipalW(pwszName,
                                     pwszRealm,
                                     &pwszPrincipal);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = KtKrb5AddKeyW(pwszPrincipal,
                            (PVOID)pwszPassword,
                            dwPasswordLen,
                            NULL,
                            pwszSalt,
                            pwszDCName,
                            dwKvno);
    BAIL_ON_LSA_ERROR(dwError);

cleanup:
    LW_SAFE_FREE_MEMORY(pwszPrincipal);

    return dwError;

error:
    goto cleanup;
}


static
DWORD
LsaDirectoryConnect(
    const wchar16_t *domain,
    LDAP **ldconn,
    wchar16_t **dn_context
    )
{
    DWORD dwError = ERROR_SUCCESS;
    int lderr = LDAP_SUCCESS;
    int close_lderr = LDAP_SUCCESS;
    LDAP *ld = NULL;
    LDAPMessage *info = NULL;
    LDAPMessage *res = NULL;
    wchar16_t *dn_context_name = NULL;
    wchar16_t **dn_context_val = NULL;

    BAIL_ON_INVALID_POINTER(domain);
    BAIL_ON_INVALID_POINTER(ldconn);
    BAIL_ON_INVALID_POINTER(dn_context);

    *ldconn     = NULL;
    *dn_context = NULL;

    lderr = LdapInitConnection(&ld, domain, FALSE);
    BAIL_ON_LDAP_ERROR(lderr);

    lderr = LdapGetDirectoryInfo(&info, &res, ld);
    BAIL_ON_LDAP_ERROR(lderr);

    dwError = LwMbsToWc16s("defaultNamingContext",
                           &dn_context_name);
    BAIL_ON_LSA_ERROR(dwError);

    dn_context_val = LdapAttributeGet(ld, info, dn_context_name, NULL);
    if (dn_context_val == NULL) {
        /* TODO: find more descriptive error code */
        lderr = LDAP_NO_SUCH_ATTRIBUTE;
        BAIL_ON_LDAP_ERROR(lderr);

    }

    dwError = LwAllocateWc16String(dn_context,dn_context_val[0]);
    BAIL_ON_LSA_ERROR(dwError);

    *ldconn = ld;

cleanup:
    LW_SAFE_FREE_MEMORY(dn_context_name);

    if (dn_context_val) {
        LdapAttributeValueFree(dn_context_val);
    }

    if (res) {
        LdapMessageFree(res);
    }

    if (dwError == ERROR_SUCCESS &&
        lderr != 0)
    {
        dwError = LwMapLdapErrorToLwError(lderr);
    }

    return dwError;

error:
    if (ld) {
        close_lderr = LdapCloseConnection(ld);
        if (lderr == LDAP_SUCCESS &&
            close_lderr != STATUS_SUCCESS) {
            lderr = close_lderr;
        }
    }

    *dn_context = NULL;
    *ldconn     = NULL;
    goto cleanup;
}


static
DWORD
LsaDirectoryDisconnect(
    LDAP *ldconn
    )
{
    int lderr = LdapCloseConnection(ldconn);
    return LwMapLdapErrorToLwError(lderr);
}


static
DWORD
LsaMachAcctCreate(
    LDAP *ld,
    const wchar16_t *machine_name,
    const wchar16_t *machacct_name,
    const wchar16_t *ou,
    int rejoin
    )
{
    DWORD dwError = ERROR_SUCCESS;
    int lderr = LDAP_SUCCESS;
    LDAPMessage *machacct = NULL;
    LDAPMessage *res = NULL;
    LDAPMessage *info = NULL;
    wchar16_t *dn_context_name = NULL;
    wchar16_t **dn_context_val = NULL;
    wchar16_t *dn_name = NULL;
    wchar16_t **dn_val = NULL;

    BAIL_ON_INVALID_POINTER(ld);
    BAIL_ON_INVALID_POINTER(machine_name);
    BAIL_ON_INVALID_POINTER(machacct_name);
    BAIL_ON_INVALID_POINTER(ou);

    lderr = LdapMachAcctCreate(ld, machacct_name, ou);
    if (lderr == LDAP_ALREADY_EXISTS && rejoin) {
        lderr = LdapGetDirectoryInfo(&info, &res, ld);
        BAIL_ON_LDAP_ERROR(lderr);

        dwError = LwMbsToWc16s("defaultNamingContext",
                               &dn_context_name);
        BAIL_ON_LSA_ERROR(dwError);

        dn_context_val = LdapAttributeGet(ld, info, dn_context_name, NULL);
        if (dn_context_val == NULL) {
            /* TODO: find more descriptive error code */
            lderr = LDAP_NO_SUCH_ATTRIBUTE;
            goto error;
        }

        lderr = LdapMachAcctSearch(&machacct, ld, machacct_name,
                                   dn_context_val[0]);
        BAIL_ON_LDAP_ERROR(lderr);

        dwError = LwMbsToWc16s("distinguishedName",
                               &dn_name);
        BAIL_ON_LSA_ERROR(dwError);

        dn_val = LdapAttributeGet(ld, machacct, dn_name, NULL);
        if (dn_val == NULL) {
            dwError = LW_ERROR_LDAP_INSUFFICIENT_ACCESS;
            goto error;
        }

        lderr = LdapMachAcctMove(ld, dn_val[0], machine_name, ou);
        BAIL_ON_LDAP_ERROR(lderr);
    }

cleanup:
    LW_SAFE_FREE_MEMORY(dn_context_name);
    LW_SAFE_FREE_MEMORY(dn_name);

    if (res)
    {
        LdapMessageFree(res);
    }

    if (machacct)
    {
        LdapMessageFree(machacct);
    }

    if (dn_context_val) {
        LdapAttributeValueFree(dn_context_val);
    }

    if (dn_val) {
        LdapAttributeValueFree(dn_val);
    }

    if (dwError == ERROR_SUCCESS &&
        lderr != 0)
    {
        dwError = LwMapLdapErrorToLwError(lderr);
    }

    return dwError;

error:
    goto cleanup;
}


static
DWORD
LsaMachDnsNameSearch(
    LDAP *ldconn,
    const wchar16_t *name,
    const wchar16_t *dn_context,
    const wchar16_t *dns_domain_name,
    wchar16_t **samacct
    )
{
    DWORD dwError = ERROR_SUCCESS;
    int lderr = LDAP_SUCCESS;
    LDAPMessage *res = NULL;
    wchar16_t *samacct_attr_name = NULL;
    wchar16_t **samacct_attr_val = NULL;

    BAIL_ON_INVALID_POINTER(ldconn);
    BAIL_ON_INVALID_POINTER(name);
    BAIL_ON_INVALID_POINTER(dn_context);
    BAIL_ON_INVALID_POINTER(dns_domain_name);
    BAIL_ON_INVALID_POINTER(samacct);

    *samacct = NULL;

    lderr = LdapMachDnsNameSearch(
                &res,
                ldconn,
                name,
                dns_domain_name,
                dn_context);
    BAIL_ON_LDAP_ERROR(lderr);

    dwError = LwMbsToWc16s("sAMAccountName",
                           &samacct_attr_name);
    BAIL_ON_LSA_ERROR(dwError);

    samacct_attr_val = LdapAttributeGet(ldconn, res, samacct_attr_name, NULL);
    if (!samacct_attr_val) {
        lderr = LDAP_NO_SUCH_ATTRIBUTE;
        BAIL_ON_LDAP_ERROR(lderr);
    }

    dwError = LwAllocateWc16String(samacct, samacct_attr_val[0]);
    BAIL_ON_LSA_ERROR(dwError);

cleanup:
    LW_SAFE_FREE_MEMORY(samacct_attr_name);
    LdapAttributeValueFree(samacct_attr_val);

    if (res)
    {
        LdapMessageFree(res);
    }

    if (dwError == ERROR_SUCCESS &&
        lderr != 0)
    {
        dwError = LwMapLdapErrorToLwError(lderr);
    }

    return dwError;

error:

    *samacct = NULL;
    goto cleanup;
}


static
DWORD
LsaMachAcctSearch(
    LDAP *ldconn,
    const wchar16_t *name,
    const wchar16_t *dn_context,
    wchar16_t **pDn,
    wchar16_t **pDnsName
    )
{
    DWORD dwError = ERROR_SUCCESS;
    int lderr = LDAP_SUCCESS;
    LDAPMessage *res = NULL;
    wchar16_t *dn_attr_name = NULL;
    wchar16_t **dn_attr_val = NULL;
    wchar16_t *dns_name_attr_name = NULL;
    wchar16_t **dns_name_attr_val = NULL;
    wchar16_t *dn = NULL;
    wchar16_t *dnsName = NULL;

    BAIL_ON_INVALID_POINTER(ldconn);
    BAIL_ON_INVALID_POINTER(name);
    BAIL_ON_INVALID_POINTER(dn_context);

    lderr = LdapMachAcctSearch(&res, ldconn, name, dn_context);
    BAIL_ON_LDAP_ERROR(lderr);

    if (pDn)
    {
        dwError = LwMbsToWc16s("distinguishedName", &dn_attr_name);
        BAIL_ON_LSA_ERROR(dwError);

        dn_attr_val = LdapAttributeGet(ldconn, res, dn_attr_name, NULL);
        if (!dn_attr_val) {
            lderr = LDAP_NO_SUCH_ATTRIBUTE;
            BAIL_ON_LDAP_ERROR(lderr);
        }

        dwError = LwAllocateWc16String(&dn, dn_attr_val[0]);
        BAIL_ON_LSA_ERROR(dwError);
    }

    if (pDnsName)
    {
        dwError = LwMbsToWc16s("dNSHostName", &dns_name_attr_name);
        BAIL_ON_LSA_ERROR(dwError);

        dns_name_attr_val = LdapAttributeGet(
                                ldconn,
                                res,
                                dns_name_attr_name,
                                NULL);
        if (dns_name_attr_val) {
            dwError = LwAllocateWc16String(&dnsName, dns_name_attr_val[0]);
            BAIL_ON_LSA_ERROR(dwError);
        }
    }

cleanup:

    if (pDn)
    {
        *pDn = dn;
    }
    if (pDnsName)
    {
        *pDnsName = dnsName;
    }

    LW_SAFE_FREE_MEMORY(dn_attr_name);
    LdapAttributeValueFree(dn_attr_val);
    LW_SAFE_FREE_MEMORY(dns_name_attr_name);
    LdapAttributeValueFree(dns_name_attr_val);

    if (res) {
        LdapMessageFree(res);
    }

    if (dwError == ERROR_SUCCESS &&
        lderr != 0)
    {
        dwError = LwMapLdapErrorToLwError(lderr);
    }

    return dwError;

error:
    LW_SAFE_FREE_MEMORY(dn);
    LW_SAFE_FREE_MEMORY(dnsName);
    goto cleanup;
}


static
DWORD
LsaMachAcctSetAttribute(
    LDAP *ldconn,
    const wchar16_t *dn,
    const wchar16_t *attr_name,
    const wchar16_t **attr_val,
    int new
    )
{
    int lderr = LDAP_SUCCESS;

    lderr = LdapMachAcctSetAttribute(ldconn, dn, attr_name, attr_val, new);
    return LwMapLdapErrorToLwError(lderr);
}


DWORD
LsaGetRwDcName(
    const wchar16_t *DnsDomainName,
    BOOLEAN Force,
    wchar16_t** DomainControllerName
    )
{
    DWORD dwError = 0;
    wchar16_t *domain_controller_name = NULL;
    char *dns_domain_name_mbs = NULL;
    DWORD get_dc_name_flags = DS_WRITABLE_REQUIRED;
    PLWNET_DC_INFO pDC = NULL;

    if (Force)
    {
        get_dc_name_flags |= DS_FORCE_REDISCOVERY;
    }

    dwError = LwWc16sToMbs(DnsDomainName, &dns_domain_name_mbs);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LWNetGetDCName(NULL, dns_domain_name_mbs, NULL, get_dc_name_flags, &pDC);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwMbsToWc16s(pDC->pszDomainControllerName,
                           &domain_controller_name);
    BAIL_ON_LSA_ERROR(dwError)

cleanup:
    LW_SAFE_FREE_MEMORY(dns_domain_name_mbs);
    LWNET_SAFE_FREE_DC_INFO(pDC);
    if (dwError)
    {
        LW_SAFE_FREE_MEMORY(domain_controller_name);
    }

    *DomainControllerName = domain_controller_name;

    // ISSUE-2008/07/14-dalmeida -- Need to do error code conversion

    return dwError;

error:
    goto cleanup;
}


DWORD
LsaEnableDomainGroupMembership(
    PCSTR pszDomainName,
    PCSTR pszDomainSID
    )
{
    return LsaChangeDomainGroupMembership(pszDomainName,
                                          pszDomainSID,
					  TRUE);
}


DWORD
LsaChangeDomainGroupMembership(
    IN  PCSTR    pszDomainName,
    IN  PCSTR    pszDomainSID,
    IN  BOOLEAN  bEnable
    )
{
    DWORD dwError = ERROR_SUCCESS;
    NTSTATUS ntStatus = STATUS_SUCCESS;
    PSID pDomainSid = NULL;
    PSID pBuiltinAdminsSid = NULL;
    PSID pBuiltinUsersSid = NULL;
    PSID pDomainAdminsSid = NULL;
    PSID pDomainUsersSid = NULL;
    PSTR pszBuiltinAdminsSid = NULL;
    PSTR pszBuiltinUsersSid = NULL;
    PSTR pszDomainAdminsSid = NULL;
    PSTR pszDomainUsersSid = NULL;
    LSA_GROUP_MOD_INFO_2 adminsMods = {0};
    LSA_GROUP_MOD_INFO_2 usersMods = {0};
    PSTR pszTargetProvider = NULL;
    HANDLE hConnection = NULL;

    ntStatus = RtlAllocateSidFromCString(
                   &pDomainSid,
                   pszDomainSID);
    BAIL_ON_NT_STATUS(ntStatus);

    dwError = LwAllocateWellKnownSid(WinBuiltinAdministratorsSid,
                                   NULL,
                                   &pBuiltinAdminsSid,
                                   NULL);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateWellKnownSid(WinBuiltinUsersSid,
                                   NULL,
                                   &pBuiltinUsersSid,
                                   NULL);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateWellKnownSid(WinAccountDomainAdminsSid,
                                   pDomainSid,
                                   &pDomainAdminsSid,
                                   NULL);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwAllocateWellKnownSid(WinAccountDomainUsersSid,
                                   pDomainSid,
                                   &pDomainUsersSid,
                                   NULL);
    BAIL_ON_LSA_ERROR(dwError);

    ntStatus = RtlAllocateCStringFromSid(
                  &pszBuiltinAdminsSid,
                  pBuiltinAdminsSid);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = RtlAllocateCStringFromSid(
                  &pszBuiltinUsersSid,
                  pBuiltinUsersSid);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = RtlAllocateCStringFromSid(
                  &pszDomainAdminsSid,
                  pDomainAdminsSid);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = RtlAllocateCStringFromSid(
                  &pszDomainUsersSid,
                  pDomainUsersSid);
    BAIL_ON_NT_STATUS(ntStatus);

    adminsMods.pszSid = pszBuiltinAdminsSid;
    if (bEnable)
    {
        adminsMods.actions.bAddMembers = TRUE;
        adminsMods.dwAddMembersNum = 1;
        adminsMods.ppszAddMembers = &pszDomainAdminsSid;
    }
    else
    {
        adminsMods.actions.bRemoveMembers = TRUE;
        adminsMods.dwRemoveMembersNum = 1;
        adminsMods.ppszRemoveMembers = &pszDomainAdminsSid;
    }

    usersMods.pszSid = pszBuiltinUsersSid;
    if (bEnable)
    {
        usersMods.actions.bAddMembers = TRUE;
        usersMods.dwAddMembersNum = 1;
        usersMods.ppszAddMembers = &pszDomainUsersSid;
    }
    else
    {
        usersMods.actions.bRemoveMembers = TRUE;
        usersMods.dwRemoveMembersNum = 1;
        usersMods.ppszRemoveMembers = &pszDomainUsersSid;
    }

    dwError = LwAllocateStringPrintf(
                  &pszTargetProvider,
                  ":%s",
                  pszDomainName);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvOpenServer(0, 0, getpid(), &hConnection);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvModifyGroup2(
                  hConnection,
                  pszTargetProvider,
                  &adminsMods);
    if ((bEnable && (dwError == ERROR_MEMBER_IN_ALIAS ||
                     dwError == ERROR_MEMBER_IN_GROUP)) ||
        (!bEnable && (dwError == ERROR_MEMBER_NOT_IN_ALIAS ||
                      dwError == ERROR_MEMBER_NOT_IN_GROUP)))
    {
        dwError = 0;
    }
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaSrvModifyGroup2(
                  hConnection,
                  pszTargetProvider,
                  &usersMods);
    if ((bEnable && (dwError == ERROR_MEMBER_IN_ALIAS ||
                     dwError == ERROR_MEMBER_IN_GROUP)) ||
        (!bEnable && (dwError == ERROR_MEMBER_NOT_IN_ALIAS ||
                      dwError == ERROR_MEMBER_NOT_IN_GROUP)))
    {
        dwError = 0;
    }
    BAIL_ON_LSA_ERROR(dwError);

error:
    LW_SAFE_FREE_MEMORY(pDomainSid);
    LW_SAFE_FREE_MEMORY(pBuiltinAdminsSid);
    LW_SAFE_FREE_MEMORY(pBuiltinUsersSid);
    LW_SAFE_FREE_MEMORY(pDomainAdminsSid);
    LW_SAFE_FREE_MEMORY(pDomainUsersSid);
    LW_SAFE_FREE_MEMORY(pszBuiltinAdminsSid);
    LW_SAFE_FREE_MEMORY(pszBuiltinUsersSid);
    LW_SAFE_FREE_MEMORY(pszDomainAdminsSid);
    LW_SAFE_FREE_MEMORY(pszDomainUsersSid);
    LW_SAFE_FREE_MEMORY(pszTargetProvider);

    if (hConnection)
    {
        LsaSrvCloseServer(hConnection);
    }

    if (dwError == ERROR_SUCCESS &&
        ntStatus != STATUS_SUCCESS)
    {
        dwError = LwNtStatusToWin32Error(ntStatus);
    }

    return dwError;
}


DWORD
LsaMachineChangePassword(
    IN OPTIONAL PCSTR pszDnsDomainName
    )
{
    DWORD dwError = ERROR_SUCCESS;
    PWSTR pwszDnsDomainName = NULL;
    PWSTR pwszDCName = NULL;
    size_t sDCNameLen = 0;
    PLSA_MACHINE_PASSWORD_INFO_W pPasswordInfo = NULL;
    PWSTR pwszUserName = NULL;
    PWSTR pwszOldPassword = NULL;
    WCHAR wszNewPassword[MACHPASS_LEN+1];
    PWSTR pwszHostname = NULL;
    PCWSTR pwszFqdnSuffix = NULL;
    int i = 0;

    memset(wszNewPassword, 0, sizeof(wszNewPassword));

    if (pszDnsDomainName)
    {
        dwError = LwMbsToWc16s(pszDnsDomainName, &pwszDnsDomainName);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaPstoreGetPasswordInfoW(pwszDnsDomainName, &pPasswordInfo);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaGetRwDcName(pPasswordInfo->Account.DnsDomainName, FALSE,
                            &pwszDCName);
    BAIL_ON_LSA_ERROR(dwError);

    pwszUserName     = pPasswordInfo->Account.SamAccountName;
    pwszOldPassword  = pPasswordInfo->Password;

    dwError = LwAllocateWc16String(&pwszHostname, pPasswordInfo->Account.Fqdn);
    BAIL_ON_LSA_ERROR(dwError);

    for (i = 0; pwszHostname[i]; i++)
    {
        if ('.' == pwszHostname[i])
        {
            pwszHostname[i] = 0;
            pwszFqdnSuffix = &pwszHostname[i+1];
            break;
        }
    }

    LsaGenerateMachinePassword(
                  wszNewPassword,
                  sizeof(wszNewPassword)/sizeof(wszNewPassword[0]));

    dwError = LwWc16sLen(pwszDCName, &sDCNameLen);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaUserChangePassword(pwszDCName,
                                    pwszUserName,
                                    pwszOldPassword,
                                    (PWSTR)wszNewPassword);
    BAIL_ON_LSA_ERROR(dwError);

    // TODO-2010/01/10-dalmeida -- Simplify this calling sequence
    // by using keytab plugin in lsapstore...
    dwError = LsaSaveMachinePassword(
                    pwszHostname,
                    pPasswordInfo->Account.SamAccountName,
                    pwszFqdnSuffix ? pwszFqdnSuffix : pPasswordInfo->Account.DnsDomainName,
                    pPasswordInfo->Account.NetbiosDomainName,
                    pPasswordInfo->Account.DnsDomainName,
                    pwszDCName,
                    pPasswordInfo->Account.DomainSid,
                    wszNewPassword);
    BAIL_ON_LSA_ERROR(dwError);

error:
    LW_SAFE_FREE_MEMORY(pwszDCName);
    LW_SAFE_FREE_MEMORY(pwszHostname);
    LSA_PSTORE_FREE_PASSWORD_INFO_W(&pPasswordInfo);
    LW_SAFE_FREE_MEMORY(pwszDnsDomainName);

    return dwError;
}


DWORD
LsaUserChangePassword(
    PWSTR  pwszDCName,
    PWSTR  pwszUserName,
    PWSTR  pwszOldPassword,
    PWSTR  pwszNewPassword
    )
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD dwError = ERROR_SUCCESS;
    SAMR_BINDING hSamrBinding = NULL;
    size_t sOldPasswordLen = 0;
    size_t sNewPasswordLen = 0;
    BYTE OldNtHash[16] = {0};
    BYTE NewNtHash[16] = {0};
    BYTE NtPasswordBuffer[516] = {0};
    BYTE NtVerHash[16] = {0};
    RC4_KEY RC4Key;
    PIO_CREDS pCreds = NULL;

    ntStatus = LwIoGetActiveCreds(NULL, &pCreds);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = SamrInitBindingDefault(&hSamrBinding, pwszDCName, pCreds);
    BAIL_ON_NT_STATUS(ntStatus);

    dwError = LwWc16sLen(pwszOldPassword, &sOldPasswordLen);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LwWc16sLen(pwszNewPassword, &sNewPasswordLen);
    BAIL_ON_LSA_ERROR(dwError);

    /* prepare NT password hashes */
    dwError = LsaGetNtPasswordHash(pwszOldPassword,
                                   OldNtHash,
                                   sizeof(OldNtHash));
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaGetNtPasswordHash(pwszNewPassword,
                                   NewNtHash,
                                   sizeof(NewNtHash));
    BAIL_ON_LSA_ERROR(dwError);

    /* encode password buffer */
    dwError = LsaEncodePasswordBuffer(pwszNewPassword,
                                      NtPasswordBuffer,
                                      sizeof(NtPasswordBuffer));
    BAIL_ON_LSA_ERROR(dwError);

    RC4_set_key(&RC4Key, 16, (unsigned char*)OldNtHash);
    RC4(&RC4Key, sizeof(NtPasswordBuffer), NtPasswordBuffer, NtPasswordBuffer);

    /* encode NT verifier */
    dwError = LsaEncryptNtHashVerifier(NewNtHash, sizeof(NewNtHash),
                                       OldNtHash, sizeof(OldNtHash),
                                       NtVerHash, sizeof(NtVerHash));
    BAIL_ON_LSA_ERROR(dwError);

    ntStatus = SamrChangePasswordUser2(hSamrBinding,
                                       pwszDCName,
                                       pwszUserName,
                                       NtPasswordBuffer,
                                       NtVerHash,
                                       0,
                                       NULL,
                                       NULL);
    BAIL_ON_NT_STATUS(ntStatus);

cleanup:
    if (hSamrBinding)
    {
        SamrFreeBinding(&hSamrBinding);
    }

    memset(OldNtHash, 0, sizeof(OldNtHash));
    memset(NewNtHash, 0, sizeof(NewNtHash));
    memset(NtPasswordBuffer, 0, sizeof(NtPasswordBuffer));

    if (pCreds)
    {
        LwIoDeleteCreds(pCreds);
    }

    if (dwError == ERROR_SUCCESS &&
        ntStatus != STATUS_SUCCESS)
    {
        dwError = NtStatusToWin32Error(ntStatus);
    }

    return dwError;

error:
    goto cleanup;
}


static
DWORD
LsaGetNtPasswordHash(
    IN  PCWSTR  pwszPassword,
    OUT PBYTE   pNtHash,
    IN  DWORD   dwNtHashSize
    )
{
    DWORD dwError = ERROR_SUCCESS;
    size_t sPasswordLen = 0;
    PWSTR pwszPasswordLE = NULL;
    BYTE Hash[16] = {0};

    BAIL_ON_INVALID_POINTER(pwszPassword);
    BAIL_ON_INVALID_POINTER(pNtHash);

    if (dwNtHashSize < sizeof(Hash))
    {
        dwError = ERROR_INSUFFICIENT_BUFFER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LwWc16sLen(pwszPassword, &sPasswordLen);
    BAIL_ON_LSA_ERROR(dwError);

    /*
     * Make sure the password is 2-byte little-endian
     */
    dwError = LwAllocateMemory((sPasswordLen + 1) * sizeof(pwszPasswordLE[0]),
                               OUT_PPVOID(&pwszPasswordLE));
    BAIL_ON_LSA_ERROR(dwError);

    wc16stowc16les(pwszPasswordLE, pwszPassword, sPasswordLen);

    MD4((PBYTE)pwszPasswordLE,
        sPasswordLen * sizeof(pwszPasswordLE[0]),
        Hash);

    memcpy(pNtHash, Hash, sizeof(Hash));

cleanup:
    LW_SECURE_FREE_WSTRING(pwszPasswordLE);

    memset(Hash, 0, sizeof(Hash));

    return dwError;

error:
    memset(pNtHash, 0, dwNtHashSize);

    goto cleanup;
}


static
DWORD
LsaEncryptNtHashVerifier(
    IN  PBYTE    pNewNtHash,
    IN  DWORD    dwNewNtHashLen,
    IN  PBYTE    pOldNtHash,
    IN  DWORD    dwOldNtHashLen,
    OUT PBYTE    pNtVerifier,
    IN  DWORD    dwNtVerifierSize
    )
{
    DWORD dwError = ERROR_SUCCESS;
    DES_cblock KeyBlockLo;
    DES_cblock KeyBlockHi;
    DES_key_schedule KeyLo;
    DES_key_schedule KeyHi;
    BYTE Verifier[16] = {0};

    BAIL_ON_INVALID_POINTER(pNewNtHash);
    BAIL_ON_INVALID_POINTER(pOldNtHash);
    BAIL_ON_INVALID_POINTER(pNtVerifier);

    if (dwNtVerifierSize < sizeof(Verifier))
    {
        dwError = ERROR_INSUFFICIENT_BUFFER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    memset(&KeyBlockLo, 0, sizeof(KeyBlockLo));
    memset(&KeyBlockHi, 0, sizeof(KeyBlockHi));
    memset(&KeyLo, 0, sizeof(KeyLo));
    memset(&KeyHi, 0, sizeof(KeyHi));

    dwError = LsaPrepareDesKey(&pNewNtHash[0],
			       (PBYTE)KeyBlockLo);
    BAIL_ON_LSA_ERROR(dwError);

    DES_set_odd_parity(&KeyBlockLo);
    DES_set_key_unchecked(&KeyBlockLo, &KeyLo);

    dwError = LsaPrepareDesKey(&pNewNtHash[7],
			       (PBYTE)KeyBlockHi);
    BAIL_ON_LSA_ERROR(dwError);

    DES_set_odd_parity(&KeyBlockHi);
    DES_set_key_unchecked(&KeyBlockHi, &KeyHi);

    DES_ecb_encrypt((DES_cblock*)&pOldNtHash[0],
                    (DES_cblock*)&Verifier[0],
                    &KeyLo,
                    DES_ENCRYPT);
    DES_ecb_encrypt((DES_cblock*)&pOldNtHash[8],
                    (DES_cblock*)&Verifier[8],
                    &KeyHi,
                    DES_ENCRYPT);

    memcpy(pNtVerifier, Verifier, sizeof(Verifier));

cleanup:
    memset(&KeyBlockLo, 0, sizeof(KeyBlockLo));
    memset(&KeyBlockHi, 0, sizeof(KeyBlockHi));
    memset(&KeyLo, 0, sizeof(KeyLo));
    memset(&KeyHi, 0, sizeof(KeyHi));

    return dwError;

error:
    goto cleanup;
}


static
DWORD
LsaPrepareDesKey(
    IN  PBYTE  pInput,
    OUT PBYTE  pOutput
    )
{
    DWORD dwError = ERROR_SUCCESS;
    DWORD i = 0;

    BAIL_ON_INVALID_POINTER(pInput);
    BAIL_ON_INVALID_POINTER(pOutput);

    /*
     * Expand the input 7x8 bits so that each 7 bits are
     * appended with 1 bit space for parity bit and yield
     * 8x8 bits ready to become a DES key
     */
    pOutput[0] = pInput[0] >> 1;
    pOutput[1] = ((pInput[0]&0x01) << 6) | (pInput[1] >> 2);
    pOutput[2] = ((pInput[1]&0x03) << 5) | (pInput[2] >> 3);
    pOutput[3] = ((pInput[2]&0x07) << 4) | (pInput[3] >> 4);
    pOutput[4] = ((pInput[3]&0x0F) << 3) | (pInput[4] >> 5);
    pOutput[5] = ((pInput[4]&0x1F) << 2) | (pInput[5] >> 6);
    pOutput[6] = ((pInput[5]&0x3F) << 1) | (pInput[6] >> 7);
    pOutput[7] = pInput[6]&0x7F;

    for (i = 0; i < 8; i++)
    {
        pOutput[i] = pOutput[i] << 1;
    }

cleanup:
    return dwError;

error:
    goto cleanup;
}





/*
local variables:
mode: c
c-basic-offset: 4
indent-tabs-mode: nil
tab-width: 4
end:
*/
