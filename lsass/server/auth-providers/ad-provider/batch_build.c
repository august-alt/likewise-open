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
 *        batch_build.c
 *
 * Abstract:
 *
 *        Likewise Security and Authentication Subsystem (LSASS)
 *
 *        Active Directory Authentication Provider
 *
 * Authors: Danilo Almeida (dalmeida@likewisesoftware.com)
 *          Wei Fu (wfu@likewisesoftware.com)
 *
 */

#include "adprovider.h"
#include "batch_build.h"

typedef DWORD (*LSA_AD_BATCH_BUILDER_GET_ATTR_VALUE_CALLBACK)(
    IN PVOID pCallbackContext,
    IN PVOID pItem,
    OUT PCSTR* ppszValue,
    OUT PVOID* ppFreeValueContext
    );

typedef VOID (*LSA_AD_BATCH_BUILDER_FREE_VALUE_CONTEXT_CALLBACK)(
    IN PVOID pCallbackContext,
    IN OUT PVOID* ppFreeValueContext
    );

typedef PVOID (*LSA_AD_BATCH_BUILDER_NEXT_ITEM_CALLBACK)(
    IN PVOID pCallbackContext,
    IN PVOID pItem
    );

static
DWORD
LsaAdBatchBuilderAppend(
    IN OUT PDWORD pdwQueryOffset,
    IN OUT PSTR pszQuery,
    IN DWORD dwQuerySize,
    IN PCSTR pszAppend,
    IN DWORD dwAppendLength
    )
{
    DWORD dwError = 0;
    DWORD dwQueryOffset = *pdwQueryOffset;
    DWORD dwNewQueryOffset = 0;

    if (dwAppendLength > 0)
    {
        dwNewQueryOffset = dwQueryOffset + dwAppendLength;
        if (dwNewQueryOffset < dwQueryOffset)
        {
            // overflow
            dwError = LSA_ERROR_DATA_ERROR;
            BAIL_ON_LSA_ERROR(dwError);
        }
        else if (dwNewQueryOffset - 1 >= dwQuerySize)
        {
            // overflow
            dwError = LSA_ERROR_DATA_ERROR;
            BAIL_ON_LSA_ERROR(dwError);
        }
        memcpy(pszQuery + dwQueryOffset, pszAppend, dwAppendLength);
        pszQuery[dwNewQueryOffset] = 0;
        *pdwQueryOffset = dwNewQueryOffset;
    }

cleanup:
    return dwError;

error:
    goto cleanup;
}

