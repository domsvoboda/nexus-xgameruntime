/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Opt-in, function-only DLC diagnostic stream for native package/store
 * boundaries. Default OFF: only NEXUS_DLC_DIAGNOSTICS with the exact value "1"
 * enables it. The only accepted inputs are an allowlisted stage enum, an
 * HRESULT and a bounded count. No paths, identifiers, tokens, certificates,
 * URLs, receipts, account data or arbitrary text are accepted. Output goes to
 * stderr and is capped to a small fixed number of fixed-format records per
 * process.
 *
 * This is deliberately not a general logger and must never change an API
 * result, output buffer, callback, cancellation or rights decision. */
#ifndef NEXUS_DLC_DIAGNOSTICS_H
#define NEXUS_DLC_DIAGNOSTICS_H

#include <windows.h>
#include <stdio.h>

#define NEXUS_DLC_DIAGNOSTIC_MAX_RECORDS 256u
#define NEXUS_DLC_DIAGNOSTIC_MAX_COUNT 4096u

enum nexus_dlc_stage
{
    NEXUS_DLC_STAGE_LAYOUT_INVALID = 1,
    NEXUS_DLC_STAGE_LAYOUT_DESTINATION,
    NEXUS_DLC_STAGE_LAYOUT_SOURCE,
    NEXUS_DLC_STAGE_LAYOUT_NONE,
    NEXUS_DLC_STAGE_LOAD_ACCEPTED,
    NEXUS_DLC_STAGE_DISCOVER_ACCEPTED,
    NEXUS_DLC_STAGE_DISCOVER_REJECTED,
    NEXUS_DLC_STAGE_ENUM_LEGACY_ENTRY,
    NEXUS_DLC_STAGE_ENUM_LEGACY_RETURN,
    NEXUS_DLC_STAGE_ENUM_CURRENT_ENTRY,
    NEXUS_DLC_STAGE_ENUM_CURRENT_RETURN,
    NEXUS_DLC_STAGE_ENUM_2_ENTRY,
    NEXUS_DLC_STAGE_ENUM_2_RETURN,
    NEXUS_DLC_STAGE_ENUM_3_ENTRY,
    NEXUS_DLC_STAGE_ENUM_3_RETURN,
    NEXUS_DLC_STAGE_ENUM_MATCHES,
    NEXUS_DLC_STAGE_MOUNT_LEGACY_ENTRY,
    NEXUS_DLC_STAGE_MOUNT_LEGACY_RETURN,
    NEXUS_DLC_STAGE_MOUNT_UI_ASYNC_ENTRY,
    NEXUS_DLC_STAGE_MOUNT_UI_ASYNC_RETURN,
    NEXUS_DLC_STAGE_MOUNT_UI_RESULT_ENTRY,
    NEXUS_DLC_STAGE_MOUNT_UI_RESULT_RETURN,
    NEXUS_DLC_STAGE_MOUNT_PLAN_GROUPS,
    NEXUS_DLC_STAGE_MOUNT_PLAN_FILES,
    NEXUS_DLC_STAGE_ADDON_BEGIN,
    NEXUS_DLC_STAGE_ADDON_COUNT,
    NEXUS_DLC_STAGE_ADDON_RESULT,
    NEXUS_DLC_STAGE_ACQUIRE_BEGIN,
    NEXUS_DLC_STAGE_ACQUIRE_RESULT,
    NEXUS_DLC_STAGE_CAN_ACQUIRE_STORE_BEGIN,
    NEXUS_DLC_STAGE_CAN_ACQUIRE_STORE_RESULT,
    NEXUS_DLC_STAGE_CAN_ACQUIRE_PACKAGE_BEGIN,
    NEXUS_DLC_STAGE_CAN_ACQUIRE_PACKAGE_RESULT,
    NEXUS_DLC_STAGE_ASSOCIATED_BEGIN,
    NEXUS_DLC_STAGE_ASSOCIATED_RESULT,
};

