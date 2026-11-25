// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../src/private.h"
#include <atomic>
/* The final back-off slice is followed by a real scheduler wake-up that a
 * unit test cannot reproduce deterministically, so the signed retry test
 * routes only this translation unit's Sleep calls through a test seam and
 * sets the real user cancellation flag exactly after the tenth 50 ms slice.
 * Every other test path still calls the real Sleep. */
static XUser *title_test_sleep_target = nullptr;
static unsigned title_test_sleep_slices = 0;
static void WINAPI title_test_sleep( DWORD milliseconds );
#define Sleep title_test_sleep
#include "../../src/System/XUser.cpp"
#undef Sleep
static void WINAPI title_test_sleep( DWORD milliseconds )
{
    if ( title_test_sleep_target && milliseconds == 50 && ++title_test_sleep_slices == 10 )
        title_test_sleep_target->cancelled = true;
    else
        Sleep( milliseconds );
}
static unsigned failures, checks;
#define CHECK( v )                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if ( !( v ) )                                                                                                  \
        {                                                                                                              \
            ++failures;                                                                                                \
            printf( "FAIL line %u\n", __LINE__ );                                                                      \
        }                                                                                                              \
    } while ( 0 )
class TitleTransport : public UserTransport
{
public:
    DWORD status = 200;
    std::string claim = "305419896";
    bool includeToken = true;
    bool cancelOnRequest = false;
    bool retryOnce = false;
    XUserHandle owner = nullptr;
    unsigned requests = 0;
    HRESULT Backend( UINT16, const char *, UINT16, char **, UINT16 * ) override
    {
        return E_ACCESSDENIED;
    }
    HRESULT Http( const WCHAR *, const WCHAR *, char *, const WCHAR *, const WCHAR **, BYTE **, SIZE_T * ) override
    {
        return E_ACCESSDENIED;
    }
    HRESULT HttpRaw( const WCHAR *method, const WCHAR *url, const WCHAR *headers, const void *body, DWORD size,
                     DWORD *outStatus, BYTE **out, SIZE_T *length ) override
    {
        ++requests;
        if ( cancelOnRequest && owner ) owner->cancelled = true;
        CHECK( !wcscmp( method, L"POST" ) );
        CHECK( !wcscmp( url, L"https://title.auth.xboxlive.com/title/authenticate" ) );
        CHECK( wcsstr( headers, L"Signature: " ) && wcsstr( headers, L"x-xbl-contract-version: 1" ) );
        std::string sent( static_cast<const char *>( body ), size );
        CHECK( sent.find( "\"TitleId\":305419896" ) != std::string::npos );
        CHECK( sent.find( "\"DeviceToken\":\"synthetic-device\"" ) != std::string::npos );
        CHECK( sent.find( "\"ProofKey\":{" ) != std::string::npos );
        CHECK( sent.find( "synthetic-other-title" ) == std::string::npos );
        std::string response = "{\"DisplayClaims\":{\"xti\":{\"tid\":" + claim + "}}";
        if ( includeToken )
            response += ",\"Token\":\"synthetic-windows-title\"";
        response += "}";
        *outStatus = ( retryOnce && requests == 1 ) ? 500 : status;
        *length = response.size();
        *out = static_cast<BYTE *>( malloc( response.size() + 1 ) );
        memcpy( *out, response.c_str(), response.size() + 1 );
        return S_OK;
    }
};