static
DWORD
LsaAdBatchBuilderCreateQuery(
    IN PCSTR pszQueryPrefix,
    IN PCSTR pszQuerySuffix,
    IN PCSTR pszAttributeName,
    IN PVOID pFirstItem,
    IN PVOID pEndItem,
    OUT PVOID* ppNextItem,
    IN OPTIONAL PVOID pCallbackContext,
    IN LSA_AD_BATCH_BUILDER_GET_ATTR_VALUE_CALLBACK pGetAttributeValueCallback,
    IN OPTIONAL LSA_AD_BATCH_BUILDER_FREE_VALUE_CONTEXT_CALLBACK pFreeValueContextCallback,
    IN LSA_AD_BATCH_BUILDER_NEXT_ITEM_CALLBACK pNextItemCallback,
    IN DWORD dwMaxQuerySize,
    IN DWORD dwMaxQueryCount,
    OUT PDWORD pdwQueryCount,
    OUT PSTR* ppszQuery
    )
{
    DWORD dwError = 0;
    PVOID pCurrentItem = NULL;
    PSTR pszQuery = NULL;
    PVOID pLastItem = pFirstItem;
    const char szOrPrefix[] = "(|";
    const char szOrSuffix[] = ")";
    const DWORD dwOrPrefixLength = sizeof(szOrPrefix)-1;
    const DWORD dwOrSuffixLength = sizeof(szOrSuffix)-1;
    DWORD dwAttributeNameLength = strlen(pszAttributeName);
    DWORD dwQuerySize = 0;
    DWORD dwQueryCount = 0;
    PVOID pFreeValueContext = NULL;
    DWORD dwQueryPrefixLength = 0;
    DWORD dwQuerySuffixLength = 0;
    DWORD dwQueryOffset = 0;
    DWORD dwSavedQueryCount = 0;

    if (pszQueryPrefix)
    {
        dwQueryPrefixLength = strlen(pszQueryPrefix);
    }
    if (pszQuerySuffix)
    {
        dwQuerySuffixLength = strlen(pszQuerySuffix);
    }

    // The overhead is:
    // prefix + orPrefix + <CONTENT> + orSuffix + suffix + NULL
    dwQuerySize = dwQueryPrefixLength + dwOrPrefixLength + dwOrSuffixLength + dwQuerySuffixLength + 1;

    pCurrentItem = pFirstItem;
    while (pCurrentItem != pEndItem)
    {
        PCSTR pszAttributeValue = NULL;

        if (pFreeValueContext)
        {
            pFreeValueContextCallback(pCallbackContext, &pFreeValueContext);
        }

        dwError = pGetAttributeValueCallback(
                        pCallbackContext,
                        pCurrentItem,
                        &pszAttributeValue,
                        &pFreeValueContext);
        BAIL_ON_LSA_ERROR(dwError);

        if (pszAttributeValue)
        {
            // "(" + attributeName + "=" + attributeValue + ")"
            DWORD dwAttributeValueLength = strlen(pszAttributeValue);
            DWORD dwItemLength = (1 + dwAttributeNameLength + 1 + dwAttributeValueLength + 1);
            DWORD dwNewQuerySize = dwQuerySize + dwItemLength;
            DWORD dwNewQueryCount = dwQueryCount + 1;

            if (dwNewQuerySize < dwQuerySize)
            {
                // overflow
                dwError = LSA_ERROR_DATA_ERROR;
                BAIL_ON_LSA_ERROR(dwError);
            }
            if (dwMaxQuerySize && (dwNewQuerySize > dwMaxQuerySize))
            {
                break;
            }
            if (dwMaxQueryCount && (dwNewQueryCount > dwMaxQueryCount))
            {
                break;
            }
            dwQuerySize = dwNewQuerySize;
            dwQueryCount = dwNewQueryCount;
        }

        pCurrentItem = pNextItemCallback(pCallbackContext, pCurrentItem);
    }
    pLastItem = pCurrentItem;
    dwSavedQueryCount = dwQueryCount;

    if (dwQueryCount < 1)
    {
        goto cleanup;
    }

    dwError = LsaAllocateMemory(dwQuerySize, (PVOID*)&pszQuery);
    BAIL_ON_LSA_ERROR(dwError);

    // Set up the query
    dwQueryOffset = 0;

    dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                      pszQueryPrefix, dwQueryPrefixLength);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                      szOrPrefix, dwOrPrefixLength);
    BAIL_ON_LSA_ERROR(dwError);

    dwQueryCount = 0;
    pCurrentItem = pFirstItem;
    while (pCurrentItem != pLastItem)
    {
        PCSTR pszAttributeValue = NULL;

        if (pFreeValueContext)
        {
            pFreeValueContextCallback(pCallbackContext, &pFreeValueContext);
        }

        dwError = pGetAttributeValueCallback(
                        pCallbackContext,
                        pCurrentItem,
                        &pszAttributeValue,
                        &pFreeValueContext);
        BAIL_ON_LSA_ERROR(dwError);

        if (pszAttributeValue)
        {
            DWORD dwAttributeValueLength = strlen(pszAttributeValue);

            dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                              "(", 1);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                              pszAttributeName, dwAttributeNameLength);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                              "=", 1);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                              pszAttributeValue, dwAttributeValueLength);
            BAIL_ON_LSA_ERROR(dwError);

            dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                              ")", 1);
            BAIL_ON_LSA_ERROR(dwError);

            dwQueryCount++;
        }

        pCurrentItem = pNextItemCallback(pCallbackContext, pCurrentItem);
    }

    dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                      szOrSuffix, dwOrSuffixLength);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaAdBatchBuilderAppend(&dwQueryOffset, pszQuery, dwQuerySize,
                                      pszQuerySuffix, dwQuerySuffixLength);
    BAIL_ON_LSA_ERROR(dwError);

    assert(dwQueryOffset + 1 == dwQuerySize);
    assert(dwSavedQueryCount == dwQueryCount);

