// SPDX-License-Identifier: MIT
// Focused native fixture for the actual http_request_raw response reader.
// Compiles the real src/HttpClient.cpp (no copied alternate implementation) and
// routes the fixed HTTPS endpoint to a private loopback port. Plaintext only at
// that boundary; no account, remote cloud, TLS or title proof.
#include "../../src/private.h"
#include "../../src/HttpClient.h"
#include <winhttp.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

static INTERNET_PORT fixturePort;
static HINTERNET WINAPI route_connect( HINTERNET session, LPCWSTR, INTERNET_PORT, DWORD reserved )
{
    return WinHttpConnect( session, L"127.0.0.1", fixturePort, reserved );
}
static HINTERNET WINAPI route_request( HINTERNET connection, LPCWSTR method, LPCWSTR path, LPCWSTR version,
                                       LPCWSTR referer, LPCWSTR *accept, DWORD flags )
{
    return WinHttpOpenRequest( connection, method, path, version, referer, accept, flags & ~WINHTTP_FLAG_SECURE );
}
static HINTERNET WINAPI no_proxy( LPCWSTR agent, DWORD, LPCWSTR, LPCWSTR, DWORD flags )
{
    return WinHttpOpen( agent, WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, flags );
}
/* Hooks for the production reader compiled below: they observe its allocation
 * behaviour and inject the bounded failures the fixture needs. */
static SIZE_T realloc_calls, realloc_requested, realloc_moved, realloc_previous;
static SIZE_T fail_realloc_after = SIZE_MAX;
static void *fixture_realloc( void *pointer, SIZE_T size )
{
    if ( realloc_calls >= fail_realloc_after )
    {
        ++realloc_calls;
        return nullptr;
    }
    uintptr_t old = ( uintptr_t )pointer;
    SIZE_T old_size = pointer ? realloc_previous : 0;
    void *result = realloc( pointer, size );
    ++realloc_calls;
    realloc_requested += size;
    if ( result )
    {
        if ( old && old != ( uintptr_t )result ) realloc_moved += old_size;
        realloc_previous = size;
    }
    return result;
}
static BOOL fail_read;
static BOOL WINAPI read_failure( HINTERNET, LPVOID, DWORD, LPDWORD )
{
    SetLastError( ERROR_READ_FAULT );
    return FALSE;
}

static BOOL WINAPI read_dispatch( HINTERNET request, LPVOID buffer, DWORD to_read, LPDWORD read )
{
    if ( fail_read ) return read_failure( request, buffer, to_read, read );
    return WinHttpReadData( request, buffer, to_read, read );
}

#define WinHttpConnect route_connect
#define WinHttpOpenRequest route_request
#define WinHttpOpen no_proxy
#define WinHttpReadData read_dispatch
#define realloc fixture_realloc
#include "../../src/HttpClient.cpp"
#undef realloc
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpOpen
#undef WinHttpReadData

static unsigned checks, failures;
#define CHECK( expr )                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            ++failures;                                                                                                \
            printf( "FAIL line=%u %s error=%lu\n", __LINE__, #expr, GetLastError() );                                  \
        }                                                                                                              \
    } while ( 0 )

enum Case
{
    Full,
    FullFramed,
    Truncated,
    Empty,
    NoLength,
    Chunked,
    Head,
    HeadWithLength,
    LowercaseHead,
    HeadMalformedLength,
    NoContent,
    NotModified,
    MalformedLength,
    OverflowLength,
    ChunkedConflictingLength,
    ReadFailure,
    AllocFailure,
    NoConnection,
    EarlyDisconnect,
};

struct Fixture
{
    Case kind;
    SOCKET listener = INVALID_SOCKET;
    HANDLE ready = nullptr;
    HANDLE server = nullptr;
    LONG port = 0;
    DWORD worker_result = 1;
    BYTE *response = nullptr;
    SIZE_T size = 0;
    DWORD status = 0;
    HRESULT hr = E_PENDING;
};

static const SIZE_T FULL_SIZE = 16u * 1024u * 1024u;

static void fill( BYTE *data, SIZE_T size )
{
    for ( SIZE_T i = 0; i < size; i++ ) data[i] = ( BYTE )( i & 255 );
}

