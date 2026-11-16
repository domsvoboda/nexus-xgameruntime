/* SPDX-License-Identifier: MIT
 * Actual save upload/close/sync code, two synthetic device roots and a closed
 * in-memory transport. No real HTTP, accounts, credentials or game saves. */

#include "../../src/private.h"
#include "../../src/System/GameSave/Transport.h"
#include "../../src/Runtime/Configuration.h"
#include "../../src/HttpClient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures, user_closes;

#define CHECK( name, expr )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if ( !( expr ) )                                                                                               \
        {                                                                                                              \
            ++failures;                                                                                                \
            printf( "FAIL %s line=%d\n", name, __LINE__ );                                                             \
        }                                                                                                              \
    } while ( 0 )

class ClosedSaveTransport final : public SaveTransport
{
public:
    HRESULT Authorization( XUserHandle, const char *, char ** ) override;
    HRESULT SignedRequest( XUserHandle, const char *, const char *, const char *, const char *, const void *, DWORD,
                           DWORD *, BYTE **, SIZE_T * ) override;
    HRESULT DirectRequest( const WCHAR *, const WCHAR *, const WCHAR *, const void *, DWORD, DWORD *, BYTE **,
                           SIZE_T * ) override;
    void CloseUser( XUserHandle ) override
    {
        ++user_closes;
    }
};
static ClosedSaveTransport closedTransport;
HRESULT ClosedSaveTransport::Authorization( XUserHandle user, const char *relying, char **auth )
{
    *auth = strdup( "synthetic-not-a-token" );
    return *auth ? S_OK : E_OUTOFMEMORY;
}