cleanup:
    // We handle error here instead of error label
    // because there is a goto cleanup above.
    if (dwError)
    {
        LSA_SAFE_FREE_STRING(pszQuery);
        dwQueryCount = 0;
        pLastItem = pFirstItem;
    }

    if (pFreeValueContext)
    {
        pFreeValueContextCallback(pCallbackContext, &pFreeValueContext);
    }

    *ppszQuery = pszQuery;
    *pdwQueryCount = dwQueryCount;
    *ppNextItem = pLastItem;

    return dwError;

error:
    // Do not actually handle any errors here.
    goto cleanup;
}

typedef struct _LSA_AD_BATCH_BUILDER_BATCH_ITEM_CONTEXT {
    LSA_AD_BATCH_QUERY_TYPE QueryType;
    BOOLEAN bIsForRealObject;
    // Buffer needs to contain 32-bit unsigned number in decimal.
    // That's 10 digits plus a terminating NULL.
    char szIdBuffer[11];
} LSA_AD_BATCH_BUILDER_BATCH_ITEM_CONTEXT, *PLSA_AD_BATCH_BUILDER_BATCH_ITEM_CONTEXT;

static
DWORD
LsaAdBatchBuilderBatchItemGetAttributeValue(
    IN PVOID pCallbackContext,
    IN PVOID pItem,
    OUT PCSTR* ppszValue,
    OUT PVOID* ppFreeValueContext
    )
{
    DWORD dwError = 0;
    PLSA_AD_BATCH_BUILDER_BATCH_ITEM_CONTEXT pContext = (PLSA_AD_BATCH_BUILDER_BATCH_ITEM_CONTEXT)pCallbackContext;
    LSA_AD_BATCH_QUERY_TYPE QueryType = pContext->QueryType;
    BOOLEAN bIsForRealObject = pContext->bIsForRealObject;
    PLSA_LIST_LINKS pLinks = (PLSA_LIST_LINKS)pItem;
    PLSA_AD_BATCH_ITEM pBatchItem = LW_STRUCT_FROM_FIELD(pLinks, LSA_AD_BATCH_ITEM, BatchItemListLinks);
    PSTR pszValueToEscape = NULL;
    PSTR pszValue = NULL;
    PVOID pFreeValueContext = NULL;
    BOOLEAN bHaveReal = IsSetFlag(pBatchItem->Flags, LSA_AD_BATCH_ITEM_FLAG_HAVE_REAL);
    BOOLEAN bHavePseudo = IsSetFlag(pBatchItem->Flags, LSA_AD_BATCH_ITEM_FLAG_HAVE_PSEUDO);

    if ((bIsForRealObject && bHaveReal) ||
        (!bIsForRealObject && bHavePseudo))
    {
        // This can only happen in the linked cells case, but even
        // that should go away in the future as we just keep an
        // unresolved batch items list.
        LSA_ASSERT(!bIsForRealObject && (gpADProviderData->dwDirectoryMode == CELL_MODE));
        goto cleanup;
    }

    switch (QueryType)
    {
        case LSA_AD_BATCH_QUERY_TYPE_BY_DN:
            // Must be looking for real object
            LSA_ASSERT(bIsForRealObject);
            LSA_ASSERT(QueryType == pBatchItem->QueryTerm.Type);

            pszValueToEscape = (PSTR)pBatchItem->QueryTerm.pszString;
            LSA_ASSERT(pszValueToEscape);
            break;

        case LSA_AD_BATCH_QUERY_TYPE_BY_SID:
            if (pBatchItem->pszSid)
            {
                // This is case where we already got the SID by resolving
                // the pseudo object by id/alias.
                pszValue = pBatchItem->pszSid;
            }
            else if (QueryType == pBatchItem->QueryTerm.Type)
            {
                // This is the case where the original query was
                // a query by SID.
                pszValue = (PSTR)pBatchItem->QueryTerm.pszString;
                LSA_ASSERT(pszValue);
            }
            // Will be NULL if we cannot find a SID for which to query.
            // This can happen if this batch item is for an object
            // that we did not find real but are trying to look up pseudo.
            // In that case, we have NULL and will just skip it.

            // If we have a SID string, make sure it looks like a SID.
            // Note that we must do some sanity checking to make sure
            // that the string does not need escaping since we are
            // not setting pszValueToEscape.  (The SID check takes
            // care of that.)
            if (pszValue && !LsaAdBatchHasValidCharsForSid(pszValue))
            {
                LSA_ASSERT(FALSE);
                dwError = LSA_ERROR_INTERNAL;
                BAIL_ON_LSA_ERROR(dwError);
            }
            break;

        case LSA_AD_BATCH_QUERY_TYPE_BY_NT4:
            LSA_ASSERT(bIsForRealObject);
            if (pBatchItem->pszSamAccountName)
            {
                // Unprovisioned id/alias case where id mapper returned
                // a SAM account name (and domain name) but no SID.
                pszValueToEscape = pBatchItem->pszSamAccountName;
                // However, we currently do not have this sort of id mapper
                // support, so we LSA_ASSERT(FALSE) for now.
                LSA_ASSERT(FALSE);
            }
            else if (QueryType == pBatchItem->QueryTerm.Type)
            {
                pszValueToEscape = (PSTR)pBatchItem->QueryTerm.pszString;
                LSA_ASSERT(pszValueToEscape);
                // The query term will already have the domain stripped out.
                LSA_ASSERT(!index(pszValueToEscape, '\\'));
            }
            break;

        case LSA_AD_BATCH_QUERY_TYPE_BY_UID:
        case LSA_AD_BATCH_QUERY_TYPE_BY_GID:
            LSA_ASSERT(!bIsForRealObject);
            LSA_ASSERT(QueryType == pBatchItem->QueryTerm.Type);

            snprintf(pContext->szIdBuffer, sizeof(pContext->szIdBuffer),
                     "%u", (unsigned int)pBatchItem->QueryTerm.dwId);
            // There should have been enough space for the NULL termination.
            LSA_ASSERT(!pContext->szIdBuffer[sizeof(pContext->szIdBuffer) - 1]);
            pszValue = pContext->szIdBuffer;
            break;

        case LSA_AD_BATCH_QUERY_TYPE_BY_USER_ALIAS:
        case LSA_AD_BATCH_QUERY_TYPE_BY_GROUP_ALIAS:
            LSA_ASSERT(!bIsForRealObject);
            LSA_ASSERT(QueryType == pBatchItem->QueryTerm.Type);

            pszValueToEscape = (PSTR)pBatchItem->QueryTerm.pszString;
            LSA_ASSERT(pszValueToEscape);
            break;

        default:
            dwError = LSA_ERROR_INVALID_PARAMETER;
            BAIL_ON_LSA_ERROR(dwError);
    }

    if (pszValueToEscape)
    {
        LSA_ASSERT(!pszValue);
        dwError = LsaLdapEscapeString(&pszValue, pszValueToEscape);
        BAIL_ON_LSA_ERROR(dwError);

        pFreeValueContext = pszValue;
    }

cleanup:
    // Note that the match value is different from the value,
    // which we need to escape.
    pBatchItem->pszQueryMatchTerm = pszValueToEscape ? pszValueToEscape : pszValue;
    *ppszValue = pszValue;
    *ppFreeValueContext = pFreeValueContext;

    return dwError;

error:
    // assing output in cleanup in case of goto cleanup in function.
    pszValueToEscape = NULL;
    pszValue = NULL;
    LSA_SAFE_FREE_STRING(pFreeValueContext);
    goto cleanup;
}