/* Bounded readiness waits; every socket operation stays nonblocking. */
static BOOL wait_readable( SOCKET socket, DWORD timeout )
{
    fd_set set;
    timeval tv;
    FD_ZERO( &set );
    FD_SET( socket, &set );
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = ( timeout % 1000 ) * 1000;
    return select( 0, &set, NULL, NULL, &tv ) == 1;
}
static BOOL wait_writable( SOCKET socket, DWORD timeout )
{
    fd_set set;
    timeval tv;
    FD_ZERO( &set );
    FD_SET( socket, &set );
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = ( timeout % 1000 ) * 1000;
    return select( 0, NULL, &set, NULL, &tv ) == 1;
}
static BOOL send_all( SOCKET client, const char *data, SIZE_T size )
{
    while ( size )
    {
        if ( !wait_writable( client, 5000 ) ) return FALSE;
        int sent = send( client, data, ( int )size, 0 );
        if ( sent <= 0 )
        {
            if ( WSAGetLastError() == WSAEWOULDBLOCK ) continue;
            return FALSE;
        }
        data += sent;
        size -= ( SIZE_T )sent;
    }
    return TRUE;
}
/* Exact wire length of a literal, never a hand-counted constant. */
static void send_literal( SOCKET client, const char *literal )
{
    send_all( client, literal, strlen( literal ) );
}

/* The worker owns every socket it touches. A failed join never returns to the
 * stack owner: the test process aborts instead of reusing live worker state. */
static void join_worker( Fixture &f )
{
    DWORD wait = WaitForSingleObject( f.server, 15000 );
    if ( wait == WAIT_TIMEOUT && f.listener != INVALID_SOCKET )
    {
        closesocket( f.listener );
        f.listener = INVALID_SOCKET;
        wait = WaitForSingleObject( f.server, 15000 );
    }
    if ( wait != WAIT_OBJECT_0 )
    {
        printf( "FATAL worker did not exit (%lu)\n", wait );
        fflush( stdout );
        ExitProcess( 1 );
    }
    CloseHandle( f.server );
    f.server = nullptr;
    if ( f.listener != INVALID_SOCKET )
    {
        closesocket( f.listener );
        f.listener = INVALID_SOCKET;
    }
}