HRESULT ClosedSaveTransport::DirectRequest( const WCHAR *method, const WCHAR *url, const WCHAR *headers,
                                            const void *body, DWORD count, DWORD *status, BYTE **out, SIZE_T *size )
{
    *status = 403;
    *out = NULL;
    *size = 0;
    return E_ACCESSDENIED;
}
static struct
{
    char atom[16][48], bytes[16][64], metadata[4096], time[48];
    unsigned atoms, blob_puts, metadata_puts, releases, announced, calls, rejects;
    BOOL locked, fail_blob, fail_metadata, fail_release;
    HANDLE entered, resume;
    /* Exact lock query values as sent, decoded once per request. */
    char lock_query[8][1024], lock_decoded[8][1024];
    unsigned lock_queries, lock_undecodable;
} remote;
HRESULT ClosedSaveTransport::SignedRequest( XUserHandle user, const char *method, const char *url, const char *auth,
                                            const char *headers, const void *body, DWORD count, DWORD *status,
                                            BYTE **out, SIZE_T *size )
{
    const char *path, *response = "{}";
    char listing[512];
    ++remote.calls;
    *out = NULL;
    *size = 0;
    *status = 200;
    if ( strncmp( url, "https://fixture.invalid", 23 ) )
    {
        ++remote.rejects;
        return E_ACCESSDENIED;
    }
    path = url + 23;
    if ( !strcmp( method, "PUT" ) && !strncmp( path, "/lock?", 6 ) )
    {
        /* Capture the raw query and its percent-decoded value. A malformed
         * escape or an overlong value is recorded, never truncated. */
        static const char digits[] = "0123456789abcdef";
        const char *query = path + 6;
        char *decoded = remote.lock_decoded[remote.lock_queries < 8 ? remote.lock_queries : 7];
        SIZE_T used = 0;
        if ( remote.lock_queries < 8 && strlen( query ) < sizeof( remote.lock_query[0] ) )
            strcpy( remote.lock_query[remote.lock_queries], query );
        else
            remote.lock_query[remote.lock_queries < 8 ? remote.lock_queries : 7][0] = 0;
        decoded[0] = 0;
        for ( const char *p = query; *p && used + 1 < sizeof( remote.lock_decoded[0] ); p++ )
        {
            if ( *p == '%' )
            {
                const char *high = p[1] ? strchr( digits, tolower( ( unsigned char )p[1] ) ) : NULL;
                const char *low = p[1] && p[2] ? strchr( digits, tolower( ( unsigned char )p[2] ) ) : NULL;
                if ( !high || !low )
                {
                    ++remote.lock_undecodable;
                    break;
                }
                decoded[used++] = ( char )( ( high - digits ) * 16 + ( low - digits ) );
                p += 2;
            }
            else
                decoded[used++] = *p;
        }
        decoded[used] = 0;
        ++remote.lock_queries;
        if ( remote.locked )
            *status = 409;
        else
        {
            remote.locked = TRUE;
            *status = 201;
        }
    }
    else if ( !strcmp( method, "DELETE" ) && !strncmp( path, "/lock?newSavesUploaded=", 23 ) )
    {
        ++remote.releases;
        if ( remote.fail_release )
            *status = 503;
        else
        {
            remote.locked = FALSE;
            if ( strstr( path, "=true" ) ) ++remote.announced;
        }
    }
    else if ( !strcmp( method, "GET" ) && !*path )
    {
        if ( *remote.metadata )
            snprintf(
                listing, sizeof( listing ),
                "{\"blobs\":[{\"fileName\":\"Save,savedgame\",\"displayName\":\"Save\",\"clientFileTime\":\"%s\"}]}",
                remote.time );
        else
            strcpy( listing, "{\"blobs\":[]}" );
        response = listing;
    }
    else if ( !strcmp( method, "PUT" ) && !strncmp( path, "/Save,savedgame?clientFileTime=", 31 ) )
    {
        const char *time = strstr( path, "clientFileTime=" ) + 15, *end = strchr( time, '&' );
        ++remote.metadata_puts;
        if ( remote.fail_metadata )
            *status = 503;
        else if ( !end || end - time >= sizeof( remote.time ) || count >= sizeof( remote.metadata ) )
            return E_INVALIDARG;
        else
        {
            memcpy( remote.time, time, end - time );
            remote.time[end - time] = 0;
            memcpy( remote.metadata, body, count );
            remote.metadata[count] = 0;
        }
    }
    else if ( !strcmp( method, "GET" ) && !strcmp( path, "/Save,savedgame" ) )
    {
        if ( *remote.metadata )
            response = remote.metadata;
        else
            *status = 404;
    }
    else if ( !strcmp( method, "PUT" ) && strlen( path ) == 44 && !strcmp( path + 37, ",binary" ) )
    {
        ++remote.blob_puts;
        if ( remote.entered )
        {
            SetEvent( remote.entered );
            if ( WaitForSingleObject( remote.resume, 5000 ) != WAIT_OBJECT_0 ) return E_ABORT;
        }
        if ( remote.fail_blob )
            *status = 503;
        else if ( remote.atoms == 16 || count >= sizeof( remote.bytes[0] ) )
            return E_OUTOFMEMORY;
        else
        {
            strcpy( remote.atom[remote.atoms], path + 1 );
            memcpy( remote.bytes[remote.atoms], body, count );
            remote.bytes[remote.atoms][count] = 0;
            ++remote.atoms;
        }
    }
    else if ( !strcmp( method, "GET" ) )
    {
        unsigned i;
        for ( i = 0; i < remote.atoms; i++ )
            if ( !strcmp( path + 1, remote.atom[i] ) ) break;
        if ( i == remote.atoms )
        {
            ++remote.rejects;
            return E_ACCESSDENIED;
        }
        response = remote.bytes[i];
    }
    else
    {
        ++remote.rejects;
        return E_ACCESSDENIED;
    }
    *size = strlen( response );
    *out = static_cast<std::remove_reference_t<decltype( *out )>>( malloc( *size + 1 ) );
    if ( !*out ) return E_OUTOFMEMORY;
    memcpy( *out, response, *size + 1 );
    return S_OK;
}
/* Test-only seam for the system-name source. Wine reads the prefix registry,
 * so a fixture cannot make GetComputerNameExW fail; the macro must precede the
 * production include because macros never apply retroactively. The shim below
 * forwards to the real API, so production behaviour is unchanged. */
static BOOL test_system_hostname_fails;
static BOOL WINAPI test_GetComputerNameExW( COMPUTER_NAME_FORMAT format, LPWSTR name, LPDWORD size );
#define GetComputerNameExW test_GetComputerNameExW
#include "../../src/System/XGameSave.cpp"
#undef GetComputerNameExW

