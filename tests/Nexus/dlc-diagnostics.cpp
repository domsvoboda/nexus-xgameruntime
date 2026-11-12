/* SPDX-License-Identifier: MIT
 * Bounded unit fixture for the opt-in DLC diagnostic helper. No account,
 * network, game, package or XML input is used. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../../src/Runtime/DlcDiagnostics.h"

static unsigned checks, failures;
static WCHAR capture_path[MAX_PATH];
static HANDLE capture_file = INVALID_HANDLE_VALUE;
static HANDLE saved_error_handle = INVALID_HANDLE_VALUE;

#define CHECK( name, test )                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if ( !( test ) )                                                                                               \
        {                                                                                                              \
            ++failures;                                                                                                \
            printf( "FAIL %s line=%d\n", name, __LINE__ );                                                             \
        }                                                                                                              \
    } while ( 0 )

static void reset_budget() { nexus_dlc_diagnostic_records = 0; }
static void clear_flag() { SetEnvironmentVariableA( "NEXUS_DLC_DIAGNOSTICS", NULL ); }
static void set_flag( const char *value ) { SetEnvironmentVariableA( "NEXUS_DLC_DIAGNOSTICS", value ); }

static void start_capture()
{
    WCHAR temporary[MAX_PATH];
    GetTempPathW( MAX_PATH, temporary );
    swprintf( capture_path, MAX_PATH, L"%lsdlc-diag-%lu-%lu.log", temporary, GetCurrentProcessId(), GetTickCount() );
    capture_file = CreateFileW( capture_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    CHECK( "capture-open", capture_file != INVALID_HANDLE_VALUE );
    saved_error_handle = GetStdHandle( STD_ERROR_HANDLE );
    SetStdHandle( STD_ERROR_HANDLE, capture_file );
}

static void stop_capture()
{
    FlushFileBuffers( capture_file );
    SetStdHandle( STD_ERROR_HANDLE, saved_error_handle );
    CloseHandle( capture_file );
    capture_file = INVALID_HANDLE_VALUE;
}

static int read_capture( char *buffer, SIZE_T size )
{
    HANDLE file = CreateFileW( capture_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL );
    DWORD read = 0;
    BOOL ok;
    if ( file == INVALID_HANDLE_VALUE ) return -1;
    ok = ReadFile( file, buffer, ( DWORD )( size - 1 ), &read, NULL );
    CloseHandle( file );
    if ( !ok ) return -1;
    buffer[read] = 0;
    return ( int )read;
}

static unsigned lines_in( const char *text )
{
    unsigned lines = 0;
    for ( ; *text; ++text )
        if ( *text == '\n' ) ++lines;
    return lines;
}

static unsigned frames_in( const char *text )
{
    static const char prefix[] = "NEXUS_DLC_DIAGNOSTIC_V1 ";
    unsigned frames = 0;
    while ( *text )
    {
        if ( !strncmp( text, prefix, sizeof( prefix ) - 1 ) ) ++frames;
        text = strchr( text, '\n' );
        if ( !text ) break;
        ++text;
    }
    return frames;
}

static void test_default_off()
{
    char buffer[8192];
    reset_budget();
    clear_flag();
    start_capture();
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
    stop_capture();
    CHECK( "default-off-read", read_capture( buffer, sizeof( buffer ) ) == 0 );
    CHECK( "default-off-empty", !buffer[0] );
}

static void test_strict_flag()
{
    static const char *off[] = { NULL, "", "0", " true", "true", "11", "1 ", "yes" };
    char long_value[300], buffer[8192];
    unsigned i;
    memset( long_value, '1', sizeof( long_value ) - 1 );
    long_value[sizeof( long_value ) - 1] = 0;
    for ( i = 0; i < sizeof( off ) / sizeof( off[0] ); ++i )
    {
        reset_budget();
        set_flag( off[i] );
        start_capture();
        nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
        stop_capture();
        CHECK( "strict-off-read", read_capture( buffer, sizeof( buffer ) ) == 0 );
        CHECK( "strict-off-empty", !buffer[0] );
    }
    reset_budget();
    set_flag( long_value );
    start_capture();
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
    stop_capture();
    CHECK( "strict-long-off", read_capture( buffer, sizeof( buffer ) ) == 0 && !buffer[0] );
    reset_budget();
    set_flag( "1" );
    start_capture();
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
    stop_capture();
    CHECK( "strict-on-read", read_capture( buffer, sizeof( buffer ) ) > 0 );
    CHECK( "strict-on-one-line", lines_in( buffer ) == 1 && frames_in( buffer ) == 1 );
}

static void test_unknown_stage()
{
    char buffer[8192];
    reset_budget();
    set_flag( "1" );
    start_capture();
    nexus_dlc_diagnostic( ( nexus_dlc_stage )0, S_OK, 0 );
    nexus_dlc_diagnostic( ( nexus_dlc_stage )999, S_OK, 0 );
    stop_capture();
    CHECK( "unknown-stage-read", read_capture( buffer, sizeof( buffer ) ) == 0 );
    CHECK( "unknown-stage-empty", !buffer[0] );
}

static void test_count_bounds()
{
    char buffer[8192];
    reset_budget();
    set_flag( "1" );
    start_capture();
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, NEXUS_DLC_DIAGNOSTIC_MAX_COUNT + 1 );
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 0xffffffffu );
    stop_capture();
    CHECK( "count-bound-read", read_capture( buffer, sizeof( buffer ) ) == 0 );
    CHECK( "count-bound-empty", !buffer[0] );
    reset_budget();
    start_capture();
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, NEXUS_DLC_DIAGNOSTIC_MAX_COUNT );
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 0 );
    stop_capture();
    CHECK( "count-edge-lines", read_capture( buffer, sizeof( buffer ) ) > 0 && lines_in( buffer ) == 2 );
}

static void test_schema()
{
    char buffer[8192];
    reset_budget();
    set_flag( "1" );
    start_capture();
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, E_INVALIDARG, 7 );
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_ENUM_3_RETURN, S_OK, 2 );
    stop_capture();
    CHECK( "schema-read", read_capture( buffer, sizeof( buffer ) ) > 0 );
    CHECK( "schema-exact",
           !strcmp( buffer, "NEXUS_DLC_DIAGNOSTIC_V1 layout_destination 80070057 7\n"
                            "NEXUS_DLC_DIAGNOSTIC_V1 enum_3_return 00000000 2\n" ) );
}

static void test_getlasterror()
{
    DWORD observed;
    reset_budget();
    set_flag( "1" );
    start_capture();
    SetLastError( 0x1234 );
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
    observed = GetLastError();
    stop_capture();
    CHECK( "lasterror-success", observed == 0x1234 );
}

static void test_invalid_handle()
{
    HANDLE saved = GetStdHandle( STD_ERROR_HANDLE );
    reset_budget();
    set_flag( "1" );
    SetStdHandle( STD_ERROR_HANDLE, INVALID_HANDLE_VALUE );
    SetLastError( 0x5678 );
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
    CHECK( "invalid-handle-lasterror", GetLastError() == 0x5678 );
    SetStdHandle( STD_ERROR_HANDLE, saved );
}

static void test_failed_write()
{
    WCHAR temporary[MAX_PATH], path[MAX_PATH];
    HANDLE file, saved = GetStdHandle( STD_ERROR_HANDLE );
    GetTempPathW( MAX_PATH, temporary );
    swprintf( path, MAX_PATH, L"%lsdlc-diag-ro-%lu.log", temporary, GetCurrentProcessId() );
    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    CHECK( "failed-write-create", file != INVALID_HANDLE_VALUE );
    if ( file != INVALID_HANDLE_VALUE ) CloseHandle( file );
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    CHECK( "failed-write-open-read", file != INVALID_HANDLE_VALUE );
    if ( file != INVALID_HANDLE_VALUE )
    {
        reset_budget();
        set_flag( "1" );
        SetStdHandle( STD_ERROR_HANDLE, file );
        SetLastError( 0x9abc );
        nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
        CHECK( "failed-write-lasterror", GetLastError() == 0x9abc );
        SetStdHandle( STD_ERROR_HANDLE, saved );
        CloseHandle( file );
    }
    DeleteFileW( path );
}

struct held_lock_context
{
    HANDLE started;
};

static DWORD WINAPI held_lock_worker( void *argument )
{
    held_lock_context *context = static_cast<held_lock_context *>( argument );
    SetEvent( context->started );
    nexus_dlc_diagnostic( NEXUS_DLC_STAGE_LAYOUT_DESTINATION, S_OK, 1 );
    return 0;
}

/* The helper must not wait for the write lock. The main thread owns the lock;
 * the worker signals a start latch, then calls the helper and must return
 * immediately while dropping the frame. If a blocking acquire is present, the
 * timeout releases it and the captured frame fails the test without hanging. */