/* The stage dictionary is a fixed allowlist of ASCII literals. Unknown values
 * are never formatted. */
inline const char *nexus_dlc_stage_name( nexus_dlc_stage stage )
{
    switch ( stage )
    {
    case NEXUS_DLC_STAGE_LAYOUT_INVALID: return "layout_invalid";
    case NEXUS_DLC_STAGE_LAYOUT_DESTINATION: return "layout_destination";
    case NEXUS_DLC_STAGE_LAYOUT_SOURCE: return "layout_source";
    case NEXUS_DLC_STAGE_LAYOUT_NONE: return "layout_none";
    case NEXUS_DLC_STAGE_LOAD_ACCEPTED: return "load_accepted";
    case NEXUS_DLC_STAGE_DISCOVER_ACCEPTED: return "discover_accepted";
    case NEXUS_DLC_STAGE_DISCOVER_REJECTED: return "discover_rejected";
    case NEXUS_DLC_STAGE_ENUM_LEGACY_ENTRY: return "enum_legacy_entry";
    case NEXUS_DLC_STAGE_ENUM_LEGACY_RETURN: return "enum_legacy_return";
    case NEXUS_DLC_STAGE_ENUM_CURRENT_ENTRY: return "enum_current_entry";
    case NEXUS_DLC_STAGE_ENUM_CURRENT_RETURN: return "enum_current_return";
    case NEXUS_DLC_STAGE_ENUM_2_ENTRY: return "enum_2_entry";
    case NEXUS_DLC_STAGE_ENUM_2_RETURN: return "enum_2_return";
    case NEXUS_DLC_STAGE_ENUM_3_ENTRY: return "enum_3_entry";
    case NEXUS_DLC_STAGE_ENUM_3_RETURN: return "enum_3_return";
    case NEXUS_DLC_STAGE_ENUM_MATCHES: return "enum_matches";
    case NEXUS_DLC_STAGE_MOUNT_LEGACY_ENTRY: return "mount_legacy_entry";
    case NEXUS_DLC_STAGE_MOUNT_LEGACY_RETURN: return "mount_legacy_return";
    case NEXUS_DLC_STAGE_MOUNT_UI_ASYNC_ENTRY: return "mount_ui_async_entry";
    case NEXUS_DLC_STAGE_MOUNT_UI_ASYNC_RETURN: return "mount_ui_async_return";
    case NEXUS_DLC_STAGE_MOUNT_UI_RESULT_ENTRY: return "mount_ui_result_entry";
    case NEXUS_DLC_STAGE_MOUNT_UI_RESULT_RETURN: return "mount_ui_result_return";
    case NEXUS_DLC_STAGE_MOUNT_PLAN_GROUPS: return "mount_plan_groups";
    case NEXUS_DLC_STAGE_MOUNT_PLAN_FILES: return "mount_plan_files";
    case NEXUS_DLC_STAGE_ADDON_BEGIN: return "addon_begin";
    case NEXUS_DLC_STAGE_ADDON_COUNT: return "addon_count";
    case NEXUS_DLC_STAGE_ADDON_RESULT: return "addon_result";
    case NEXUS_DLC_STAGE_ACQUIRE_BEGIN: return "acquire_begin";
    case NEXUS_DLC_STAGE_ACQUIRE_RESULT: return "acquire_result";
    case NEXUS_DLC_STAGE_CAN_ACQUIRE_STORE_BEGIN: return "can_acquire_store_begin";
    case NEXUS_DLC_STAGE_CAN_ACQUIRE_STORE_RESULT: return "can_acquire_store_result";
    case NEXUS_DLC_STAGE_CAN_ACQUIRE_PACKAGE_BEGIN: return "can_acquire_package_begin";
    case NEXUS_DLC_STAGE_CAN_ACQUIRE_PACKAGE_RESULT: return "can_acquire_package_result";
    case NEXUS_DLC_STAGE_ASSOCIATED_BEGIN: return "associated_begin";
    case NEXUS_DLC_STAGE_ASSOCIATED_RESULT: return "associated_result";
    default: return nullptr;
    }
}