static char root[1024];
static unsigned serial;
static struct XGameSaveProvider *device( BOOL files, const char *label )
{
    struct XGameSaveProvider *p = static_cast<std::remove_reference_t<decltype( p )>>( calloc( 1, sizeof( *p ) ) );
    if ( !p ) return NULL;
    p->transport = &closedTransport;
    p->files = files;
    p->cloud = TRUE;
    p->quota = SAVE_QUOTA;
    p->user = reinterpret_cast<XUserHandle>( 1 );
    snprintf( p->root, sizeof( p->root ), "%s\\%u-%s", root, ++serial, label );
    strcpy( p->base, "https://fixture.invalid" );
    InitializeCriticalSection( &p->cs );
    create_directories( p->root );
    return p;
}
static void seed( struct XGameSaveProvider *p, const char *value, BOOL dirty )
{
    struct container_state s = { 0 };
    char file[2048], dir[2048];
    struct blob_entry *blob;
    s.name = strdup( "Save" );
    s.display = strdup( "Save" );
    s.time = parse_iso_time( "2026-10-22T12:00:00Z" );
    s.dirty = dirty;
    s.revision = 1;
    container_dir( p, s.name, dir, sizeof( dir ) );
    create_directories( dir );
    blob = add_blob( &s, "progress" );
    blob->size = strlen( value );
    blob_path( p, s.name, blob->name, file, sizeof( file ) );
    CHECK( "seed-bytes", write_file( file, value, strlen( value ) ) == S_OK );
    CHECK( "seed-index", save_state( p, &s ) == S_OK );
    free_state( &s );
}
static void read_progress( struct XGameSaveProvider *p, const char *expected, BOOL dirty )
{
    char file[2048];
    BYTE *bytes = NULL;
    SIZE_T size = 0;
    struct container_state s;
    HRESULT hr = load_state( p, "Save", &s );
    CHECK( "saved-index", hr == S_OK );
    if ( SUCCEEDED( hr ) )
    {
        CHECK( "saved-dirty", s.dirty == dirty );
        free_state( &s );
    }
    blob_path( p, "Save", "progress", file, sizeof( file ) );
    hr = read_file( file, &bytes, &size );
    CHECK( "exact-progress-bytes", hr == S_OK && size == strlen( expected ) && !memcmp( bytes, expected, size ) );
    free( bytes );
}
static void start_uploads( struct XGameSaveProvider *p )
{
    InitializeCriticalSection( &p->cloud_cs );
    p->upload_event = CreateEventW( NULL, FALSE, FALSE, NULL );
    p->upload_thread = CreateThread( NULL, 0, upload_worker, p, 0, NULL );
    CHECK( "upload-thread", p->upload_event && p->upload_thread );
}
static DWORD WINAPI close_on_thread( void *p )
{
    close_provider( static_cast<XGameSaveProvider *>( p ) );
    return 0;
}
static void roundtrip( BOOL files )
{
    struct XGameSaveProvider *a, *b;
    char aroot[2048];
    HANDLE closing;
    printf( "CASE delayed close roundtrip files=%d\n", files );
    memset( &remote, 0, sizeof( remote ) );
    a = device( files, "A" );
    seed( a, "progress-on-A", TRUE );
    strcpy( aroot, a->root );
    remote.entered = CreateEventW( NULL, TRUE, FALSE, NULL );
    remote.resume = CreateEventW( NULL, TRUE, FALSE, NULL );
    start_uploads( a );
    EnterCriticalSection( &a->cs );
    queue_upload( a, "Save" );
    LeaveCriticalSection( &a->cs );
    CHECK( "upload-started", WaitForSingleObject( remote.entered, 5000 ) == WAIT_OBJECT_0 );
    closing = CreateThread( NULL, 0, close_on_thread, a, 0, NULL );
    CHECK( "close-waits-for-cloud", WaitForSingleObject( closing, 50 ) == WAIT_TIMEOUT );
    SetEvent( remote.resume );
    CHECK( "close-completes", WaitForSingleObject( closing, 5000 ) == WAIT_OBJECT_0 );
    CloseHandle( closing );
    CloseHandle( remote.entered );
    CloseHandle( remote.resume );
    remote.entered = remote.resume = NULL;
    CHECK( "cloud-committed", remote.metadata_puts == 1 && remote.announced == 1 && !remote.locked );
    b = device( files, "B" );
    CHECK( "device-B-download", cloud_sync( b ) == S_OK );
    read_progress( b, "progress-on-A", FALSE );
    close_provider( b );
    a = device( files, "A-reopen" );
    strcpy( a->root, aroot );
    CHECK( "A-reopen-sync", cloud_sync( a ) == S_OK );
    read_progress( a, "progress-on-A", FALSE );
    close_provider( a );
    CHECK( "closed-transport", !remote.rejects );
}
static void failed_sync( BOOL files, BOOL known_remote, BOOL metadata, BOOL force )
{
    struct XGameSaveProvider *a;
    struct container_state s;
    printf( "CASE failed retry files=%d known=%d metadata=%d force=%d\n", files, known_remote, metadata, force );
    memset( &remote, 0, sizeof( remote ) );
    a = device( files, "retry" );
    seed( a, "unsent-local", TRUE );
    if ( known_remote )
    {
        strcpy( remote.metadata, "{\"atoms\":{}}" );
        strcpy( remote.time, "2026-10-20T12:00:00Z" );
        load_state( a, "Save", &s );
        strcpy( s.remote, remote.time );
        save_state( a, &s );
        free_state( &s );
    }
    remote.fail_blob = !metadata;
    remote.fail_metadata = metadata;
    SetEnvironmentVariableA( "XODUS_SAVE_SYNC", force ? "upload" : "auto" );
    CHECK( "failed-upload-propagates", FAILED( cloud_sync( a ) ) );
    CHECK( "failed-upload-not-announced", !remote.announced && ( metadata || !remote.metadata_puts ) );
    read_progress( a, "unsent-local", TRUE );
    close_provider( a );
    SetEnvironmentVariableA( "XODUS_SAVE_SYNC", "auto" );
}
static void session_announcement( BOOL files )
{
    struct XGameSaveProvider *a, *b;
    struct container_state s;
    printf( "CASE session release files=%d\n", files );
    memset( &remote, 0, sizeof( remote ) );
    a = device( files, "session" );
    seed( a, "session-progress", TRUE );
    CHECK( "session-lock", cloud_lock( a ) );
    a->session_lock = TRUE;
    load_state( a, "Save", &s );
    CHECK( "session-upload", cloud_upload( a, &s ) == S_OK );
    CHECK( "save-acknowledged-index", save_state( a, &s ) == S_OK );
    free_state( &s );
    CHECK( "session-retains-lock", remote.locked && remote.releases == 0 );
    a->session_lock = FALSE;
    close_provider( a );
    CHECK( "session-announces-actual-upload", remote.announced == 1 && !remote.locked );
    b = device( files, "session-B" );
    CHECK( "session-B-download", cloud_sync( b ) == S_OK );
    read_progress( b, "session-progress", FALSE );
    close_provider( b );
}
static void failed_release( BOOL files )
{
    struct XGameSaveProvider *a;
    struct container_state s;
    printf( "CASE failed release files=%d\n", files );
    memset( &remote, 0, sizeof( remote ) );
    a = device( files, "release" );
    seed( a, "release-progress", TRUE );
    CHECK( "release-lock", cloud_lock( a ) );
    a->session_lock = TRUE;
    load_state( a, "Save", &s );
    CHECK( "release-upload", cloud_upload( a, &s ) == S_OK );
    free_state( &s );
    a->session_lock = FALSE;
    remote.fail_release = TRUE;
    cloud_unlock( a, FALSE );
    CHECK( "failed-release-keeps-lock-state", a->locked && remote.locked && !remote.announced );
    remote.fail_release = FALSE;
    cloud_unlock( a, FALSE );
    CHECK( "release-retry-keeps-upload-notification", remote.announced == 1 && !remote.locked );
    close_provider( a );
}
static void release_does_not_ack_upload( BOOL files )
{
    struct XGameSaveProvider *a;
    struct container_state s;
    printf( "CASE unacknowledged upload release files=%d\n", files );
    memset( &remote, 0, sizeof( remote ) );
    a = device( files, "unacknowledged" );
    seed( a, "unacknowledged-progress", TRUE );
    load_state( a, "Save", &s );
    remote.fail_release = TRUE;
    CHECK( "failed-release-fails-upload", FAILED( cloud_upload( a, &s ) ) );
    CHECK( "failed-release-keeps-dirty", s.dirty && a->locked );
    free_state( &s );
    remote.fail_release = FALSE;
    cloud_unlock( a, FALSE );
    CHECK( "unacknowledged-release-retry", remote.announced == 1 && !remote.locked );
    close_provider( a );
}
static void updated_during_upload( BOOL files )
{
    struct XGameSaveProvider *a, *b;
    struct container_state s;
    printf( "CASE newer pending snapshot files=%d\n", files );
    memset( &remote, 0, sizeof( remote ) );
    a = device( files, "newer-A" );
    seed( a, "old-snapshot", TRUE );
    start_uploads( a );
    remote.entered = CreateEventW( NULL, TRUE, FALSE, NULL );
    remote.resume = CreateEventW( NULL, TRUE, FALSE, NULL );
    EnterCriticalSection( &a->cs );
    queue_upload( a, "Save" );
    LeaveCriticalSection( &a->cs );
    CHECK( "older-upload-in-flight", WaitForSingleObject( remote.entered, 5000 ) == WAIT_OBJECT_0 );
    EnterCriticalSection( &a->cs );
    seed( a, "latest-snapshot", TRUE );
    load_state( a, "Save", &s );
    s.revision = 2;
    CHECK( "newer-revision", save_state( a, &s ) == S_OK );
    free_state( &s );
    queue_upload( a, "Save" );
    LeaveCriticalSection( &a->cs );
    SetEvent( remote.resume );
    close_provider( a );
    CloseHandle( remote.entered );
    CloseHandle( remote.resume );
    remote.entered = remote.resume = NULL;
    CHECK( "both-snapshots-uploaded", remote.metadata_puts == 2 );
    b = device( files, "newer-B" );
    CHECK( "newer-device-B-sync", cloud_sync( b ) == S_OK );
    read_progress( b, "latest-snapshot", FALSE );
    close_provider( b );
}
static void failed_close_and_retry( BOOL files )
{
    struct XGameSaveProvider *a, *b;
    char saved_root[2048];
    printf( "CASE failed final metadata then relaunch files=%d\n", files );
    memset( &remote, 0, sizeof( remote ) );
    a = device( files, "failed-A" );
    seed( a, "retry-after-relaunch", TRUE );
    strcpy( saved_root, a->root );
    remote.fail_metadata = TRUE;
    start_uploads( a );
    EnterCriticalSection( &a->cs );
    queue_upload( a, "Save" );
    LeaveCriticalSection( &a->cs );
    close_provider( a );
    CHECK( "close-retried-once", remote.metadata_puts == 2 && !*remote.metadata && !remote.announced );
    a = device( files, "retry-A" );
    strcpy( a->root, saved_root );
    read_progress( a, "retry-after-relaunch", TRUE );
    remote.fail_metadata = FALSE;
    CHECK( "relaunch-retries-durable-save", cloud_sync( a ) == S_OK );
    read_progress( a, "retry-after-relaunch", FALSE );
    close_provider( a );
    b = device( files, "retry-B" );
    CHECK( "retry-device-B-sync", cloud_sync( b ) == S_OK );
    read_progress( b, "retry-after-relaunch", FALSE );
    close_provider( b );
}
static void check_view( const char *view, const char *expected )
{
    char file[2048];
    BYTE *bytes = NULL;
    SIZE_T size = 0;
    snprintf( file, sizeof( file ), "%s\\Save\\progress", view );
    HRESULT hr = files_read( file, &bytes, &size );
    CHECK( "exported-game-view", hr == S_OK && size == strlen( expected ) && !memcmp( bytes, expected, size ) );
    free( bytes );
}
static void file_views_roundtrip( void )
{
    struct XGameSaveProvider *a, *b;
    struct container_state s;
    char view_a[2048], view_b[2048], file[2048], aroot[2048];
    HRESULT hr;
    printf( "CASE actual file-view import/export A->B->A\n" );
    memset( &remote, 0, sizeof( remote ) );
    a = device( TRUE, "view-A" );
    strcpy( aroot, a->root );
    snprintf( view_a, sizeof( view_a ), "%s\\view-A", root );
    snprintf( view_b, sizeof( view_b ), "%s\\view-B", root );
    CHECK( "initial-empty-view", files_export( a, view_a ) == S_OK );
    snprintf( file, sizeof( file ), "%s\\Save", view_a );
    CHECK( "view-save-dir", files_mkdir( file ) == S_OK );
    snprintf( file, sizeof( file ), "%s\\Save\\progress", view_a );
    CHECK( "game-A-write", files_write( file, ( const BYTE * )"file-device-A", 13 ) == S_OK );
    CHECK( "game-A-import", files_import( a, view_a ) == S_OK );
    CHECK( "game-A-lock", cloud_lock( a ) );
    a->session_lock = TRUE;
    hr = S_OK;
    enumerate_local( a, files_upload_visit, &hr );
    CHECK( "game-A-upload", hr == S_OK );
    a->session_lock = FALSE;
    close_provider( a );
    b = device( TRUE, "view-B" );
    CHECK( "game-B-download", cloud_sync( b ) == S_OK );
    CHECK( "game-B-export", files_export( b, view_b ) == S_OK );
    check_view( view_b, "file-device-A" );
    snprintf( file, sizeof( file ), "%s\\Save\\progress", view_b );
    CHECK( "game-B-write", files_write( file, ( const BYTE * )"file-device-B", 13 ) == S_OK );
    CHECK( "game-B-import", files_import( b, view_b ) == S_OK );
    /* Distinct synthetic session times, without sleeps or changing host clock. */
    load_state( b, "Save", &s );
    s.time += 2;
    CHECK( "game-B-later-session", save_state( b, &s ) == S_OK );
    free_state( &s );
    CHECK( "game-B-lock", cloud_lock( b ) );
    b->session_lock = TRUE;
    hr = S_OK;
    enumerate_local( b, files_upload_visit, &hr );
    CHECK( "game-B-upload", hr == S_OK );
    b->session_lock = FALSE;
    close_provider( b );
    a = device( TRUE, "view-A-reopen" );
    strcpy( a->root, aroot );
    CHECK( "unchanged-A-import", files_import( a, view_a ) == S_OK );
    CHECK( "A-download-B-progress", cloud_sync( a ) == S_OK );
    CHECK( "A-reexport", files_export( a, view_a ) == S_OK );
    check_view( view_a, "file-device-B" );
    close_provider( a );
    CHECK( "both-sessions-announced", remote.announced == 2 );
    CHECK( "views-no-network", !remote.rejects );
}
/* The lock query is display metadata only. These cases pin the exact decoded
 * friendlyName VALUE sent by the production cloud_lock path, its raw
 * percent-encoding and its bounded input handling. The captured query must be
 * exactly one "friendlyName=" parameter, so no extra parameter can hide here.
 * Unicode input is written with SetEnvironmentVariableW, because Wine's ANSI
 * environment API uses the ANSI code page, not UTF-8. No real HTTP, account or
 * save is involved. */
