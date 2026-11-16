/*
 * Copyright 2026 Olivia Ryan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "private.h"
#include "HttpClient.h"
#include <winhttp.h>
#include "HttpCancellation.h"

static const WCHAR USER_AGENT[] = L"curl/1.0";

static bool http_canceled( const std::atomic<bool> *cancel )
{
    return cancel && cancel->load( std::memory_order_acquire );
}
static HRESULT http_request_once( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers,
                                  const WCHAR **accept, UCHAR **buffer, SIZE_T *bufferSize,
                                  const std::atomic<bool> *cancel );

static HRESULT http_request_impl( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers,
                                  const WCHAR **accept, UCHAR **buffer, SIZE_T *bufferSize,
                                  const std::atomic<bool> *cancel )
{
    if ( cancel )
    {
        if ( !buffer || !bufferSize ) return E_INVALIDARG;
        *buffer = NULL; *bufferSize = 0;
    }
    HRESULT hr = E_FAIL;
    for ( int attempt = 0; attempt < 3; attempt++ )
    {
        if ( http_canceled( cancel ) ) return E_ABORT;
        if ( attempt )
        {
            WARN( "HTTP request failed with hr %#lx, retrying.\n", hr );
            if ( !cancel ) Sleep( 500 * attempt );
            else for ( DWORD remaining = 500 * attempt; remaining; )
            {
                if ( http_canceled( cancel ) ) return E_ABORT;
                DWORD slice = remaining > 50 ? 50 : remaining;
                Sleep( slice ); remaining -= slice;
            }
        }
        *buffer = NULL;
        *bufferSize = 0;
        hr = http_request_once( method, url, data, headers, accept, buffer, bufferSize, cancel );
        if ( SUCCEEDED( hr ) || hr == E_FAIL || ( cancel && hr == E_ABORT ) ) break;
    }
    return hr;
}

HRESULT http_request( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers, const WCHAR **accept,
                      UCHAR **buffer, SIZE_T *bufferSize )
{
    return http_request_impl( method, url, data, headers, accept, buffer, bufferSize, nullptr );
}
HRESULT http_request_cancellable( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers,
                                  const WCHAR **accept, UCHAR **buffer, SIZE_T *bufferSize,
                                  const std::atomic<bool> &cancel )
{
    return http_request_impl( method, url, data, headers, accept, buffer, bufferSize, &cancel );
}

static HRESULT http_request_once( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers,
                                  const WCHAR **accept, UCHAR **buffer, SIZE_T *bufferSize,
                                  const std::atomic<bool> *cancel )
{
    URL_COMPONENTS uc = {
        .dwStructSize = sizeof( URL_COMPONENTS ), .dwHostNameLength = MAXDWORD, .dwUrlPathLength = MAXDWORD };
    HINTERNET connection = NULL, request = NULL, session = NULL;
    DWORD size = sizeof( DWORD ), status;
    WCHAR *hostName = NULL;
    UCHAR *tmpBuffer = NULL;
    HRESULT hr = S_OK;
    HttpCancellationMonitor cancellation;

    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( !WinHttpCrackUrl( url, 0, 0, &uc ) ) goto error;
    if ( !( hostName = static_cast<std::remove_reference_t<decltype( hostName )>>(
                calloc( uc.dwHostNameLength + 1, sizeof( WCHAR ) ) ) ) )
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    memcpy( hostName, uc.lpszHostName, uc.dwHostNameLength * sizeof( WCHAR ) );

    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( !( session = WinHttpOpen( USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0 ) ) )
        goto error;
    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( !( connection = WinHttpConnect( session, hostName, INTERNET_DEFAULT_HTTPS_PORT, 0 ) ) ) goto error;
    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( !( request = WinHttpOpenRequest( connection, method, uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER, accept,
                                          WINHTTP_FLAG_SECURE ) ) )
        goto error;
    if ( FAILED( hr = cancellation.start( request, cancel ) ) ) goto cleanup;
    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( !WinHttpSendRequest( request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? -1 : 0, data,
                              ( data ? strlen( data ) : 0 ), ( data ? strlen( data ) : 0 ), 0 ) )
        goto error;
    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( !WinHttpReceiveResponse( request, NULL ) ) goto error;
    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( !WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX ) )
        goto error;
    if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
    if ( status != 200 )
    {
        hr = E_FAIL;
        goto cleanup;
    }

    /* buffer response data */
    *bufferSize = 0;
    *buffer = NULL;
    tmpBuffer = NULL;
    do
    {
        if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
        if ( !WinHttpQueryDataAvailable( request, &size ) ) goto error;
        if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
        if ( !size ) break;
        if ( !( tmpBuffer = static_cast<std::remove_reference_t<decltype( tmpBuffer )>>(
                    realloc( *buffer, *bufferSize + size ) ) ) )
        {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }
        *buffer = tmpBuffer;

        if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
        if ( !WinHttpReadData( request, *buffer + *bufferSize, size, &size ) ) goto error;
        if ( http_canceled( cancel ) ) { hr = E_ABORT; goto cleanup; }
        *bufferSize += size;
    } while ( size );
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    cancellation.join();
    if ( request ) WinHttpCloseHandle( request );
    if ( connection ) WinHttpCloseHandle( connection );
    if ( session ) WinHttpCloseHandle( session );
    if ( hostName ) free( hostName );
    if ( http_canceled( cancel ) ) hr = E_ABORT;
    if ( SUCCEEDED( hr ) ) return hr;
    if ( *buffer ) free( *buffer );
    *bufferSize = 0;
    *buffer = NULL;
    return hr;
}