static
VOID
LsaAdBatchBuilderGenericFreeValueContext(
    IN PVOID pCallbackContext,
    IN OUT PVOID* ppFreeValueContext
    )
{
    LSA_SAFE_FREE_MEMORY(*ppFreeValueContext);
}

static
PVOID
LsaAdBatchBuilderBatchItemNextItem(
    IN PVOID pCallbackContext,
    IN PVOID pItem
    )
{
    PLSA_LIST_LINKS pLinks = (PLSA_LIST_LINKS)pItem;
    return pLinks->Next;
}

#define AD_LDAP_QUERY_LW_USER  "(keywords=objectClass=centerisLikewiseUser)"
#define AD_LDAP_QUERY_LW_GROUP "(keywords=objectClass=centerisLikewiseGroup)"
#define AD_LDAP_QUERY_SCHEMA_USER "(objectClass=posixAccount)"
#define AD_LDAP_QUERY_SCHEMA_GROUP "(objectClass=posixGroup)"
#define AD_LDAP_QUERY_NON_SCHEMA "(objectClass=serviceConnectionPoint)"

static
PCSTR
LsaAdBatchBuilderGetPseudoQueryPrefix(
    IN BOOLEAN bIsSchemaMode,
    IN LSA_AD_BATCH_OBJECT_TYPE ObjectType,
    OUT PCSTR* ppszSuffix
    )
{
    PCSTR pszPrefix = NULL;

    if (bIsSchemaMode)
    {
        switch (ObjectType)
        {
            case LSA_AD_BATCH_OBJECT_TYPE_USER:
                pszPrefix =
                    "(&"
                    "(&" AD_LDAP_QUERY_SCHEMA_USER AD_LDAP_QUERY_LW_USER ")"
                    "";
                break;
            case LSA_AD_BATCH_OBJECT_TYPE_GROUP:
                pszPrefix =
                    "(&"
                    "(&" AD_LDAP_QUERY_SCHEMA_GROUP AD_LDAP_QUERY_LW_GROUP ")"
                    "";
                break;
            default:
                pszPrefix =
                    "(&"
                    "(|"
                    "(&" AD_LDAP_QUERY_SCHEMA_USER AD_LDAP_QUERY_LW_USER ")"
                    "(&" AD_LDAP_QUERY_SCHEMA_GROUP AD_LDAP_QUERY_LW_GROUP ")"
                    ")"
                    "";
        }
    }
    else
    {
        switch (ObjectType)
        {
            case LSA_AD_BATCH_OBJECT_TYPE_USER:
                pszPrefix =
                    "(&"
                    AD_LDAP_QUERY_NON_SCHEMA
                    AD_LDAP_QUERY_LW_USER
                    "";
                break;
            case LSA_AD_BATCH_OBJECT_TYPE_GROUP:
                pszPrefix =
                    "(&"
                    AD_LDAP_QUERY_NON_SCHEMA
                    AD_LDAP_QUERY_LW_GROUP
                    "";
                break;
            default:
                pszPrefix =
                    "(&"
                    AD_LDAP_QUERY_NON_SCHEMA
                    "(|"
                    AD_LDAP_QUERY_LW_USER
                    AD_LDAP_QUERY_LW_GROUP
                    ")"
                    "";
        }
    }

    *ppszSuffix = pszPrefix ? ")" : NULL;
    return pszPrefix;
}