class LegacyInitTransport final : public TitleTransport
{
public:
    std::string xid = "123456789";
    bool includeXid = true, cancelOnXsts = false, sisuTitleBound = false;
    bool refuseTitleDiscovery = false, cancelTitleDiscovery = false;
    std::string titleDiscoveryPayload = R"({"SignaturePolicies":[],"EndPoints":[]})";
    std::string sisuDiscoveryPayload = R"({"SignaturePolicies":[],"EndPoints":[]})";
    bool sawTitleAuthorization = false, sawTitleDiscovery = false, sawSisuDiscovery = false;
    unsigned titleXstsRequests = 0, discoveryRequests = 0;
    unsigned titleDiscoveryRequests = 0, sisuDiscoveryRequests = 0;
    static HRESULT Reply( const char *text, BYTE **out, SIZE_T *length )
    {
        *length = strlen( text );
        *out = static_cast<BYTE *>( malloc( *length + 1 ) );
        if ( !*out )
            return E_OUTOFMEMORY;
        memcpy( *out, text, *length + 1 );
        return S_OK;
    }
    HRESULT Backend( UINT16 type, const char *, UINT16, char **out, UINT16 *length ) override
    {
        CHECK( type == 3 );
        const char xml[] =
            "<MSATokenResponse><Token>synthetic-ticket</Token><Expiry>4102444800</Expiry>"
            "<DeviceRps>synthetic-device-ticket</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>";
        *out = strdup( xml );
        *length = sizeof( xml ) - 1;
        return S_OK;
    }
    HRESULT Http( const WCHAR *method, const WCHAR *url, char *, const WCHAR *headers, const WCHAR **, BYTE **out,
                  SIZE_T *length ) override
    {
        if ( !wcscmp( url, L"https://device.auth.xboxlive.com/device/authenticate" ) )
            return Reply( R"({"Token":"synthetic-device"})", out, length );
        if ( !wcscmp( url, L"https://sisu.xboxlive.com/authorize" ) )
        {
            std::string titleToken = sisuTitleBound ? "synthetic-sisu-title" : "synthetic-other-title";
            std::string titleClaim = sisuTitleBound ? "305419896" : "19";
            return Reply( ( R"({"UserToken":{"Token":"synthetic-user"},"TitleToken":{"Token":")" + titleToken +
                            R"(","DisplayClaims":{"xti":{"tid":)" + titleClaim +
                            R"(}}},"AuthorizationToken":{"Token":"synthetic-platform-auth","DisplayClaims":{"xui":[{"xid":"123456789","gtg":"Synthetic","mgt":"Synthetic","mgs":"","umg":"Synthetic","agg":"Adult"}]}}})" )
                              .c_str(),
                          out, length );
        }
        if ( !wcscmp( url, L"https://title.mgt.xboxlive.com/titles/current/endpoints" ) )
        {
            CHECK( !wcscmp( method, L"GET" ) );
            CHECK( headers && wcsstr( headers, L"Authorization: " ) );
            ++discoveryRequests;
            sawTitleDiscovery = headers && wcsstr( headers, L"synthetic-title-xsts" ) != nullptr;
            sawSisuDiscovery = headers && wcsstr( headers, L"synthetic-platform-auth" ) != nullptr;
            if ( sawTitleDiscovery ) ++titleDiscoveryRequests;
            else ++sisuDiscoveryRequests;
            if ( sawTitleDiscovery )
            {
                CHECK( wcsstr( headers, L"Signature: " ) );
                if ( cancelTitleDiscovery && owner )
                {
                    owner->cancelled = true;
                    return E_ABORT;
                }
                if ( refuseTitleDiscovery ) return E_ACCESSDENIED;
                return Reply( titleDiscoveryPayload.c_str(), out, length );
            }
            return Reply( sisuDiscoveryPayload.c_str(), out, length );
        }
        else
            CHECK( !wcscmp( url, L"https://title.mgt.xboxlive.com/titles/default/endpoints?type=1" ) );
        return Reply( R"({"SignaturePolicies":[],"EndPoints":[]})", out, length );
    }
    HRESULT HttpRaw( const WCHAR *method, const WCHAR *url, const WCHAR *headers, const void *body, DWORD size,
                     DWORD *outStatus, BYTE **out, SIZE_T *length ) override
    {
        if ( !wcscmp( url, L"https://title.auth.xboxlive.com/title/authenticate" ) )
            return TitleTransport::HttpRaw( method, url, headers, body, size, outStatus, out, length );
        CHECK( !wcscmp( url, L"https://xsts.auth.xboxlive.com/xsts/authorize" ) );
        ++titleXstsRequests;
        if ( cancelOnXsts && owner ) owner->cancelled = true;
        std::string request( static_cast<const char *>( body ), size );
        CHECK( request.find( "\"TitleToken\":\"synthetic-windows-title\"" ) != std::string::npos );
        CHECK( request.find( "synthetic-other-title" ) == std::string::npos );
        sawTitleAuthorization = true;
        *outStatus = 200;
        std::string response = R"({"Token":"synthetic-title-xsts","DisplayClaims":{"xui":[{"uhs":"synthetic-hash")";
        if ( includeXid ) response += ",\"xid\":\"" + xid + "\"";
        response += "}]}}";
        *length = response.size();
        *out = static_cast<BYTE *>( malloc( response.size() + 1 ) );
        memcpy( *out, response.c_str(), response.size() + 1 );
        return S_OK;
    }
};