/* Strict decimal Content-Length. An absent or unparsable header leaves the
 * length unknown; a header that is present must be a valid SIZE_T or the
 * response is rejected instead of being read with a guessed size. */
enum raw_length_state
{
    RAW_LENGTH_UNKNOWN,
    RAW_LENGTH_VALID,
    RAW_LENGTH_INVALID,
};

static raw_length_state raw_content_length( HINTERNET request, SIZE_T *length )
{
    static const WCHAR name[] = L"Content-Length";
    WCHAR buffer[64];
    DWORD size = sizeof( buffer ), index = 0;
    *length = 0;
    if ( !WinHttpQueryHeaders( request, WINHTTP_QUERY_CUSTOM, name, buffer, &size, &index ) )
    {
        DWORD error = GetLastError();
        if ( error == ERROR_WINHTTP_HEADER_NOT_FOUND ) return RAW_LENGTH_UNKNOWN;
        /* A larger required size is a malformed length, never a silent skip. */
        return RAW_LENGTH_INVALID;
    }
    const SIZE_T count = size / sizeof( WCHAR );
    SIZE_T begin = 0, end = count;
    while ( begin < count && ( buffer[begin] == L' ' || buffer[begin] == L'\t' ) ) begin++;
    while ( end > begin && ( buffer[end - 1] == L' ' || buffer[end - 1] == L'\t' ) ) end--;
    if ( begin == end ) return RAW_LENGTH_INVALID;
    SIZE_T value = 0;
    for ( SIZE_T i = begin; i < end; i++ )
    {
        if ( buffer[i] < L'0' || buffer[i] > L'9' ) return RAW_LENGTH_INVALID;
        const SIZE_T digit = ( SIZE_T )( buffer[i] - L'0' );
        if ( value > ( SIZE_MAX - digit ) / 10 ) return RAW_LENGTH_INVALID;
        value = value * 10 + digit;
    }
    *length = value;
    return RAW_LENGTH_VALID;
}

/* Transfer-Encoding overrides Content-Length for message framing. Presence is
 * what matters, so a present header is honoured even if its value does not fit
 * the buffer. Only a genuine absence returns FALSE; any other lookup failure is
 * reported as an error instead of being treated as "no header". */
static BOOL raw_has_transfer_encoding( HINTERNET request, HRESULT *error )
{
    static const WCHAR name[] = L"Transfer-Encoding";
    WCHAR buffer[64];
    DWORD size = sizeof( buffer ), index = 0;
    *error = S_OK;
    if ( WinHttpQueryHeaders( request, WINHTTP_QUERY_CUSTOM, name, buffer, &size, &index ) ) return TRUE;
    DWORD failure = GetLastError();
    if ( failure == ERROR_WINHTTP_HEADER_NOT_FOUND ) return FALSE;
    if ( size > sizeof( buffer ) ) return TRUE;
    *error = HRESULT_FROM_WIN32( failure );
    return FALSE;
}