static void lock_query( const char *label, const WCHAR *env, const char *expected, const char *raw, BOOL exact )
{
    struct XGameSaveProvider *a;
    char name[64];
    printf( "CASE lock friendly name %s\n", label );
    memset( &remote, 0, sizeof( remote ) );
    SetEnvironmentVariableW( L"NEXUS_DEVICE_HOSTNAME", env );
    a = device( FALSE, label );
    snprintf( name, sizeof( name ), "lock-%s", label );
    CHECK( name, cloud_lock( a ) );
    CHECK( name, remote.lock_queries == 1 && !remote.lock_undecodable );
    CHECK( name, !strncmp( remote.lock_query[0], "friendlyName=", 13 ) );
    CHECK( name, !strstr( remote.lock_query[0], "&" ) );
    /* An unusable value falls back to the system name when Wine has one, so
       only the encoding and label rules are pinned for those cases. */
    if ( exact )
    {
        CHECK( name, !strcmp( remote.lock_query[0] + 13, raw ) );
        CHECK( name, !strcmp( remote.lock_decoded[0] + 13, expected ) );
    }
    else
    {
        CHECK( name, !strstr( remote.lock_query[0] + 13, "%20" ) || strcmp( remote.lock_decoded[0] + 13, "Unknown device" ) );
        CHECK( name, strcmp( remote.lock_decoded[0] + 13, "Xodus" ) );
    }
    CHECK( name, remote.releases == 0 && remote.announced == 0 );
    close_provider( a );
    SetEnvironmentVariableW( L"NEXUS_DEVICE_HOSTNAME", NULL );
}
static BOOL WINAPI test_GetComputerNameExW( COMPUTER_NAME_FORMAT format, LPWSTR name, LPDWORD size )
{
    if ( test_system_hostname_fails )
    {
        SetLastError( ERROR_ENVVAR_NOT_FOUND );
        return FALSE;
    }
    return GetComputerNameExW( format, name, size );
}

