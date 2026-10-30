/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Read-only WinRT projection of verified durable SKU licenses. Included inside
 * nexus_store_context, after make_hstring and INSPECTABLE_STUBS. No transport,
 * account fixture, catalog ownership inference or fallback lives in this file.
 */
static const HRESULT LICENSE_E_BOUNDS = static_cast<HRESULT>(0x8000000b);
static HRESULT query_addon_licenses(AddonLicenseSnapshot *out);
static const GUID IID_StoreLicense = {
    0x26dc9579, 0x4c4f, 0x4f30, {0xbc, 0x89, 0x64, 0x9f, 0x60, 0xe3, 0x60, 0x55}};
/* UUIDv5 WinRT signatures, HSTRING -> Windows.Services.Store.StoreLicense. */
static const GUID IID_LicenseMap = {0x7d0d74f5, 0x4020, 0x54aa, {0x9f, 0x3d, 0x0f, 0x17, 0x12, 0x7a, 0xcd, 0xdf}};
static const GUID IID_LicensePair = {0x33eefc64, 0xef0c, 0x5c8d, {0xb6, 0x20, 0x47, 0x6e, 0xdf, 0x7d, 0xf7, 0x99}};
static const GUID IID_LicenseIterable = {
    0xca8ba445, 0x6f4d, 0x5da9, {0x95, 0xee, 0x42, 0xcf, 0x11, 0x8d, 0xef, 0x63}};
static const GUID IID_LicenseIterator = {
    0xa1509348, 0x6522, 0x5062, {0xae, 0x86, 0xcf, 0x59, 0x54, 0x75, 0x92, 0x6d}};
struct IAddonLicense : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Sku(HSTRING *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Active(boolean *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Expiration(INT64 *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Json(HSTRING *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Offer(HSTRING *) = 0;
};
struct ILicensePair : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Key(HSTRING *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Value(IAddonLicense **) = 0;
};
struct ILicenseIterator : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Current(ILicensePair **) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasCurrent(boolean *) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveNext(boolean *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, ILicensePair **, UINT32 *) = 0;
};
struct ILicenseIterable : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE First(ILicenseIterator **) = 0;
};
struct ILicenseMap : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Lookup(HSTRING, IAddonLicense **) = 0;
    virtual HRESULT STDMETHODCALLTYPE Size(UINT32 *) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasKey(HSTRING, boolean *) = 0;
    virtual HRESULT STDMETHODCALLTYPE Split(ILicenseMap **, ILicenseMap **) = 0;
};
static HRESULT license_string(const std::string &text, HSTRING *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (text.size() > 32768)
        return E_INVALIDARG;
    if (text.empty())
        return WindowsCreateString(nullptr, 0, out);
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), text.size(), nullptr, 0);
    if (!count)
        return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
    try
    {
        std::vector<WCHAR> wide(count);
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), text.size(), wide.data(), count) !=
            count)
            return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        return WindowsCreateString(wide.data(), count, out);
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
}

static std::string license_json_string(const std::string &text)
{
    static const char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += static_cast<char>(c);
        }
        else if (c < 0x20)
        {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        }
        else
            out += static_cast<char>(c);
    }
    out += '"';
    return out;
}

/* Microsoft StoreLicense.ExtendedJsonData is the productAddOns object in
 * https://learn.microsoft.com/windows/uwp/monetize/data-schemas-for-store-products
 * All seven fields project this same verified durable SKU, never catalog-only
 * ownership, an opaque account receipt, or a longer lifetime than its proof. */