HRESULT http_request_raw( const WCHAR *method, const WCHAR *url, const WCHAR *headers, const void *body, DWORD bodySize,
                          DWORD *status, BYTE **buffer, SIZE_T *bufferSize )
{
    URL_COMPONENTS uc = {
        .dwStructSize = sizeof( URL_COMPONENTS ), .dwHostNameLength = MAXDWORD, .dwUrlPathLength = MAXDWORD };
    HINTERNET connection = NULL, request = NULL, session = NULL;
    DWORD size = sizeof( DWORD );
    WCHAR *hostName = NULL;
    BYTE *tmpBuffer;
    SIZE_T capacity = 0, declared = 0;
    raw_length_state lengthState;
    HRESULT hr = S_OK;

    *buffer = NULL;
    *bufferSize = 0;
    *status = 0;
    if ( !WinHttpCrackUrl( url, 0, 0, &uc ) ) goto error;
    if ( !( hostName = static_cast<std::remove_reference_t<decltype( hostName )>>(
                calloc( uc.dwHostNameLength + 1, sizeof( WCHAR ) ) ) ) )
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    memcpy( hostName, uc.lpszHostName, uc.dwHostNameLength * sizeof( WCHAR ) );

    if ( !( session = WinHttpOpen( USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0 ) ) )
        goto error;
    if ( !( connection = WinHttpConnect( session, hostName, uc.nPort, 0 ) ) ) goto error;
    if ( !( request = WinHttpOpenRequest( connection, method, uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 ) ) )
        goto error;
    if ( !WinHttpSendRequest( request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? -1 : 0,
                              ( void * )body, bodySize, bodySize, 0 ) )
        goto error;
    if ( !WinHttpReceiveResponse( request, NULL ) ) goto error;
    if ( !WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, status, &size, WINHTTP_NO_HEADER_INDEX ) )
        goto error;
    /* Bodiless responses carry no bytes to compare against metadata length:
     * HEAD (the method is case-sensitive), informational 1xx, 204 and 304, and
     * a successful CONNECT whose body is a tunnel. Transfer-Encoding overrides
     * Content-Length, so a framed response never compares a declared length. */
    if ( ( method && !wcscmp( method, L"HEAD" ) ) || ( *status >= 100 && *status < 200 ) || *status == 204 ||
         *status == 304 || ( *status >= 200 && *status < 300 && method && !wcscmp( method, L"CONNECT" ) ) )
        lengthState = RAW_LENGTH_UNKNOWN;
    else
    {
        HRESULT framing = S_OK;
        if ( raw_has_transfer_encoding( request, &framing ) ) lengthState = RAW_LENGTH_UNKNOWN;
        else if ( FAILED( framing ) )
        {
            hr = framing;
            goto cleanup;
        }
        else lengthState = raw_content_length( request, &declared );
    }
    if ( lengthState == RAW_LENGTH_INVALID )
    {
        hr = HRESULT_FROM_WIN32( ERROR_WINHTTP_INVALID_SERVER_RESPONSE );
        goto cleanup;
    }

    /* Geometric growth keeps allocation and copying amortized linear in the
     * response size instead of reallocating for every received block. */
    for ( ;; )
    {
        if ( !WinHttpQueryDataAvailable( request, &size ) ) goto error;
        if ( !size ) break;
        const SIZE_T used = *bufferSize;
        if ( size > SIZE_MAX - used - 1 )
        {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }
        if ( used + size + 1 > capacity )
        {
            SIZE_T grown = capacity ? capacity : 8192;
            while ( grown < used + size + 1 )
            {
                if ( grown > ( SIZE_MAX - 1 ) / 2 )
                {
                    grown = used + size + 1;
                    break;
                }
                grown *= 2;
            }
            if ( !( tmpBuffer = static_cast<std::remove_reference_t<decltype( tmpBuffer )>>( realloc( *buffer, grown ) ) ) )
            {
                hr = E_OUTOFMEMORY;
                goto cleanup;
            }
            *buffer = tmpBuffer;
            capacity = grown;
        }
        if ( !WinHttpReadData( request, *buffer + used, size, &size ) ) goto error;
        *bufferSize = used + size;
        ( *buffer )[*bufferSize] = 0;
        if ( !size ) break;
    }
    if ( lengthState == RAW_LENGTH_VALID && *bufferSize != declared )
    {
        /* A short body under a valid declared length is a truncated transfer,
         * not a complete response; never publish the partial bytes. */
        hr = HRESULT_FROM_WIN32( ERROR_WINHTTP_CONNECTION_ERROR );
        goto cleanup;
    }
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    if ( request ) WinHttpCloseHandle( request );
    if ( connection ) WinHttpCloseHandle( connection );
    if ( session ) WinHttpCloseHandle( session );
    free( hostName );
    if ( FAILED( hr ) )
    {
        free( *buffer );
        *buffer = NULL;
        *bufferSize = 0;
    }

    return hr;
}