static
PCSTR
LsaAdBatchBuilderGetPseudoQueryAttribute(
    IN BOOLEAN bIsSchemaMode,
    IN LSA_AD_BATCH_QUERY_TYPE QueryType
    )
{
    PCSTR pszAttributeName = NULL;

    switch (QueryType)
    {
        case LSA_AD_BATCH_QUERY_TYPE_BY_SID:
            pszAttributeName = "keywords=backLink";
            break;
        case LSA_AD_BATCH_QUERY_TYPE_BY_UID:
            if (bIsSchemaMode)
            {
                pszAttributeName = AD_LDAP_UID_TAG;
            }
            else
            {
                pszAttributeName = "keywords=" AD_LDAP_UID_TAG;
            }
            break;
        case LSA_AD_BATCH_QUERY_TYPE_BY_GID:
            if (bIsSchemaMode)
            {
                pszAttributeName = AD_LDAP_GID_TAG;
            }
            else
            {
                pszAttributeName = "keywords=" AD_LDAP_GID_TAG;
            }
            break;
        case LSA_AD_BATCH_QUERY_TYPE_BY_USER_ALIAS:
            if (bIsSchemaMode)
            {
                pszAttributeName = AD_LDAP_ALIAS_TAG;
            }
            else
            {
                pszAttributeName = "keywords=" AD_LDAP_ALIAS_TAG;
            }
            break;
        case LSA_AD_BATCH_QUERY_TYPE_BY_GROUP_ALIAS:
            if (bIsSchemaMode)
            {
                pszAttributeName = AD_LDAP_DISPLAY_NAME_TAG;
            }
            else
            {
                pszAttributeName = "keywords=" AD_LDAP_DISPLAY_NAME_TAG;
            }
            break;
    }

    return pszAttributeName;
}