static void lock_friendly_names( void )
{
    char oversized[2048], *p;
    /* The old hardcoded label is the discriminator for these cases. */
    lock_query( "mixed-case-host", L"GameRoom-PC", "GameRoom-PC", "GameRoom-PC", TRUE );
    lock_query( "escaped", L"a&b/c?d#e%f+g=h", "a&b/c?d#e%f+g=h", "a%26b%2Fc%3Fd%23e%25f%2Bg%3Dh", TRUE );
    lock_query( "space-and-unicode", L"desk 1 \u017e", "desk 1 \xc5\xbe", "desk%201%20%C5%BE", TRUE );
    lock_query( "utf8", L"st\u00e9\u017e\u00ed\u010d", "st\xc3\xa9\xc5\xbe\xc3\xad\xc4\x8d",
                "st%C3%A9%C5%BE%C3%AD%C4%8D", TRUE );
    /* Unsupported values are never sent raw: with the system source forced to
       fail, each one falls back to the exact encoded neutral label. */
    test_system_hostname_fails = TRUE;
    lock_query( "unpaired-surrogate", L"bad\xd800-host", "Unknown device", "Unknown%20device", TRUE );
    lock_query( "control-bytes", L"tab\there\x01ctl", "Unknown device", "Unknown%20device", TRUE );
    lock_query( "delete-byte", L"bad\x7fhost", "Unknown device", "Unknown%20device", TRUE );
    lock_query( "empty-value", L"", "Unknown device", "Unknown%20device", TRUE );
    lock_query( "missing-value", NULL, "Unknown device", "Unknown%20device", TRUE );
    test_system_hostname_fails = FALSE;
    /* 240 ASCII bytes: within the 255-byte bound, far beyond a NetBIOS name. */
    for ( p = oversized; p < oversized + 240; p++ ) *p = ( char )( 'a' + ( p - oversized ) % 26 );
    *p = 0;
    {
        WCHAR wide[241];
        for ( unsigned i = 0; i < 240; i++ ) wide[i] = ( WCHAR )oversized[i];
        wide[240] = 0;
        lock_query( "long-valid", wide, oversized, oversized, TRUE );
    }
    test_system_hostname_fails = TRUE;
    lock_query( "oversized", L"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"
                L"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"
                L"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
                "Unknown device", "Unknown%20device", TRUE );
    test_system_hostname_fails = FALSE;
    /* Deterministic both-sources-failed: the env is unusable and the system
       source is forced to fail, so the exact neutral label is sent encoded. */
    test_system_hostname_fails = TRUE;
    lock_query( "both-sources-failed", L"bad\xd800-host", "Unknown device", "Unknown%20device", TRUE );
    lock_query( "both-sources-failed-missing", NULL, "Unknown device", "Unknown%20device", TRUE );
    test_system_hostname_fails = FALSE;
}
/* Bridge mode: consume the inherited environment exactly as production does,
 * print the decoded label actually placed in the lock query, then exit. The
 * native wrapper and real Wine are exercised by the external harness. */