static HRESULT license_json(const AddonLicenseData &value, HSTRING *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (value.expiration <= std::time(nullptr))
        return E_ACCESSDENIED;
    if (value.expiration > 253402300799LL)
        return E_INVALIDARG; /* Four-digit UTC year. */
    ULARGE_INTEGER ticks;
    ticks.QuadPart = (value.expiration + 11644473600LL) * 10000000ULL;
    FILETIME ft = {ticks.LowPart, ticks.HighPart};
    SYSTEMTIME time;
    if (!FileTimeToSystemTime(&ft, &time))
        return HRESULT_FROM_WIN32(GetLastError());
    char date[32];
    std::snprintf(date, sizeof(date), "%04u-%02u-%02u %02u:%02u:%02u", (unsigned)time.wYear, (unsigned)time.wMonth,
                  (unsigned)time.wDay, (unsigned)time.wHour, (unsigned)time.wMinute, (unsigned)time.wSecond);
    try
    {
        const std::string json =
            "{\"productId\":" + license_json_string(value.sku.substr(0, 12)) +
            ",\"skuId\":" + license_json_string(value.sku.substr(13, 4)) +
            ",\"productType\":\"Durable\",\"skuType\":" + license_json_string(value.trial ? "Trial" : "Full") +
            ",\"inAppOfferToken\":" + license_json_string(value.offer) +
            ",\"expiration\":" + license_json_string(date) + ",\"isActive\":true}";
        return license_string(json, out);
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
}

#define LICENSE_OBJECT_METHODS(type, iid, runtime_name)                                                           \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override                                      \
    {                                                                                                             \
        if (!out)                                                                                                 \
            return E_POINTER;                                                                                     \
        *out = nullptr;                                                                                           \
        if (!IsEqualGUID(id, IID_IUnknown) && !IsEqualGUID(id, IID_IInspectable) &&                               \
            !IsEqualGUID(id, IID_IAgileObject_) && !IsEqualGUID(id, iid))                                         \
            return E_NOINTERFACE;                                                                                 \
        *out = static_cast<type *>(this);                                                                         \
        AddRef();                                                                                                 \
        return S_OK;                                                                                              \
    }                                                                                                             \
    ULONG STDMETHODCALLTYPE AddRef() override                                                                     \
    {                                                                                                             \
        return ++refs;                                                                                            \
    }                                                                                                             \
    ULONG STDMETHODCALLTYPE Release() override                                                                    \
    {                                                                                                             \
        ULONG n = --refs;                                                                                         \
        if (!n)                                                                                                   \
            delete this;                                                                                          \
        return n;                                                                                                 \
    }                                                                                                             \
    INSPECTABLE_STUBS                                                                                             \
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override                                         \
    {                                                                                                             \
        return make_hstring(runtime_name, name);                                                                  \
    }
using LicenseSnapshot = std::shared_ptr<const AddonLicenseSnapshot>;
class AddonLicense final : public IAddonLicense
{
    std::atomic<ULONG> refs{1};
    LicenseSnapshot snapshot;
    UINT32 index;

  public:
    AddonLicense(LicenseSnapshot value, UINT32 i) : snapshot(std::move(value)), index(i)
    {
    }
    LICENSE_OBJECT_METHODS(IAddonLicense, IID_StoreLicense, "Windows.Services.Store.StoreLicense")
    HRESULT STDMETHODCALLTYPE Sku(HSTRING *v) override
    {
        return license_string(snapshot->licenses[index].sku, v);
    }
    HRESULT STDMETHODCALLTYPE Active(boolean *v) override
    {
        if (!v)
            return E_POINTER;
        *v = snapshot->valid() && snapshot->licenses[index].expiration > std::time(nullptr);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Expiration(INT64 *v) override
    {
        if (!v)
            return E_POINTER;
        *v = (snapshot->licenses[index].expiration + 11644473600LL) * 10000000LL;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Json(HSTRING *v) override
    {
        if (!v)
            return E_POINTER;
        *v = nullptr;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        return license_json(snapshot->licenses[index], v);
    }
    HRESULT STDMETHODCALLTYPE Offer(HSTRING *v) override
    {
        return license_string(snapshot->licenses[index].offer, v);
    }
};
class LicensePair final : public ILicensePair
{
    std::atomic<ULONG> refs{1};
    LicenseSnapshot snapshot;
    UINT32 index;

  public:
    LicensePair(LicenseSnapshot value, UINT32 i) : snapshot(std::move(value)), index(i)
    {
    }
    LICENSE_OBJECT_METHODS(ILicensePair, IID_LicensePair, "Windows.Foundation.Collections.IKeyValuePair`2")
    HRESULT STDMETHODCALLTYPE Key(HSTRING *v) override
    {
        return license_string(snapshot->licenses[index].sku, v);
    }
    HRESULT STDMETHODCALLTYPE Value(IAddonLicense **v) override
    {
        if (!v)
            return E_POINTER;
        *v = nullptr;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        *v = new (std::nothrow) AddonLicense(snapshot, index);
        return *v ? S_OK : E_OUTOFMEMORY;
    }
};
class LicenseIterator final : public ILicenseIterator
{
    std::atomic<ULONG> refs{1};
    LicenseSnapshot snapshot;
    std::atomic<UINT32> position;
    UINT32 end;

  public:
    LicenseIterator(LicenseSnapshot v, UINT32 b, UINT32 e) : snapshot(std::move(v)), position(b), end(e)
    {
    }
    LICENSE_OBJECT_METHODS(ILicenseIterator, IID_LicenseIterator, "Windows.Foundation.Collections.IIterator`1")
    HRESULT STDMETHODCALLTYPE Current(ILicensePair **v) override
    {
        if (!v)
            return E_POINTER;
        *v = nullptr;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        UINT32 i = position;
        if (i >= end)
            return LICENSE_E_BOUNDS;
        *v = new (std::nothrow) LicensePair(snapshot, i);
        return *v ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE HasCurrent(boolean *v) override
    {
        if (!v)
            return E_POINTER;
        *v = false;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        *v = position < end;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean *v) override
    {
        if (!v)
            return E_POINTER;
        *v = false;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        UINT32 i = position;
        while (i < end && !position.compare_exchange_weak(i, i + 1))
        {
        }
        *v = position < end;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, ILicensePair **values, UINT32 *actual) override
    {
        if (!actual || (capacity && !values))
            return E_POINTER;
        *actual = 0;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        UINT32 first = position, n;
        do
        {
            n = std::min(capacity, end - first);
        } while (!position.compare_exchange_weak(first, first + n));
        for (UINT32 i = 0; i < n; ++i)
        {
            values[i] = new (std::nothrow) LicensePair(snapshot, first + i);
            if (!values[i])
            {
                for (UINT32 j = 0; j < i; ++j)
                {
                    values[j]->Release();
                    values[j] = nullptr;
                }
                return E_OUTOFMEMORY;
            }
        }
        *actual = n;
        return S_OK;
    }
};
class LicenseMap final : public ILicenseMap, public ILicenseIterable
{
    std::atomic<ULONG> refs{1};
    LicenseSnapshot snapshot;
    UINT32 begin, end;
    bool matches(HSTRING key, UINT32 i) const
    {
        UINT32 n;
        const WCHAR *s = WindowsGetStringRawBuffer(key, &n);
        const auto &sku = snapshot->licenses[i].sku;
        if (n != sku.size())
            return false;
        for (UINT32 j = 0; j < n; ++j)
            if (s[j] != (unsigned char)sku[j])
                return false;
        return true;
    }

  public:
    LicenseMap(LicenseSnapshot v, UINT32 b, UINT32 e) : snapshot(std::move(v)), begin(b), end(e)
    {
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (IsEqualGUID(id, IID_LicenseIterable))
            *out = static_cast<ILicenseIterable *>(this);
        else if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IInspectable) ||
                 IsEqualGUID(id, IID_IAgileObject_) || IsEqualGUID(id, IID_LicenseMap))
            *out = static_cast<ILicenseMap *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }
    INSPECTABLE_STUBS
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *n) override
    {
        return make_hstring("Windows.Foundation.Collections.IMapView`2", n);
    }
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, IAddonLicense **v) override
    {
        if (!v)
            return E_POINTER;
        *v = nullptr;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        for (UINT32 i = begin; i < end; ++i)
            if (matches(key, i))
            {
                *v = new (std::nothrow) AddonLicense(snapshot, i);
                return *v ? S_OK : E_OUTOFMEMORY;
            }
        return LICENSE_E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE Size(UINT32 *v) override
    {
        if (!v)
            return E_POINTER;
        *v = 0;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        *v = end - begin;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, boolean *v) override
    {
        if (!v)
            return E_POINTER;
        *v = false;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        for (UINT32 i = begin; i < end; ++i)
            if (matches(key, i))
            {
                *v = true;
                break;
            }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Split(ILicenseMap **a, ILicenseMap **b) override
    {
        if (a && a == b)
            return E_INVALIDARG;
        if (!a || !b)
            return E_POINTER;
        *a = *b = nullptr;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        if (end - begin < 2)
            return S_OK;
        UINT32 mid = begin + (end - begin) / 2;
        *a = new (std::nothrow) LicenseMap(snapshot, begin, mid);
        *b = new (std::nothrow) LicenseMap(snapshot, mid, end);
        if (!*a || !*b)
        {
            if (*a)
                (*a)->Release();
            if (*b)
                (*b)->Release();
            *a = *b = nullptr;
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE First(ILicenseIterator **v) override
    {
        if (!v)
            return E_POINTER;
        *v = nullptr;
        if (!snapshot->valid())
            return E_ACCESSDENIED;
        *v = new (std::nothrow) LicenseIterator(snapshot, begin, end);
        return *v ? S_OK : E_OUTOFMEMORY;
    }
};
#undef LICENSE_OBJECT_METHODS
static HRESULT make_addon_license_map(AddonLicenseSnapshot value, IInspectable **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!value.valid())
        return E_ACCESSDENIED;
    if (value.licenses.size() > 4096)
        return E_UNEXPECTED;
    for (size_t i = 0; i < value.licenses.size(); ++i)
    {
        const auto &v = value.licenses[i];
        if (v.sku.size() != 17 || v.sku[12] != '/' || v.offer.size() > 4096 ||
            v.expiration <= std::time(nullptr) || v.expiration > 910692730085LL)
            return E_UNEXPECTED;
        for (size_t j = 0; j < v.sku.size(); ++j)
            if (j != 12 && !(v.sku[j] >= '0' && v.sku[j] <= '9') && !(v.sku[j] >= 'A' && v.sku[j] <= 'Z'))
                return E_UNEXPECTED;
        for (size_t j = 0; j < i; ++j)
            if (value.licenses[j].sku == v.sku)
                return E_UNEXPECTED;
        value.proof_until = std::min(value.proof_until, v.expiration);
    }
    try
    {
        auto snapshot = std::make_shared<const AddonLicenseSnapshot>(std::move(value));
        auto *map = new (std::nothrow) LicenseMap(snapshot, 0, snapshot->licenses.size());
        if (!map)
            return E_OUTOFMEMORY;
        *out = static_cast<ILicenseMap *>(map);
        return S_OK;
    }
    catch (const std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
}