#define encode_base64_( sfx, type, alph )                                                                              \
    HRESULT encode_base64##sfx( const UINT32 dataSize, const BYTE *data, const UINT32 base64Size, type *base64,        \
                                BOOLEAN pad )                                                                          \
    {                                                                                                                  \
        static const char alphabet[65] = alph;                                                                         \
        const BYTE *inp = data;                                                                                        \
        type *out = base64;                                                                                            \
                                                                                                                       \
        const UINT64 required = pad ? ( ( UINT64( dataSize ) + 2 ) / 3 ) * 4                                  \
                                    : ( UINT64( dataSize ) * 8 + 5 ) / 6;                                     \
        if ( required > base64Size )                                                                                  \
            return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );                                                    \
                                                                                                                       \
        for ( UINT32 i = 0; i < dataSize / 3; i++ )                                                                    \
        {                                                                                                              \
            /* first 6 bits of byte 0 */                                                                               \
            *out++ = alphabet[( inp[0] >> 2 ) & 0x3f];                                                                 \
            /* last 2 bits of byte 0, first 4 bits of byte 1 */                                                        \
            *out++ = alphabet[( ( inp[0] << 4 ) & 0x30 ) | ( ( inp[1] >> 4 ) & 0x0f )];                                \
            /* last 4 bits of byte 1, first 2 bits of byte 2 */                                                        \
            *out++ = alphabet[( ( inp[1] << 2 ) & 0x3c ) | ( ( inp[2] >> 6 ) & 0x03 )];                                \
            /* last 6 bits of byte 2 */                                                                                \
            *out++ = alphabet[inp[2] & 0x3f];                                                                          \
            inp += 3;                                                                                                  \
        }                                                                                                              \
                                                                                                                       \
        switch ( dataSize % 3 )                                                                                        \
        {                                                                                                              \
        case 1:                                                                                                        \
            /* first 6 bits of byte 0 */                                                                               \
            *out++ = alphabet[( inp[0] >> 2 ) & 0x3f];                                                                 \
            /* last 2 bits of byte 0, rest 0 */                                                                        \
            *out++ = alphabet[( ( inp[0] << 4 ) & 0x30 )];                                                             \
            /* padding */                                                                                              \
            if ( pad )                                                                                                 \
            {                                                                                                          \
                *out++ = '=';                                                                                          \
                *out++ = '=';                                                                                          \
            }                                                                                                          \
            break;                                                                                                     \
        case 2:                                                                                                        \
            /* first 6 bits of byte 0 */                                                                               \
            *out++ = alphabet[( inp[0] >> 2 ) & 0x3f];                                                                 \
            /* last 2 bits of byte 0, first 4 bits of byte 1 */                                                        \
            *out++ = alphabet[( ( inp[0] << 4 ) & 0x30 ) | ( ( inp[1] >> 4 ) & 0x0f )];                                \
            /* last 4 bits of byte 1, rest 0 */                                                                        \
            *out++ = alphabet[( ( inp[1] << 2 ) & 0x3c )];                                                             \
            /* padding */                                                                                              \
            if ( pad ) *out++ = '=';                                                                                   \
            break;                                                                                                     \
        }                                                                                                              \
        return S_OK;                                                                                                   \
    }

encode_base64_( _utf16, WCHAR, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" )
    encode_base64_( _url, char, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" )
        encode_base64_(, char, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" )

#define get_json_( sfx, typ, ret )                                                                                     \
    HRESULT get_json_##sfx( IJsonObject *object, const WCHAR *key, ret value )                                         \
    {                                                                                                                  \
        HSTRING_HEADER hdr;                                                                                            \
        HSTRING str;                                                                                                   \
        HRESULT hr;                                                                                                    \
                                                                                                                       \
        if ( FAILED( hr = WindowsCreateStringReference( key, wcslen( key ), &hdr, &str ) ) ) return hr;                \
        return object->GetNamed##typ( str, value );                                                                    \
    }

            get_json_( object, Object, IJsonObject ** ) get_json_( array, Array, IJsonArray ** )
                get_json_( boolean, Boolean, boolean * ) get_json_( value, Value, IJsonValue ** )
                    get_json_( string, String, HSTRING * ) get_json_( number, Number, DOUBLE * )