static void test_held_lock_no_wait()
{
    held_lock_context context = { CreateEventW( NULL, TRUE, FALSE, NULL ) };
    HANDLE thread;
    DWORD started, wait;
    char buffer[8192];
    CHECK( "held-lock-latch", context.started != NULL );
    reset_budget();
    set_flag( "1" );
    start_capture();
    AcquireSRWLockExclusive( &nexus_dlc_diagnostic_lock );
    thread = CreateThread( NULL, 0, held_lock_worker, &context, 0, NULL );
    CHECK( "held-lock-thread", thread != NULL );
    started = context.started ? WaitForSingleObject( context.started, 2000 ) : WAIT_FAILED;
    wait = thread && started == WAIT_OBJECT_0 ? WaitForSingleObject( thread, 2000 ) : WAIT_FAILED;
    ReleaseSRWLockExclusive( &nexus_dlc_diagnostic_lock );
    if ( thread )
    {
        if ( wait == WAIT_TIMEOUT ) WaitForSingleObject( thread, 5000 );
        CloseHandle( thread );
    }
    if ( context.started ) CloseHandle( context.started );
    stop_capture();
    CHECK( "held-lock-started", started == WAIT_OBJECT_0 );
    CHECK( "held-lock-no-wait", wait == WAIT_OBJECT_0 );
    CHECK( "held-lock-no-frame", read_capture( buffer, sizeof( buffer ) ) >= 0 && !buffer[0] );
}