int main()
{
    InitializeLogging();
    CoInitializeEx( nullptr, COINIT_MULTITHREADED );
    const char xml[] = "<Game configVersion='0'><Identity Name='Synthetic.Legacy' "
                       "Publisher='CN=Test'/><TitleId>12345678</TitleId></Game>";
    INITIALIZE_OPTIONS options{ 1, TRUE, xml };
    CHECK( nexus::InitializeGameConfiguration( &options ) == S_OK );
    for ( unsigned mode = 0; mode < 5; ++mode )
    {
        TitleTransport transport;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        user.deviceToken = strdup( "synthetic-device" );
        if ( mode == 1 )
            transport.claim = "305419897";
        if ( mode == 2 )
            transport.claim = "\"0305419896\"";
        if ( mode == 3 )
            transport.status = 403;
        if ( mode == 4 )
            transport.includeToken = false;
        HRESULT result = windows_title_auth( &user );
        CHECK( transport.requests == 1 );
        if ( mode == 0 )
        {
            CHECK( result == S_OK );
            CHECK( user.titleToken && !strcmp( user.titleToken, "synthetic-windows-title" ) );
            CHECK( user.titleTokenTitleId == 0x12345678 );
        }
        else
        {
            CHECK( FAILED( result ) );
            CHECK( !user.titleToken );
            CHECK( !user.titleTokenTitleId );
        }
        user.ResetNetworkState();
    }
    {
        LegacyInitTransport transport;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 1 && transport.sawTitleAuthorization );
        CHECK( transport.discoveryRequests == 1 && transport.sawTitleDiscovery && !transport.sawSisuDiscovery );
        CHECK( user.titleTokenTitleId == 0x12345678 );
        user.ResetNetworkState();
    }
    {
        TitleTransport transport;
        XUser user( transport );
        user.cancelled = true;
        CHECK( windows_title_auth( &user ) == E_ABORT );
        CHECK( transport.requests == 0 );
    }
    /* In-flight cancellation must stop the signed retry loop: a 500 response
     * that races with cancellation must not be retried. */
    {
        TitleTransport transport;
        transport.status = 500;
        transport.cancelOnRequest = true;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        user.deviceToken = strdup( "synthetic-device" );
        transport.owner = &user;
        CHECK( windows_title_auth( &user ) == E_ABORT );
        CHECK( transport.requests == 1 );
        CHECK( !user.titleToken && !user.titleTokenTitleId );
        user.ResetNetworkState();
    }
    /* Cancellation during the title-bearing XSTS exchange stops the whole
     * legacy initialization before discovery. */
    {
        LegacyInitTransport transport;
        transport.cancelOnXsts = true;
        XUser user( transport );
        transport.owner = &user;
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == E_ABORT );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 1 );
        CHECK( transport.discoveryRequests == 0 );
        user.ResetNetworkState();
    }
    /* Cancellation during the title request also ends the full legacy
     * initialization before any follow-up title exchange. */
    {
        LegacyInitTransport transport;
        transport.status = 500;
        transport.cancelOnRequest = true;
        XUser user( transport );
        transport.owner = &user;
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == E_ABORT );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 0 );
        CHECK( transport.discoveryRequests == 0 );
        user.ResetNetworkState();
    }
    /* A title-bearing XSTS assertion for a foreign XUID is never accepted; the
     * authenticated local user continues with platform discovery and no title
     * proof is retained. */
    {
        LegacyInitTransport transport;
        transport.xid = "999999999";
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 1 );
        CHECK( transport.discoveryRequests == 1 && transport.sawSisuDiscovery && !transport.sawTitleDiscovery );
        CHECK( !user.titleToken && !user.titleTokenTitleId );
        CHECK( user.xuid == 123456789 );
        user.ResetNetworkState();
    }
    /* A missing or malformed XUID claim in the title-bearing XSTS assertion is
     * rejected the same way and cannot grant title discovery. */
    for ( bool includeXid : { false, true } )
    {
        LegacyInitTransport transport;
        transport.includeXid = includeXid;
        transport.xid = "123456789x";
        if ( !includeXid ) transport.xid.clear();
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.discoveryRequests == 1 && transport.sawSisuDiscovery && !transport.sawTitleDiscovery );
        CHECK( !user.titleToken && !user.titleTokenTitleId );
        CHECK( user.xuid == 123456789 );
        user.ResetNetworkState();
    }
    /* A refused optional title authentication leaves the successful local user
     * sign-in and platform discovery intact, without any title authorization. */
    {
        LegacyInitTransport transport;
        transport.status = 403;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 0 );
        CHECK( transport.discoveryRequests == 1 && transport.sawSisuDiscovery && !transport.sawTitleDiscovery );
        CHECK( !user.titleToken && !user.titleTokenTitleId );
        CHECK( user.xuid == 123456789 );
        user.ResetNetworkState();
    }
    /* A refused optional Windows title authentication must not discard a title
     * token that the platform SISU exchange already bound to this title. */
    {
        LegacyInitTransport transport;
        transport.sisuTitleBound = true;
        transport.status = 403;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 0 );
        CHECK( transport.discoveryRequests == 1 && transport.sawSisuDiscovery && !transport.sawTitleDiscovery );
        CHECK( user.titleToken && !strcmp( user.titleToken, "synthetic-sisu-title" ) );
        CHECK( user.titleTokenTitleId == 0x12345678 );
        CHECK( user.xuid == 123456789 );
        user.ResetNetworkState();
    }
    /* Cancellation observed in the final retry back-off slice must be honored
     * before the next raw title request: no extra request may start. */
    {
        TitleTransport transport;
        transport.retryOnce = true;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        user.deviceToken = strdup( "synthetic-device" );
        title_test_sleep_target = &user;
        title_test_sleep_slices = 0;
        CHECK( windows_title_auth( &user ) == E_ABORT );
        CHECK( title_test_sleep_slices == 10 );
        CHECK( transport.requests == 1 );
        CHECK( !user.titleToken && !user.titleTokenTitleId );
        title_test_sleep_target = nullptr;
        user.ResetNetworkState();
    }
    /* A refused optional signed current-title discovery falls back to the
     * validated SISU platform authorization; a title token the platform bound
     * to this title stays live. */
    {
        LegacyInitTransport transport;
        transport.sisuTitleBound = true;
        transport.refuseTitleDiscovery = true;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 1 );
        CHECK( transport.discoveryRequests == 2 && transport.titleDiscoveryRequests == 1 );
        CHECK( transport.sisuDiscoveryRequests == 1 && transport.sawSisuDiscovery );
        CHECK( user.titleToken && !strcmp( user.titleToken, "synthetic-sisu-title" ) );
        CHECK( user.titleTokenTitleId == 0x12345678 );
        CHECK( user.xuid == 123456789 );
        user.ResetNetworkState();
    }
    /* Without a SISU-bound title proof the rejected optional path leaves no
     * title proof at all, only the platform discovery. */
    {
        LegacyInitTransport transport;
        transport.refuseTitleDiscovery = true;
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.discoveryRequests == 2 && transport.titleDiscoveryRequests == 1 );
        CHECK( transport.sisuDiscoveryRequests == 1 && transport.sawSisuDiscovery );
        CHECK( !user.titleToken && !user.titleTokenTitleId );
        CHECK( user.xuid == 123456789 );
        user.ResetNetworkState();
    }
    /* A partially valid title discovery payload must not publish learned
     * endpoint metadata before the platform fallback runs. */
    {
        LegacyInitTransport transport;
        transport.sisuTitleBound = true;
        transport.titleDiscoveryPayload = R"({"SignaturePolicies":[{"Version":1,"MaxBodyBytes":1024}],)"
                                          R"("EndPoints":[{"Protocol":"https","Host":"partial-host"},)"
                                          R"({"Protocol":"https"}]})";
        transport.sisuDiscoveryPayload =
            R"({"SignaturePolicies":[],"EndPoints":[{"Protocol":"https","Host":"sisu-host"}]})";
        XUser user( transport );
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( transport.discoveryRequests == 2 && transport.titleDiscoveryRequests == 1 );
        CHECK( transport.sisuDiscoveryRequests == 1 && transport.sawSisuDiscovery );
        CHECK( user.endpointsLen == 1 && user.policiesLen == 0 );
        CHECK( user.endpoints[0].host &&
               !wcscmp( WindowsGetStringRawBuffer( user.endpoints[0].host, nullptr ), L"sisu-host" ) );
        CHECK( user.titleToken && !strcmp( user.titleToken, "synthetic-sisu-title" ) );
        CHECK( user.titleTokenTitleId == 0x12345678 );
        user.ResetNetworkState();
    }
    /* Cancellation during the optional signed discovery aborts instead of
     * falling back to the platform path. */
    {
        LegacyInitTransport transport;
        transport.cancelTitleDiscovery = true;
        XUser user( transport );
        transport.owner = &user;
        CHECK( user.Initialize( XUserAddOptions::AddDefaultUserSilently ) == S_OK );
        CHECK( user.InitializeNetwork( XUserAddOptions::AddDefaultUserSilently ) == E_ABORT );
        CHECK( transport.requests == 1 && transport.titleXstsRequests == 1 );
        CHECK( transport.discoveryRequests == 1 && transport.sisuDiscoveryRequests == 0 );
        user.ResetNetworkState();
    }
    /* Explicit and modern clients never enter the legacy Windows title flow.
     * Existing authentication suites exercise such a configuration end to end;
     * the eligibility gate is verified directly here. */
    {
        nexus::GameConfiguration legacy{}, explicitClient{}, modern{}, untitled{};
        legacy.game.attributes = { { "configVersion", "0" } };
        legacy.titleId = 0x12345678;
        explicitClient = legacy;
        explicitClient.msaAppId = "000000001234ABCD";
        modern = legacy;
        modern.game.attributes = { { "configVersion", "1" } };
        untitled = legacy;
        untitled.titleId = 0;
        CHECK( legacy_title_eligible( &legacy ) );
        CHECK( !legacy_title_eligible( &explicitClient ) );
        CHECK( !legacy_title_eligible( &modern ) );
        CHECK( !legacy_title_eligible( &untitled ) );
        CHECK( !legacy_title_eligible( nullptr ) );
    }
    CoUninitialize();
    printf( "CHECKS %u FAILURES %u\n", checks, failures );
    return failures ? 1 : 0;
}
