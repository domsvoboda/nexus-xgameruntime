/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <ctime>
#include <string>
#include <vector>
namespace nexus_store_context
{
struct AddonLicenseData
{
    std::string sku, offer;
    INT64 expiration;
    bool trial = false;
};
struct AddonLicenseSnapshot
{
    std::vector<AddonLicenseData> licenses;
    INT64 proof_until = 0;
    bool valid() const
    {
        return proof_until > std::time(nullptr);
    }
};
} // namespace nexus_store_context