struct worker_context
{
    unsigned calls;
};

static DWORD WINAPI worker( void *argument )
{
    worker_context *context = static_cast<worker_context *>( argument );
    for ( unsigned i = 0; i < context->calls; ++i )
        nexus_dlc_diagnostic( NEXUS_DLC_STAGE_ENUM_MATCHES, S_OK, 1 );
    return 0;
}

/* Sequential saturation: the counter must stop at the cap and never wrap or
 * keep incrementing after it. */
static void test_budget_saturation()
{
    char buffer[65536];
    int read;
    reset_budget();
    set_flag( "1" );
    start_capture();
    for ( unsigned i = 0; i < 1000; ++i )
        nexus_dlc_diagnostic( NEXUS_DLC_STAGE_ENUM_MATCHES, S_OK, 1 );
    stop_capture();
    read = read_capture( buffer, sizeof( buffer ) );
    CHECK( "saturation-read", read >= 0 );
    CHECK( "saturation-budget", nexus_dlc_diagnostic_records == ( LONG )NEXUS_DLC_DIAGNOSTIC_MAX_RECORDS );
    CHECK( "saturation-lines", read >= 0 && lines_in( buffer ) == NEXUS_DLC_DIAGNOSTIC_MAX_RECORDS );
    CHECK( "saturation-frames", read >= 0 && frames_in( buffer ) == NEXUS_DLC_DIAGNOSTIC_MAX_RECORDS );
}

static void test_budget_concurrency()
{
    enum { threads = 16, calls_per_thread = 20 };
    HANDLE handles[threads];
    worker_context context = { calls_per_thread };
    char buffer[65536];
    int read;
    unsigned i;
    reset_budget();
    set_flag( "1" );
    start_capture();
    for ( i = 0; i < threads; ++i ) handles[i] = CreateThread( NULL, 0, worker, &context, 0, NULL );
    WaitForMultipleObjects( threads, handles, TRUE, 30000 );
    for ( i = 0; i < threads; ++i )
        if ( handles[i] ) CloseHandle( handles[i] );
    stop_capture();
    read = read_capture( buffer, sizeof( buffer ) );
    CHECK( "budget-read", read >= 0 );
    /* Contention may drop both reservations and contended writes, so this is a
     * bound and a non-empty check, not an exact-count promise. */
    CHECK( "budget-bounded", read >= 0 && lines_in( buffer ) <= NEXUS_DLC_DIAGNOSTIC_MAX_RECORDS );
    CHECK( "budget-nonzero", read >= 0 && lines_in( buffer ) > 0 );
    CHECK( "budget-whole-frames", read >= 0 && frames_in( buffer ) == lines_in( buffer ) );
    CHECK( "budget-counter-bounded",
           nexus_dlc_diagnostic_records >= 0 &&
               nexus_dlc_diagnostic_records <= ( LONG )NEXUS_DLC_DIAGNOSTIC_MAX_RECORDS );
}

int main()
{
    test_default_off();
    test_strict_flag();
    test_unknown_stage();
    test_count_bounds();
    test_schema();
    test_getlasterror();
    test_invalid_handle();
    test_failed_write();
    test_held_lock_no_wait();
    test_budget_saturation();
    test_budget_concurrency();
    clear_flag();
    SetStdHandle( STD_ERROR_HANDLE, saved_error_handle );
    DeleteFileW( capture_path );
    printf( "CHECKS %u FAILURES %u\n", checks, failures );
    return failures ? 1 : 0;
}
