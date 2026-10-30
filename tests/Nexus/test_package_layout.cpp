// SPDX-License-Identifier: MIT
// Independent raw COM-slot checks against a test-only C++ implementation of
// the generated interface. This mock is never exposed by the runtime DLL.
#include "../../src/private.h"
#include <gtest/gtest.h>
#include <xpackage.h>

namespace
{
class PackageLayout final : public IXPackageImpl4
{
public:
    HRESULT WINAPI QueryInterface( REFIID, void **out ) noexcept override
    {
        if ( !out ) return E_POINTER;
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef() noexcept override
    {
        return 1;
    }
    ULONG WINAPI Release() noexcept override
    {
        return 1;
    }
    HRESULT WINAPI XPackageGetCurrentProcessPackageIdentifier( SIZE_T bufferSize, char *buffer ) override
    {
        return E_NOTIMPL;
    }
    BOOLEAN WINAPI XPackageIsPackagedProcess() override
    {
        return FALSE;
    }
    HRESULT WINAPI XPackageCreateInstallationMonitor( const char *packageIdentifier, UINT32 selectorCount,
                                                      XPackageChunkSelector *selectors, UINT32 minimumUpdateIntervalMs,
                                                      XTaskQueueHandle queue,
                                                      XPackageInstallationMonitorHandle *installationMonitor ) override
    {
        return E_NOTIMPL;
    }
    void WINAPI XPackageCloseInstallationMonitorHandle( XPackageInstallationMonitorHandle installationMonitor ) override
    {
    }
    void WINAPI XPackageGetInstallationProgress( XPackageInstallationMonitorHandle installationMonitor,
                                                 XPackageInstallationProgress *progress ) override
    {
    }
    BOOLEAN WINAPI XPackageUpdateInstallationMonitor( XPackageInstallationMonitorHandle installationMonitor ) override
    {
        return FALSE;
    }
    HRESULT WINAPI XPackageRegisterInstallationProgressChanged( XPackageInstallationMonitorHandle installationMonitor,
                                                                void *context,
                                                                XPackageInstallationProgressCallback *callback,
                                                                XTaskQueueRegistrationToken *token ) override
    {
        return E_NOTIMPL;
    }
    BOOLEAN WINAPI XPackageUnregisterInstallationProgressChanged( XPackageInstallationMonitorHandle installationMonitor,
                                                                  XTaskQueueRegistrationToken token,
                                                                  BOOLEAN wait ) override
    {
        return FALSE;
    }
    HRESULT WINAPI XPackageGetUserLocale( SIZE_T localeSize, char *locale ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageFindChunkAvailability( const char *packageIdentifier, UINT32 selectorCount,
                                                  XPackageChunkSelector *selectors,
                                                  XPackageChunkAvailability *availability ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageEnumerateChunkAvailability( const char *packageIdentifier, XPackageChunkSelectorType type,
                                                       void *context,
                                                       XPackageChunkAvailabilityCallback *callback ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageChangeChunkInstallOrder( const char *packageIdentifier, UINT32 selectorCount,
                                                    XPackageChunkSelector *selectors ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageInstallChunks( const char *packageIdentifier, UINT32 selectorCount,
                                          XPackageChunkSelector *selectors, UINT32 minimumUpdateIntervalMs,
                                          BOOLEAN suppressUserConfirmation, XTaskQueueHandle queue,
                                          XPackageInstallationMonitorHandle *installationMonitor ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageInstallChunksAsync( const char *packageIdentifier, UINT32 selectorCount,
                                               XPackageChunkSelector *selectors, UINT32 minimumUpdateIntervalMs,
                                               BOOLEAN suppressUserConfirmation, XAsyncBlock *asyncBlock ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageInstallChunksResult( XAsyncBlock *asyncBlock,
                                                XPackageInstallationMonitorHandle *installationMonitor ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageEstimateDownloadSize( const char *packageIdentifier, UINT32 selectorCount,
                                                 XPackageChunkSelector *selectors, UINT64 *downloadSize,
                                                 BOOLEAN *shouldPresentUserConfirmation ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageUninstallChunks( const char *packageIdentifier, UINT32 selectorCount,
                                            XPackageChunkSelector *selectors ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageEnumeratePackagesLegacy( XPackageKind kind, XPackageEnumerationScope scope, void *context,
                                                    XPackageEnumerationCallback *callback ) override
    {
        return 20;
    }
    HRESULT WINAPI XPackageRegisterPackageInstalledLegacy( XTaskQueueHandle queue, void *context,
                                                           XPackageInstalledCallback *callback,
                                                           XTaskQueueRegistrationToken *token ) override
    {
        return E_NOTIMPL;
    }
    BOOLEAN WINAPI XPackageUnregisterPackageInstalled( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        return FALSE;
    }
    HRESULT WINAPI XPackageMountLegacy( const char *identifier, XPackageMountHandle *mount ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageGetMountPathSize( XPackageMountHandle mount, SIZE_T *pathSize ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageGetMountPath( XPackageMountHandle mount, SIZE_T pathSize, char *path ) override
    {
        return E_NOTIMPL;
    }
    void WINAPI XPackageCloseMountHandle( XPackageMountHandle mount ) override {}
    HRESULT WINAPI XPackageGetIdentifierLegacy( const char *store, SIZE_T size, char *identifier ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageEnumeratePackages( XPackageKind kind, XPackageEnumerationScope scope, void *context,
                                              XPackageEnumerationCallback *callback ) override
    {
        return 28;
    }
    HRESULT WINAPI XPackageRegisterPackageInstalled( XTaskQueueHandle queue, void *context,
                                                     XPackageInstalledCallback *callback,
                                                     XTaskQueueRegistrationToken *token ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageGetWriteStats( XPackageWriteStats *writeStats ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageEnumerateFeaturesLegacy( const char *identifier, void *context,
                                                    XPackageFeatureEnumerationCallback *callback ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageUninstallUWPInstance( const char *packageName ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageEnumerateFeatures( const char *packageIdentifier, void *context,
                                              XPackageFeatureEnumerationCallback *callback ) override
    {
        return E_NOTIMPL;
    }
    BOOLEAN WINAPI XPackageUninstallPackage( const char *packageIdentifier ) override
    {
        return FALSE;
    }
    HRESULT WINAPI XPackageEnumeratePackages2( XPackageKind kind, XPackageEnumerationScope scope, void *context,
                                               XPackageEnumerationCallback *callback ) override
    {
        return 35;
    }
    HRESULT WINAPI XPackageRegisterPackageInstalled2( XTaskQueueHandle queue, void *context,
                                                      XPackageInstalledCallback *callback,
                                                      XTaskQueueRegistrationToken *token ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageMountWithUiAsync( const char *packageIdentifier, XAsyncBlock *async ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageMountWithUiResult( XAsyncBlock *async, XPackageMountHandle *mount ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageEnumeratePackages3( XPackageKind kind, XPackageEnumerationScope scope, void *context,
                                               XPackageEnumerationCallback *callback ) override
    {
        return 39;
    }
    HRESULT WINAPI XPackageRegisterPackageInstalled3( XTaskQueueHandle queue, void *context,
                                                      XPackageInstalledCallback *callback,
                                                      XTaskQueueRegistrationToken *token ) override
    {
        return E_NOTIMPL;
    }
    HRESULT WINAPI XPackageGetKind( const char *packageIdentifier, XPackageKind *kind ) override
    {
        return E_NOTIMPL;
    }
};
static_assert( sizeof( XPackageDetails ) == ( sizeof( void * ) == 8 ? 80 : 56 ) );
}
TEST( NexusPackageAbi, EveryVersionKeepsItsIndependentNativeEnumerationSlot )
{
    PackageLayout package;
    auto table = *reinterpret_cast<void ***>( &package );
    using Enumerate =
        HRESULT( WINAPI * )( void *, XPackageKind, XPackageEnumerationScope, void *, XPackageEnumerationCallback * );
    for ( unsigned slot : { 20u, 28u, 35u, 39u } )
    {
        auto call = reinterpret_cast<Enumerate>( table[slot] );
        EXPECT_EQ( call( &package, static_cast<XPackageKind>( 0 ), static_cast<XPackageEnumerationScope>( 0 ), nullptr,
                         nullptr ),
                   static_cast<HRESULT>( slot ) );
    }
}

static void WINAPI PackageInstalledCallback( void *, const XPackageDetails * ) {}
TEST( NexusPackageAbi, UninitializedInstallEventsRejectRegistrationWithoutPublishingAToken )
{
    auto dll = LoadLibraryW( L"xgameruntime.dll" );
    ASSERT_NE( dll, nullptr );
    using Query = HRESULT( WINAPI * )( REFCLSID, REFIID, void ** );
    auto query = reinterpret_cast<Query>( GetProcAddress( dll, "QueryApiImpl" ) );
    ASSERT_NE( query, nullptr );
    IXPackageImpl4 *package = nullptr;
    ASSERT_EQ( query( CLSID_XPackageImpl, __uuidof( IXPackageImpl4 ), reinterpret_cast<void **>( &package ) ), S_OK );
    XTaskQueueRegistrationToken token{ 123 };
    // Installed-content events are supported. Without initialized game
    // configuration registration must fail without publishing a subscription.
    EXPECT_EQ( package->XPackageRegisterPackageInstalled3( nullptr, nullptr, PackageInstalledCallback, &token ),
               E_GAME_MISSING_GAME_CONFIG );
    EXPECT_EQ( token.token, 0u );
    EXPECT_EQ( package->XPackageRegisterPackageInstalled3( nullptr, nullptr, nullptr, &token ), E_INVALIDARG );
    EXPECT_EQ( token.token, 0u );
    EXPECT_EQ( package->XPackageRegisterPackageInstalled3( nullptr, nullptr, PackageInstalledCallback, nullptr ),
               E_INVALIDARG );
    // TRUE means no callbacks are pending, including a failed registration.
    EXPECT_TRUE( package->XPackageUnregisterPackageInstalled( token, FALSE ) );
    package->Release();
    FreeLibrary( dll );
}
TEST( NexusPackageAbi, UnknownPackageIdentifiersCannotBeReportedAsAnInstalledGame )
{
    auto dll = LoadLibraryW( L"xgameruntime.dll" );
    ASSERT_NE( dll, nullptr );
    using Query = HRESULT( WINAPI * )( REFCLSID, REFIID, void ** );
    auto query = reinterpret_cast<Query>( GetProcAddress( dll, "QueryApiImpl" ) );
    ASSERT_NE( query, nullptr );
    IXPackageImpl4 *package = nullptr;
    ASSERT_EQ( query( CLSID_XPackageImpl, __uuidof( IXPackageImpl4 ), reinterpret_cast<void **>( &package ) ), S_OK );
    XPackageKind kind = XPackageKind::PublisherContent;
    EXPECT_EQ( package->XPackageGetKind( "99999999999999999999999999999999", &kind ),
               E_GAMEPACKAGE_NO_PACKAGE_IDENTIFIER );
    EXPECT_EQ( kind, XPackageKind::PublisherContent );
    EXPECT_EQ( package->XPackageGetKind( "not-a-package", &kind ), E_INVALIDARG );
    package->Release();
    FreeLibrary( dll );
}
