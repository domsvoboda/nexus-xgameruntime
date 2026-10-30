// SPDX-License-Identifier: LGPL-2.1-or-later
// Production receipt transport, separate from the account-free WinRT ABI tests.
#include "StoreLicenseProjection.h"
namespace nexus_store_context
{
static HRESULT query_game_license( XStoreGameLicense *out, const std::atomic<bool> &canceled )
{
    XStoreContextHandle store = nullptr;
    XTaskQueueHandle queue = nullptr;
    IXThreadingImpl *threading = nullptr;
    XAsyncBlock block = {};
    *out = {};
    for ( int i = 0; i < 300 && !canceled && !nexus::GetGameConfiguration(); ++i ) Sleep( 100 );
    if ( canceled ) return E_ABORT;
    if ( !GetStoreProductId() ) return E_GAME_MISSING_GAME_CONFIG;
    HRESULT hr = entitled_store_create( nullptr, &store );
    if ( FAILED( hr ) ) return hr;
    if ( SUCCEEDED( hr = QueryApiImpl( CLSID_XThreadingImpl, IID_IXThreadingImpl, (void **)&threading ) ) &&
         SUCCEEDED( hr = threading->XTaskQueueCreate( XTaskQueueDispatchMode::ThreadPool,
                                                      XTaskQueueDispatchMode::ThreadPool, &queue ) ) )
    {
        block.queue = queue;
        hr = entitled_begin( store, static_cast<UINT32>( XStoreProductKind::Game ), ENTITLED_MAX_PRODUCTS, &block, TRUE, TRUE );
        if ( SUCCEEDED( hr ) ) hr = threading->XAsyncGetStatus( &block, TRUE );
        if ( SUCCEEDED( hr ) ) hr = threading->XAsyncGetResult( &block, &entitled_game_identity, sizeof( *out ), out, nullptr );
        threading->XTaskQueueCloseHandle( queue );
    }
    if ( threading ) threading->Release();
    entitled_store_close( store );
    if ( SUCCEEDED( hr ) && !out->isActive ) hr = E_ACCESSDENIED;
    return hr;
}
/* Use the same account-backed, signed-receipt enumeration as XStore. The
 * result includes both packaged and package-free durables, excludes consumables,
 * and never substitutes an empty success for an unavailable/denied query. */
static HRESULT query_addon_licenses(AddonLicenseSnapshot *out)
{
    *out = {};
    if (!nexus::GetGameConfiguration() || !GetStoreProductId()) return E_GAME_MISSING_GAME_CONFIG;
    XStoreContextHandle store = nullptr;
    XTaskQueueHandle queue = nullptr;
    XStoreProductQueryHandle handle = nullptr;
    IXThreadingImpl *threading = nullptr;
    XAsyncBlock block = {};
    HRESULT hr = entitled_store_create(nullptr, &store);
    if (FAILED(hr)) return hr;
    if (SUCCEEDED(hr = QueryApiImpl(CLSID_XThreadingImpl, IID_IXThreadingImpl, (void **)&threading)) &&
        SUCCEEDED(hr = threading->XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool,
                                                   XTaskQueueDispatchMode::ThreadPool, &queue)))
    {
        block.queue = queue;
        hr = entitled_begin(store, static_cast<UINT32>(XStoreProductKind::Durable), ENTITLED_MAX_PRODUCTS, &block);
        if (SUCCEEDED(hr)) hr = threading->XAsyncGetStatus(&block, TRUE);
        if (SUCCEEDED(hr)) hr = threading->XAsyncGetResult(&block, &entitled_identity, sizeof(handle), &handle, nullptr);
        if (SUCCEEDED(hr))
        {
            auto *page = entitled_page_acquire(handle);
            if (!page || !entitled_page_valid(page)) hr = E_ACCESSDENIED;
            if (SUCCEEDED(hr))
            {
                hr = addon_data_from_verified_snapshot(page->snapshot, out);
            }
            if (page) entitled_page_release(page);
        }
        if (handle) entitled_page_close(handle);
        threading->XTaskQueueCloseHandle(queue);
    }
    if (threading) threading->Release();
    entitled_store_close(store);
    if (FAILED(hr)) *out = {};
    xodus_store_license_note("store-winrt-addon-license-result", hr);
    return hr;
}

}