DWORD
LsaAdBatchBuildQueryForRpc(
    // List of PLSA_AD_BATCH_ITEM
    IN PLSA_LIST_LINKS pFirstLinks,
    IN PLSA_LIST_LINKS pEndLinks,
    OUT PLSA_LIST_LINKS* ppNextLinks,
    IN DWORD dwMaxQueryCount,
    OUT PDWORD pdwQueryCount,
    OUT PSTR** pppszQueryList
    )
{
    DWORD dwError = 0;
    PLSA_LIST_LINKS pLinks = NULL;
    PSTR* ppszQueryList = NULL;
    PLSA_LIST_LINKS pLastLinks = pFirstLinks;
    DWORD dwQueryCount = 0;
    DWORD dwSavedQueryCount = 0;

    pLinks = pFirstLinks;
    for (pLinks = pFirstLinks; pLinks != pEndLinks; pLinks = pLinks->Next)
    {
        PLSA_AD_BATCH_ITEM pEntry = LW_STRUCT_FROM_FIELD(pLinks, LSA_AD_BATCH_ITEM, BatchItemListLinks);

        if (!IsNullOrEmptyString(pEntry->QueryTerm.pszString))
        {
            DWORD dwNewQueryCount = dwQueryCount + 1;

            if (dwMaxQueryCount && (dwNewQueryCount > dwMaxQueryCount))
            {
                break;
            }
            dwQueryCount = dwNewQueryCount;
        }
    }
    pLastLinks = pLinks;
    dwSavedQueryCount = dwQueryCount;

    if (dwQueryCount < 1)
    {
        goto cleanup;
    }

    dwError = LsaAllocateMemory(dwQueryCount*sizeof(*ppszQueryList), (PVOID*)&ppszQueryList);
    BAIL_ON_LSA_ERROR(dwError);

    dwQueryCount = 0;
    // Loop until we reach last links.
    for (pLinks = pFirstLinks; pLinks != pLastLinks; pLinks = pLinks->Next)
    {
        PLSA_AD_BATCH_ITEM pEntry = LW_STRUCT_FROM_FIELD(pLinks, LSA_AD_BATCH_ITEM, BatchItemListLinks);

        if (!IsNullOrEmptyString(pEntry->QueryTerm.pszString))
        {
            dwError = LsaAllocateString(pEntry->QueryTerm.pszString,
                                        &ppszQueryList[dwQueryCount]);
            BAIL_ON_LSA_ERROR(dwError);
            dwQueryCount++;
        }
    }

    assert(dwSavedQueryCount == dwQueryCount);

cleanup:
    // We handle error here instead of error label
    // because there is a goto cleanup above.
    if (dwError)
    {
        LsaFreeStringArray(ppszQueryList, dwSavedQueryCount);
        dwQueryCount = 0;
        dwSavedQueryCount = 0;
        pLastLinks = pFirstLinks;
    }

    *pppszQueryList = ppszQueryList;
    *pdwQueryCount = dwQueryCount;
    *ppNextLinks = pLastLinks;

    return dwError;

error:
    // Do not actually handle any errors here.
    goto cleanup;
}

DWORD
LsaAdBatchBuildQueryForReal(
    IN LSA_AD_BATCH_QUERY_TYPE QueryType,
    // List of PLSA_AD_BATCH_ITEM
    IN PLSA_LIST_LINKS pFirstLinks,
    IN PLSA_LIST_LINKS pEndLinks,
    OUT PLSA_LIST_LINKS* ppNextLinks,
    IN DWORD dwMaxQuerySize,
    IN DWORD dwMaxQueryCount,
    OUT PDWORD pdwQueryCount,
    OUT PSTR* ppszQuery
    )
{
    DWORD dwError = 0;
    PLSA_LIST_LINKS pNextLinks = NULL;
    DWORD dwQueryCount = 0;
    PSTR pszQuery = NULL;
    PCSTR pszAttributeName = NULL;
    PCSTR pszPrefix = NULL;
    LSA_AD_BATCH_BUILDER_BATCH_ITEM_CONTEXT context = { 0 };

    switch (QueryType)
    {
        case LSA_AD_BATCH_QUERY_TYPE_BY_DN:
            pszAttributeName = AD_LDAP_DN_TAG;
            break;
        case LSA_AD_BATCH_QUERY_TYPE_BY_SID:
            pszAttributeName = AD_LDAP_OBJECTSID_TAG;
            break;
        case LSA_AD_BATCH_QUERY_TYPE_BY_NT4:
            pszAttributeName = AD_LDAP_SAM_NAME_TAG;
            break;
        default:
            dwError = LSA_ERROR_INVALID_PARAMETER;
            BAIL_ON_LSA_ERROR(dwError);
    }

    // In Default/schema case, filter out disabled users
    // when querying real objects.
    if ((gpADProviderData->dwDirectoryMode == DEFAULT_MODE) &&
        (gpADProviderData->adConfigurationMode == SchemaMode))
    {
        pszPrefix = "(&(|(&(objectClass=user)(uidNumber=*))(objectClass=group))(!(objectClass=computer))";
    }
    else
    {
        pszPrefix = "(&(|(objectClass=user)(objectClass=group))(!(objectClass=computer))";
    }

    context.QueryType = QueryType;
    context.bIsForRealObject = FALSE;

    dwError = LsaAdBatchBuilderCreateQuery(
                    pszPrefix,
                    ")",
                    pszAttributeName,
                    pFirstLinks,
                    pEndLinks,
                    (PVOID*)&pNextLinks,
                    &context,
                    LsaAdBatchBuilderBatchItemGetAttributeValue,
                    LsaAdBatchBuilderGenericFreeValueContext,
                    LsaAdBatchBuilderBatchItemNextItem,
                    dwMaxQuerySize,
                    dwMaxQueryCount,
                    &dwQueryCount,
                    &pszQuery);
    BAIL_ON_LSA_ERROR(dwError);

cleanup:
    *ppNextLinks = pNextLinks;
    *pdwQueryCount = dwQueryCount;
    *ppszQuery = pszQuery;
    return dwError;

error:
    // set output on cleanup
    pNextLinks = pFirstLinks;
    dwQueryCount = 0;
    LSA_SAFE_FREE_STRING(pszQuery);
    goto cleanup;
}


