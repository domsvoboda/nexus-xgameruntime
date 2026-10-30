/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Included after Entitled.h. Only receipt-verified snapshots may be projected.
 * This helper is also compiled by the account-free signed-receipt tests. */
#pragma once
#include "StoreLicenseData.h"
#include <new>
#include <algorithm>
#include <utility>
namespace nexus_store_context
{
static HRESULT addon_data_from_verified_snapshot(const entitled_snapshot *snapshot, AddonLicenseSnapshot *out)
{
    if (!out)
        return E_POINTER;
    *out = {};
    if (!snapshot || snapshot->proof_until <= (UINT64)std::time(nullptr))
        return E_ACCESSDENIED;
    if (snapshot->proof_until > 910692730085ULL || snapshot->count > ENTITLED_MAX_PRODUCTS ||
        (snapshot->count && !snapshot->products))
        return E_UNEXPECTED;
    AddonLicenseSnapshot result;
    result.proof_until = snapshot->proof_until;
    try
    {
        for (UINT32 i = 0; i < snapshot->count; ++i)
        {
            const auto &product = snapshot->products[i];
            if (product.productKind != XStoreProductKind::Durable || !product.isInUserCollection)
                continue;
            if (!entitled_id(product.storeId) || product.skusCount > ENTITLED_MAX_SKUS ||
                (product.skusCount && !product.skus) || !product.inAppOfferToken ||
                strlen(product.inAppOfferToken) > 4096)
                return E_UNEXPECTED;
            for (UINT32 j = 0; j < product.skusCount; ++j)
            {
                const auto &sku = product.skus[j];
                if (!sku.isInUserCollection || sku.collectionData.endDate <= std::time(nullptr))
                    continue;
                if (!entitled_sku_id(sku.skuId) || result.licenses.size() == 4096)
                    return E_UNEXPECTED;
                AddonLicenseData value;
                value.sku = std::string(product.storeId) + "/" + sku.skuId;
                value.offer = product.inAppOfferToken;
                value.expiration = std::min<INT64>(snapshot->proof_until, sku.collectionData.endDate);
                result.licenses.push_back(std::move(value));
            }
        }
        *out = std::move(result);
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}
} // namespace nexus_store_context
