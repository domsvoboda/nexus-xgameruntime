// SPDX-License-Identifier: LGPL-2.1-or-later
// Account-free projection tests. Synthetic records never reach the transport.
// clang-format off
#define INITGUID
#include "../../src/private.h"
#include <xstore.h>
#include <cstdio>
#include <ctime>
#include "../../src/System/Store/StoreContext.h"
// clang-format on
namespace nexus_store_context
{
static HRESULT query_game_license(XStoreGameLicense *out, const std::atomic<bool> &)
{
    *out = {};
    return E_ACCESSDENIED;
}
static HRESULT query_addon_licenses(AddonLicenseSnapshot *out)
{
    *out = {};
    return E_ACCESSDENIED;
}
} // namespace nexus_store_context
using namespace nexus_store_context;
static unsigned checks, failures;
#define CHECK(e)                                                                                                  \
    do                                                                                                            \
    {                                                                                                             \
        ++checks;                                                                                                 \
        if (!(e))                                                                                                 \
        {                                                                                                         \
            ++failures;                                                                                           \
            printf("FAIL line=%d\n", __LINE__);                                                                   \
        }                                                                                                         \
    } while (0)
static AddonLicenseSnapshot fixture()
{
    AddonLicenseSnapshot value;
    value.proof_until = std::time(nullptr) + 120;
    value.licenses = {{"B00000000001/0001", "first", std::time(nullptr) + 60},
                      {"B00000000002/0002", "second", std::time(nullptr) + 90}};
    return value;
}
static bool text(HSTRING s, const WCHAR *expected)
{
    UINT32 n;
    const WCHAR *p = WindowsGetStringRawBuffer(s, &n);
    return n == wcslen(expected) && !wmemcmp(p, expected, n);
}
int main()
{
    CHECK(make_addon_license_map(fixture(), nullptr) == E_POINTER);
    IInspectable *raw = nullptr;
    CHECK(make_addon_license_map(fixture(), &raw) == S_OK && raw);
    if (!raw)
        return 1;
    ILicenseMap *map = nullptr;
    ILicenseIterable *iterable = nullptr;
    CHECK(raw->QueryInterface(IID_LicenseMap, (void **)&map) == S_OK && map);
    CHECK(raw->QueryInterface(IID_LicenseIterable, (void **)&iterable) == S_OK && iterable);
    IUnknown *a = nullptr, *b = nullptr;
    CHECK(map->QueryInterface(IID_IUnknown, (void **)&a) == S_OK);
    CHECK(iterable->QueryInterface(IID_IUnknown, (void **)&b) == S_OK && a == b);
    a->Release();
    b->Release();
    raw->Release();
    UINT32 n = 999;
    CHECK(map->Size(&n) == S_OK && n == 2);
    CHECK(map->Size(nullptr) == E_POINTER);
    HSTRING key = nullptr;
    CHECK(WindowsCreateString(L"B00000000001/0001", 17, &key) == S_OK);
    boolean present = false;
    CHECK(map->HasKey(key, &present) == S_OK && present);
    IAddonLicense *license = nullptr;
    CHECK(map->Lookup(key, &license) == S_OK && license);
    CHECK(map->Lookup(key, nullptr) == E_POINTER);
    CHECK(map->HasKey(key, nullptr) == E_POINTER);
    WindowsDeleteString(key);
    key = nullptr;
    CHECK(license->Sku(&key) == S_OK && text(key, L"B00000000001/0001"));
    WindowsDeleteString(key);
    CHECK(license->Offer(&key) == S_OK && text(key, L"first"));
    WindowsDeleteString(key);
    CHECK(license->Active(&present) == S_OK && present);
    INT64 expiration = 0;
    CHECK(license->Expiration(&expiration) == S_OK && expiration > 116444736000000000LL);
    CHECK(license->Sku(nullptr) == E_POINTER);
    CHECK(license->Offer(nullptr) == E_POINTER);
    CHECK(license->Expiration(nullptr) == E_POINTER);
    CHECK(license->Active(nullptr) == E_POINTER);
    key = (HSTRING)1;
    CHECK(license->Json(&key) == E_NOTIMPL && !key);
    license->Release();
    CHECK(map->HasKey(nullptr, &present) == S_OK && !present);
    license = (IAddonLicense *)1;
    CHECK(map->Lookup(nullptr, &license) == LICENSE_E_BOUNDS && !license);
    ILicenseIterator *it = nullptr;
    CHECK(iterable->First(&it) == S_OK && it);
    CHECK(iterable->First(nullptr) == E_POINTER);
    ILicensePair *pair = nullptr;
    CHECK(it->Current(&pair) == S_OK && pair);
    CHECK(pair->Key(&key) == S_OK && text(key, L"B00000000001/0001"));
    WindowsDeleteString(key);
    CHECK(pair->Value(&license) == S_OK && license);
    license->Release();
    CHECK(pair->Key(nullptr) == E_POINTER);
    CHECK(pair->Value(nullptr) == E_POINTER);
    pair->Release();
    CHECK(it->HasCurrent(&present) == S_OK && present);
    CHECK(it->MoveNext(&present) == S_OK && present);
    CHECK(it->Current(&pair) == S_OK && pair);
    CHECK(pair->Key(&key) == S_OK && text(key, L"B00000000002/0002"));
    WindowsDeleteString(key);
    pair->Release();
    CHECK(it->MoveNext(&present) == S_OK && !present);
    pair = (ILicensePair *)1;
    CHECK(it->Current(&pair) == LICENSE_E_BOUNDS && !pair);
    CHECK(it->MoveNext(&present) == S_OK && !present);
    CHECK(it->GetMany(0, nullptr, &n) == S_OK && n == 0);
    CHECK(it->GetMany(1, nullptr, &n) == E_POINTER);
    CHECK(it->GetMany(0, nullptr, nullptr) == E_POINTER);
    CHECK(it->Current(nullptr) == E_POINTER);
    CHECK(it->HasCurrent(nullptr) == E_POINTER);
    CHECK(it->MoveNext(nullptr) == E_POINTER);
    it->Release();
    CHECK(iterable->First(&it) == S_OK);
    ILicensePair *many[3] = {};
    CHECK(it->GetMany(3, many, &n) == S_OK && n == 2 && many[0] && many[1]);
    for (UINT32 i = 0; i < n; ++i)
        many[i]->Release();
    CHECK(it->HasCurrent(&present) == S_OK && !present);
    it->Release();
    ILicenseMap *left = nullptr, *right = nullptr;
    CHECK(map->Split(&left, &right) == S_OK && left && right);
    CHECK(map->Split(&left, &left) == E_INVALIDARG);
    CHECK(left->Size(&n) == S_OK && n == 1);
    CHECK(right->Size(&n) == S_OK && n == 1);
    CHECK(left->Split(nullptr, &right) == E_POINTER);
    left->Release();
    right->Release();
    iterable->Release();
    map->Release();
    AddonLicenseSnapshot empty;
    empty.proof_until = std::time(nullptr) + 60;
    CHECK(make_addon_license_map(empty, &raw) == S_OK && raw);
    CHECK(raw->QueryInterface(IID_LicenseMap, (void **)&map) == S_OK);
    raw->Release();
    CHECK(map->Size(&n) == S_OK && n == 0);
    left = right = (ILicenseMap *)1;
    CHECK(map->Split(&left, &right) == S_OK && !left && !right);
    map->Release();
    auto bad = fixture();
    bad.proof_until = 0;
    raw = (IInspectable *)1;
    CHECK(make_addon_license_map(bad, &raw) == E_ACCESSDENIED && !raw);
    bad = fixture();
    bad.licenses[0].expiration = std::time(nullptr) - 1;
    CHECK(make_addon_license_map(bad, &raw) == E_UNEXPECTED && !raw);
    bad = fixture();
    bad.licenses[1].sku = bad.licenses[0].sku;
    CHECK(make_addon_license_map(bad, &raw) == E_UNEXPECTED && !raw);
    bad = fixture();
    bad.licenses[0].sku = "invalid";
    CHECK(make_addon_license_map(bad, &raw) == E_UNEXPECTED && !raw);
    AppLicense inactive({});
    raw = (IInspectable *)1;
    CHECK(inactive.get_AddOnLicenses(&raw) == E_ACCESSDENIED && !raw);
    XStoreGameLicense active = {};
    active.isActive = true;
    active.expirationDate = std::time(nullptr) + 60;
    AppLicense unavailable(active);
    raw = (IInspectable *)1;
    CHECK(unavailable.get_AddOnLicenses(&raw) == E_ACCESSDENIED && !raw);
    // Shared immutable lifetime survives map release. Expired proof refuses
    // enumeration.
    auto shared = std::make_shared<AddonLicenseSnapshot>(fixture());
    map = new LicenseMap(shared, 0, 2);
    CHECK(map->QueryInterface(IID_LicenseIterable, (void **)&iterable) == S_OK);
    CHECK(iterable->First(&it) == S_OK);
    map->Release();
    iterable->Release();
    CHECK(it->Current(&pair) == S_OK && pair);
    pair->Release();
    shared->proof_until = std::time(nullptr) - 1;
    CHECK(it->HasCurrent(&present) == E_ACCESSDENIED && !present);
    it->Release();
    printf("%u checks %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