DWORD
LsaAdBatchBuildQueryForPseudo(
    IN BOOLEAN bIsSchemaMode,
    IN LSA_AD_BATCH_QUERY_TYPE QueryType,
    // List of PLSA_AD_BATCH_ITEM
    IN PLSA_LIST_LINKS pFirstLinks,
    IN PLSA_LIST_LINKS pEndLinks,
    OUT PLSA_LIST_LINKS* ppNextLinks,
    IN DWORD dwMaxQuerySize,
    IN DWORD dwMaxQueryCount,
    OUT PDWORD pdwQueryCount,
    OUT PSTR* ppszQuery
    )
{
    DWORD dwError = 0;
    PLSA_LIST_LINKS pNextLinks = NULL;
    DWORD dwQueryCount = 0;
    PSTR pszQuery = NULL;
    PCSTR pszAttributeName = NULL;
    PCSTR pszPrefix = NULL;
    PCSTR pszSuffix = NULL;
    LSA_AD_BATCH_BUILDER_BATCH_ITEM_CONTEXT context = { 0 };

    pszAttributeName = LsaAdBatchBuilderGetPseudoQueryAttribute(
                            bIsSchemaMode,
                            QueryType);
    if (!pszAttributeName)
    {
        LSA_ASSERT(FALSE);
        dwError = LSA_ERROR_INVALID_PARAMETER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    pszPrefix = LsaAdBatchBuilderGetPseudoQueryPrefix(
                        bIsSchemaMode,
                        LsaAdBatchGetObjectTypeFromQueryType(QueryType),
                        &pszSuffix);
    if (!pszPrefix || !pszSuffix)
    {
        LSA_ASSERT(FALSE);
        dwError = LSA_ERROR_INVALID_PARAMETER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    context.QueryType = QueryType;
    context.bIsForRealObject = FALSE;

    dwError = LsaAdBatchBuilderCreateQuery(
                    pszPrefix,
                    pszSuffix,
                    pszAttributeName,
                    pFirstLinks,
                    pEndLinks,
                    (PVOID*)&pNextLinks,
                    &context,
                    LsaAdBatchBuilderBatchItemGetAttributeValue,
                    LsaAdBatchBuilderGenericFreeValueContext,
                    LsaAdBatchBuilderBatchItemNextItem,
                    dwMaxQuerySize,
                    dwMaxQueryCount,
                    &dwQueryCount,
                    &pszQuery);
    BAIL_ON_LSA_ERROR(dwError);

cleanup:
    *ppNextLinks = pNextLinks;
    *pdwQueryCount = dwQueryCount;
    *ppszQuery = pszQuery;
    return dwError;

error:
    // set output on cleanup
    pNextLinks = pFirstLinks;
    dwQueryCount = 0;
    LSA_SAFE_FREE_STRING(pszQuery);
    goto cleanup;
}