/* One budget and one write lock per linked image. These inline entities are
 * shared by every translation unit that includes this header in the same
 * DLL/exe; separate DLL copies each have their own budget. No global named
 * mutex, ABI or cross-image singleton is introduced. The budget counts
 * emission attempts: a contended or failed write consumes a reservation
 * without emitting a frame. */
inline volatile LONG nexus_dlc_diagnostic_records = 0;
inline SRWLOCK nexus_dlc_diagnostic_lock = SRWLOCK_INIT;

/* Best-effort saturating reservation. It never increments at or above the cap
 * and never retries or spins: a caller that loses the CAS race drops the
 * record. This keeps the counter bounded at MAX_RECORDS for the life of the
 * image. */
inline BOOL nexus_dlc_diagnostic_reserve()
{
    LONG current = InterlockedCompareExchange( &nexus_dlc_diagnostic_records, 0, 0 );
    if ( current < 0 || current >= ( LONG )NEXUS_DLC_DIAGNOSTIC_MAX_RECORDS ) return FALSE;
    return InterlockedCompareExchange( &nexus_dlc_diagnostic_records, current + 1, current ) == current;
}

/* Strict opt-in: only a one-character value "1" enables the stream. Missing,
 * empty, "0", "true", "11", truncated and long values all stay OFF. Preserves
 * GetLastError. */
inline BOOL nexus_dlc_diagnostic_enabled()
{
    DWORD saved = GetLastError();
    char value[2] = { 0, 0 };
    BOOL enabled = GetEnvironmentVariableA( "NEXUS_DLC_DIAGNOSTICS", value, sizeof( value ) ) == 1 &&
                   value[0] == '1';
    SetLastError( saved );
    return enabled;
}

/* Emit one fixed record:
 *   NEXUS_DLC_DIAGNOSTIC_V1 <static-stage> <8hex-HRESULT> <count>\n
 * A diagnostic failure is silent and cannot affect the caller's API result,
 * output buffers, callbacks, cancellation or rights. GetLastError is preserved
 * on every path. No heap, file creation, network or global setting is used.
 * The write lock is best-effort and non-blocking: a contended frame is dropped
 * instead of making a game API thread wait. WriteFile still targets the
 * process's existing stderr handle, so an explicit diagnostic run must have an
 * actively drained stderr sink; this helper does not claim universally
 * non-blocking I/O. */
inline void nexus_dlc_diagnostic( nexus_dlc_stage stage, HRESULT hr, UINT32 count )
{
    DWORD saved = GetLastError();
    const char *name;
    char line[128];
    int length;
    HANDLE error_handle;
    if ( !nexus_dlc_diagnostic_enabled() ) goto done;
    if ( !( name = nexus_dlc_stage_name( stage ) ) ) goto done;
    if ( count > NEXUS_DLC_DIAGNOSTIC_MAX_COUNT ) goto done;
    if ( !nexus_dlc_diagnostic_reserve() ) goto done;
    length = snprintf( line, sizeof( line ), "NEXUS_DLC_DIAGNOSTIC_V1 %s %08lX %lu\n", name,
                       ( unsigned long )( UINT32 )hr, ( unsigned long )count );
    if ( length <= 0 || ( SIZE_T )length >= sizeof( line ) ) goto done;
    error_handle = GetStdHandle( STD_ERROR_HANDLE );
    if ( !error_handle || error_handle == INVALID_HANDLE_VALUE ) goto done;
    if ( !TryAcquireSRWLockExclusive( &nexus_dlc_diagnostic_lock ) ) goto done;
    {
        DWORD written = 0;
        WriteFile( error_handle, line, ( DWORD )length, &written, NULL );
    }
    ReleaseSRWLockExclusive( &nexus_dlc_diagnostic_lock );
done:
    SetLastError( saved );
}

#endif