static DWORD WINAPI serve( void *context )
{
    Fixture &f = *static_cast<Fixture *>( context );
    sockaddr_in address = {};
    int length = sizeof( address );
    u_long nonblocking = 1;
    DWORD result = 1;
    SOCKET client = INVALID_SOCKET;
    char head[8192] = { 0 };
    DWORD used = 0;
    char header[512];
    std::vector<BYTE> body( FULL_SIZE );

    f.listener = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( f.listener == INVALID_SOCKET ) goto ready;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    if ( bind( f.listener, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) ||
         listen( f.listener, 1 ) || getsockname( f.listener, reinterpret_cast<sockaddr *>( &address ), &length ) ||
         ioctlsocket( f.listener, FIONBIO, &nonblocking ) )
        goto ready;
    f.port = ntohs( address.sin_port );
    result = 0;
ready:
    SetEvent( f.ready );
    if ( result ) return result;

    if ( f.kind == NoConnection )
    {
        /* Never accept: the client must fail its own connect within its timeout. */
        f.worker_result = 0;
        Sleep( 6000 );
        return 0;
    }
    if ( !wait_readable( f.listener, 5000 ) ) return 1;
    client = accept( f.listener, NULL, NULL );
    if ( client == INVALID_SOCKET ) return 1;
    if ( ioctlsocket( client, FIONBIO, &nonblocking ) ) goto done;
    result = 1;
    if ( f.kind == EarlyDisconnect )
    {
        /* Close before any response byte: a transport failure, never data. */
        f.worker_result = 0;
        result = 0;
        goto done;
    }
    for ( ;; )
    {
        if ( strstr( head, "\r\n\r\n" ) ) break;
        if ( used >= sizeof( head ) - 1 ) goto done;
        if ( !wait_readable( client, 5000 ) ) goto done;
        int count = recv( client, head + used, ( int )( sizeof( head ) - used - 1 ), 0 );
        if ( count <= 0 )
        {
            if ( count < 0 && WSAGetLastError() == WSAEWOULDBLOCK ) continue;
            goto done;
        }
        used += ( DWORD )count;
        head[used] = 0;
    }
    fill( body.data(), body.size() );
    switch ( f.kind )
    {
    case Full:
        snprintf( header, sizeof( header ), "HTTP/1.1 200 OK\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",
                  ( unsigned long long )FULL_SIZE );
        send_literal( client, header );
        if ( !send_all( client, ( const char * )body.data(), body.size() ) ) goto done;
        break;
    case FullFramed:
        send_literal( client, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n" );
        for ( SIZE_T offset = 0; offset < body.size(); offset += 65536 )
        {
            char chunk[32];
            SIZE_T count = body.size() - offset < 65536 ? body.size() - offset : 65536;
            snprintf( chunk, sizeof( chunk ), "%llx\r\n", ( unsigned long long )count );
            send_literal( client, chunk );
            if ( !send_all( client, ( const char * )body.data() + offset, count ) ) goto done;
            send_literal( client, "\r\n" );
        }
        send_literal( client, "0\r\n\r\n" );
        break;
    case Truncated:
    case ReadFailure:
    case AllocFailure:
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 1048576\r\nConnection: close\r\n\r\n" );
        {
            SIZE_T count = f.kind == Truncated ? 32768 : f.kind == ReadFailure ? 65536 : 262144;
            if ( !send_all( client, ( const char * )body.data(), count ) ) goto done;
        }
        break;
    case Empty:
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
        break;
    case NoLength:
        send_literal( client, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" );
        if ( !send_all( client, ( const char * )body.data(), 4096 ) ) goto done;
        break;
    case Chunked:
        /* Wine reads the first chunk size together with the header block, so the
         * size line shares that write and the payload follows separately. */
        send_literal( client, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\n" );
        if ( !send_all( client, "hello", 5 ) ) goto done;
        send_literal( client, "\r\n0\r\n\r\n" );
        break;
    case ChunkedConflictingLength:
        /* Transfer-Encoding wins: the body is exactly the framed five bytes. */
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 4096\r\nTransfer-Encoding: chunked\r\n"
                              "Connection: close\r\n\r\n5\r\n" );
        if ( !send_all( client, "hello", 5 ) ) goto done;
        send_literal( client, "\r\n0\r\n\r\n" );
        break;
    case Head:
        send_literal( client, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" );
        break;
    case HeadWithLength:
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 4096\r\nConnection: close\r\n\r\n" );
        break;
    case HeadMalformedLength:
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 12abc\r\nConnection: close\r\n\r\n" );
        break;
    case LowercaseHead:
        /* The method is case-sensitive: a lowercase 'head' is an ordinary request
         * and a short body under its declared length must fail. */
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 1048576\r\nConnection: close\r\n\r\n" );
        if ( !send_all( client, ( const char * )body.data(), 32768 ) ) goto done;
        break;
    case NoContent:
        send_literal( client, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n" );
        break;
    case NotModified:
        send_literal( client, "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n" );
        break;
    case MalformedLength:
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 12abc\r\nConnection: close\r\n\r\n" );
        if ( !send_all( client, ( const char * )body.data(), 4096 ) ) goto done;
        break;
    case OverflowLength:
        send_literal( client, "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999\r\nConnection: close\r\n\r\n" );
        if ( !send_all( client, ( const char * )body.data(), 4096 ) ) goto done;
        break;
    default:
        goto done;
    }
    f.worker_result = 0;
    result = 0;
done:
    if ( client != INVALID_SOCKET )
    {
        shutdown( client, SD_SEND );
        char drain[4096];
        DWORD deadline = GetTickCount() + 5000;
        for ( ;; )
        {
            if ( !wait_readable( client, 200 ) )
            {
                if ( GetTickCount() > deadline ) break;
                continue;
            }
            int count = recv( client, drain, sizeof( drain ), 0 );
            if ( count <= 0 )
            {
                if ( count < 0 && WSAGetLastError() == WSAEWOULDBLOCK ) continue;
                break;
            }
        }
        closesocket( client );
    }
    return result;
}

static const WCHAR *method_of( Case kind )
{
    switch ( kind )
    {
    case Head:
    case HeadWithLength:
    case HeadMalformedLength:
        return L"HEAD";
    case LowercaseHead:
        return L"head";
    default:
        return L"GET";
    }
}

static void run( Case kind, const char *name )
{
    Fixture f{ kind };
    f.ready = CreateEventW( nullptr, TRUE, FALSE, nullptr );
    f.server = CreateThread( nullptr, 0, serve, &f, 0, nullptr );
    CHECK( f.ready && f.server );
    CHECK( WaitForSingleObject( f.ready, 5000 ) == WAIT_OBJECT_0 );
    fixturePort = ( INTERNET_PORT )f.port;
    if ( kind != NoConnection )
    {
        CHECK( f.port != 0 );
        if ( !f.port )
        {
            join_worker( f );
            CloseHandle( f.ready );
            return;
        }
    }

    realloc_calls = realloc_requested = realloc_moved = realloc_previous = 0;
    fail_realloc_after = kind == AllocFailure ? 2 : SIZE_MAX;
    fail_read = kind == ReadFailure;
    ULONGLONG begin = GetTickCount64();
    f.hr = http_request_raw( method_of( kind ), L"https://127.0.0.1/synthetic", nullptr, nullptr, 0, &f.status, &f.response,
                             &f.size );
    ULONGLONG elapsed = GetTickCount64() - begin;
    fail_read = false;
    fail_realloc_after = SIZE_MAX;
    join_worker( f );
    CloseHandle( f.ready );

    SIZE_T mismatches = 0;
    for ( SIZE_T i = 0; i < f.size && i < FULL_SIZE; i++ )
        if ( f.response[i] != ( BYTE )( i & 255 ) ) ++mismatches;
    printf( "CASE=%s hr=%08lx status=%lu bytes=%llu expected=%lld elapsed_ms=%llu realloc_calls=%llu "
            "requested_total=%llu moved_prefix_bytes=%llu byte_errors=%llu worker=%lu\n",
            name, ( unsigned long )f.hr, f.status, ( unsigned long long )f.size,
            kind == Full || kind == FullFramed ? ( long long )FULL_SIZE : -1, ( unsigned long long )elapsed,
            ( unsigned long long )realloc_calls, ( unsigned long long )realloc_requested,
            ( unsigned long long )realloc_moved, ( unsigned long long )mismatches, f.worker_result );
    fflush( stdout );

    switch ( kind )
    {
    case Full:
    case FullFramed:
        CHECK( f.hr == S_OK );
        CHECK( f.status == 200 );
        CHECK( f.size == FULL_SIZE );
        CHECK( mismatches == 0 );
        CHECK( f.response && f.response[f.size] == 0 );
        /* Geometric growth: bounded allocations and amortized linear copying. */
        CHECK( realloc_calls <= 128 );
        CHECK( realloc_moved < 4 * FULL_SIZE );
        break;
    case Truncated:
    case LowercaseHead:
        CHECK( f.hr == HRESULT_FROM_WIN32( ERROR_WINHTTP_CONNECTION_ERROR ) );
        CHECK( f.status == 200 );
        CHECK( f.response == nullptr && f.size == 0 );
        break;
    case Empty:
        CHECK( f.hr == S_OK && f.status == 200 && f.size == 0 );
        break;
    case NoLength:
        CHECK( f.hr == S_OK && f.status == 200 && f.size == 4096 && mismatches == 0 );
        CHECK( f.response && f.response[f.size] == 0 );
        break;
    case Chunked:
    case ChunkedConflictingLength:
        CHECK( f.hr == S_OK && f.status == 200 );
        CHECK( f.size == 5 && !memcmp( f.response, "hello", 5 ) );
        CHECK( f.response[f.size] == 0 );
        break;
    case Head:
    case HeadWithLength:
    case HeadMalformedLength:
        /* Bodiless metadata: no length is compared, malformed CL included. */
        CHECK( f.hr == S_OK && f.status == 200 && f.size == 0 );
        break;
    case NoContent:
        CHECK( f.hr == S_OK && f.status == 204 && f.size == 0 );
        break;
    case NotModified:
        CHECK( f.hr == S_OK && f.status == 304 && f.size == 0 );
        break;
    case MalformedLength:
    case OverflowLength:
        CHECK( f.hr == HRESULT_FROM_WIN32( ERROR_WINHTTP_INVALID_SERVER_RESPONSE ) );
        CHECK( f.response == nullptr && f.size == 0 );
        break;
    case ReadFailure:
        CHECK( FAILED( f.hr ) );
        CHECK( f.response == nullptr && f.size == 0 );
        break;
    case AllocFailure:
        CHECK( f.hr == E_OUTOFMEMORY );
        CHECK( f.response == nullptr && f.size == 0 );
        break;
    case NoConnection:
    case EarlyDisconnect:
        CHECK( FAILED( f.hr ) );
        CHECK( f.response == nullptr && f.size == 0 );
        CHECK( f.worker_result == 0 );
        break;
    }
    free( f.response );
}

int main()
{
    setvbuf( stdout, nullptr, _IONBF, 0 );
    InitializeLogging();
    WSADATA data;
    CHECK( !WSAStartup( MAKEWORD( 2, 2 ), &data ) );
    run( Full, "full-16mib" );
    run( FullFramed, "full-16mib-chunked" );
    run( Truncated, "truncated-cl" );
    run( Empty, "empty" );
    run( NoLength, "unknown-length" );
    run( Chunked, "chunked" );
    run( ChunkedConflictingLength, "chunked-conflicting-cl" );
    run( Head, "head" );
    run( HeadWithLength, "head-with-length" );
    run( HeadMalformedLength, "head-malformed-cl" );
    run( LowercaseHead, "lowercase-head-truncated" );
    run( NoContent, "204" );
    run( NotModified, "304" );
    run( MalformedLength, "malformed-cl" );
    run( OverflowLength, "overflow-cl" );
    run( ReadFailure, "read-failure" );
    run( AllocFailure, "alloc-failure" );
    run( NoConnection, "no-connection" );
    run( EarlyDisconnect, "early-disconnect" );
    WSACleanup();
    printf( "CHECKS %u FAILURES %u\n", checks, failures );
    return failures ? 1 : 0;
}