static void lock_bridge_report( void )
{
    struct XGameSaveProvider *a;
    memset( &remote, 0, sizeof( remote ) );
    a = device( FALSE, "bridge" );
    CHECK( "bridge-lock", cloud_lock( a ) );
    CHECK( "bridge-single-query", remote.lock_queries == 1 && !remote.lock_undecodable );
    printf( "BRIDGE %s\n", remote.lock_decoded[0] );
    close_provider( a );
}
static void lock_acquire_release_409( void )
{
    struct XGameSaveProvider *a;
    printf( "CASE lock acquire, genuine 409 and session release with a host name\n" );
    memset( &remote, 0, sizeof( remote ) );
    SetEnvironmentVariableW( L"NEXUS_DEVICE_HOSTNAME", L"GameRoom-PC" );
    a = device( FALSE, "lock-409" );
    CHECK( "lock-409-acquire", cloud_lock( a ) );
    CHECK( "lock-409-name", remote.lock_queries == 1 && !strcmp( remote.lock_decoded[0], "friendlyName=GameRoom-PC" ) );
    /* Retention applies only to a session-owned lock. */
    a->session_lock = TRUE;
    CHECK( "lock-409-session-retained", cloud_lock( a ) && remote.lock_queries == 1 );
    a->session_lock = FALSE;
    CHECK( "lock-409-release", cloud_unlock( a, FALSE ) == S_OK && !a->locked && !remote.locked );
    CHECK( "lock-409-release-query", remote.releases == 1 && !remote.announced );
    /* Another device holds the lock: the genuine conflict is reported, the
       provider stays unlocked and the sync fails rather than being disabled. */
    remote.locked = TRUE;
    CHECK( "lock-409-conflict", !cloud_lock( a ) && !a->locked );
    CHECK( "lock-409-conflict-name", remote.lock_queries == 2 && !strcmp( remote.lock_decoded[1], "friendlyName=GameRoom-PC" ) );
    CHECK( "lock-409-no-release", remote.releases == 1 );
    remote.locked = FALSE;
    close_provider( a );
    SetEnvironmentVariableW( L"NEXUS_DEVICE_HOSTNAME", NULL );
}
int main( void )
{
    InitializeLogging();
    if ( getenv( "NEXUS_BRIDGE_REPORT" ) )
    {
        INITIALIZE_OPTIONS bridge{};
        bridge.isInlineConfig = TRUE;
        bridge.gameConfig = "<Game><Identity Name='Nexus.SaveFixture' Publisher='CN=Fixture'/></Game>";
        CHECK( "actual-config", nexus::InitializeGameConfiguration( &bridge ) == S_OK );
        WCHAR temp[MAX_PATH];
        char path[1024];
        GetTempPathW( MAX_PATH, temp );
        WideCharToMultiByte( CP_UTF8, 0, temp, -1, path, sizeof( path ), NULL, NULL );
        snprintf( root, sizeof( root ), "%snexus-bridge-%lu-%I64u", path, GetCurrentProcessId(), GetTickCount64() );
        create_directories( root );
        lock_bridge_report();
        printf( "CHECKS %u FAILURES %u\n", checks, failures );
        return failures ? 1 : 0;
    }
    INITIALIZE_OPTIONS options{};
    options.isInlineConfig = TRUE;
    options.gameConfig = "<Game><Identity Name='Nexus.SaveFixture' Publisher='CN=Fixture'/></Game>";
    CHECK( "actual-config", nexus::InitializeGameConfiguration( &options ) == S_OK );
    WCHAR temp[MAX_PATH];
    char path[1024];
    GetTempPathW( MAX_PATH, temp );
    WideCharToMultiByte( CP_UTF8, 0, temp, -1, path, sizeof( path ), NULL, NULL );
    snprintf( root, sizeof( root ), "%snexus-transfer-%lu-%I64u", path, GetCurrentProcessId(), GetTickCount64() );
    create_directories( root );
    SetEnvironmentVariableA( "XODUS_SAVE_SYNC", "auto" );
    for ( unsigned files = 0; files < 2; files++ )
    {
        roundtrip( files );
        for ( unsigned known = 0; known < 2; known++ )
            for ( unsigned metadata = 0; metadata < 2; metadata++ )
                for ( unsigned force = 0; force < 2; force++ )
                    failed_sync( files, known, metadata, force );
        session_announcement( files );
        failed_release( files );
        release_does_not_ack_upload( files );
        updated_during_upload( files );
        failed_close_and_retry( files );
    }
    file_views_roundtrip();
    lock_friendly_names();
    lock_acquire_release_409();
    printf( "CHECKS %u FAILURES %u\n", checks, failures );
    return failures ? 1 : 0;
}
