/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Observe newly available, layout-validated packages in the same scoped view
 * used by XPackageEnumeratePackages. Installation metadata never grants a
 * license or establishes authenticity. Registration does not replay packages.
 */
struct installed_package_key
{
    char identifier[33];
    XVersion version;
};
struct installed_package_subscription
{
    LONG refs;
    installed_package_subscription *next;
    IXThreadingImpl *threading;
    XTaskQueueHandle queue;
    HANDLE stop;
    XPackageInstalledCallback *callback;
    void *context;
    UINT64 token;
    BOOL removed, running;
    DWORD callback_thread;
    UINT queued, count;
    SRWLOCK callback_lock;
    installed_package_key seen[CONTENT_MAX_PACKAGES];
};
struct installed_package_notification
{
    installed_package_subscription *subscription;
    content_package package;
};
static SRWLOCK installed_packages_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE installed_packages_changed = CONDITION_VARIABLE_INIT;
static installed_package_subscription *installed_package_subscriptions;
static UINT64 installed_package_next_token;

static void installed_package_release( installed_package_subscription *subscription )
{
    if ( InterlockedDecrement( &subscription->refs ) ) return;
    if ( subscription->stop ) CloseHandle( subscription->stop );
    if ( subscription->queue ) subscription->threading->XTaskQueueCloseHandle( subscription->queue );
    subscription->threading->Release();
    free( subscription );
}

/* Registry lock held. A removed subscription stays discoverable while its
 * callback runs, so wait=true after a nonblocking unregister can still wait. */
static BOOL installed_package_retire( installed_package_subscription *subscription )
{
    installed_package_subscription **link;
    for ( link = &installed_package_subscriptions; *link && *link != subscription; link = &( *link )->next )
        ;
    if ( !*link ) return FALSE;
    *link = subscription->next;
    return TRUE;
}

static void CALLBACK installed_package_callback( void *arg, BOOLEAN canceled )
{
    auto *notification = static_cast<installed_package_notification *>( arg );
    auto *subscription = notification->subscription;
    BOOL deliver, retired = FALSE;
    AcquireSRWLockExclusive( &subscription->callback_lock );
    AcquireSRWLockExclusive( &installed_packages_lock );
    --subscription->queued;
    deliver = !canceled && !subscription->removed;
    if ( deliver )
    {
        subscription->running = TRUE;
        subscription->callback_thread = GetCurrentThreadId();
    }
    ReleaseSRWLockExclusive( &installed_packages_lock );
    if ( deliver )
    {
        XPackageDetails details{};
        const auto &package = notification->package;
        details.packageIdentifier = package.identifier;
        details.version = package.version;
        details.kind = XPackageKind::Content;
        details.displayName = package.display;
        details.description = package.description;
        details.publisher = package.publisher;
        details.storeId = package.store;
        details.titleID = "";
        details.count = 1;
        subscription->callback( subscription->context, &details );
    }
    AcquireSRWLockExclusive( &installed_packages_lock );
    subscription->running = FALSE;
    if ( subscription->removed ) retired = installed_package_retire( subscription );
    WakeAllConditionVariable( &installed_packages_changed );
    ReleaseSRWLockExclusive( &installed_packages_lock );
    ReleaseSRWLockExclusive( &subscription->callback_lock );
    if ( retired ) installed_package_release( subscription );
    free( notification );
    installed_package_release( subscription );
}

static HRESULT installed_package_submit( installed_package_subscription *subscription, const content_package *package )
{
    auto *notification = static_cast<installed_package_notification *>( malloc( sizeof( installed_package_notification ) ) );
    if ( !notification ) return E_OUTOFMEMORY;
    notification->subscription = subscription;
    notification->package = *package;
    AcquireSRWLockExclusive( &installed_packages_lock );
    if ( subscription->removed || subscription->queued >= CONTENT_MAX_PACKAGES )
    {
        ReleaseSRWLockExclusive( &installed_packages_lock );
        free( notification );
        return E_ABORT;
    }
    ++subscription->queued;
    InterlockedIncrement( &subscription->refs );
    ReleaseSRWLockExclusive( &installed_packages_lock );
    HRESULT hr = subscription->threading->XTaskQueueSubmitCallback( subscription->queue, XTaskQueuePort::Completion,
                                                                   notification, installed_package_callback );
    if ( FAILED( hr ) )
    {
        AcquireSRWLockExclusive( &installed_packages_lock );
        --subscription->queued;
        ReleaseSRWLockExclusive( &installed_packages_lock );
        free( notification );
        installed_package_release( subscription );
    }
    return hr;
}

static HRESULT installed_package_refresh( installed_package_subscription *subscription, BOOL baseline )
{
    auto *snapshot = static_cast<content_snapshot *>( calloc( 1, sizeof( content_snapshot ) ) );
    if ( !snapshot ) return E_OUTOFMEMORY;
    snapshot->current = GetContentConfiguration();
    if ( !content_id( snapshot->current.store ) || !snapshot->current.identity[0] )
    {
        free( snapshot );
        return E_GAME_MISSING_GAME_CONFIG;
    }
    HRESULT hr = content_discover( snapshot );
    if ( SUCCEEDED( hr ) )
    {
        installed_package_key next[CONTENT_MAX_PACKAGES]{};
        UINT count = 0;
        for ( UINT i = 0; i < snapshot->count; ++i )
        {
            const auto *package = &snapshot->packages[i];
            if ( !content_matches( snapshot, package, XPackageEnumerationScope::ThisAndRelated ) ) continue;
            BOOL seen = baseline;
            for ( UINT j = 0; !seen && j < subscription->count; ++j )
                seen = !strcmp( subscription->seen[j].identifier, package->identifier ) &&
                       !memcmp( &subscription->seen[j].version, &package->version, sizeof( package->version ) );
            if ( seen || SUCCEEDED( installed_package_submit( subscription, package ) ) )
            {
                strcpy( next[count].identifier, package->identifier );
                next[count++].version = package->version;
            }
        }
        memcpy( subscription->seen, next, count * sizeof( *next ) );
        subscription->count = count;
    }
    /* A failed scan is not an uninstall. Preserve the last complete snapshot. */
    free( snapshot );
    return hr;
}

static DWORD WINAPI installed_package_worker( void *arg )
{
    auto *subscription = static_cast<installed_package_subscription *>( arg );
    while ( WaitForSingleObject( subscription->stop, 1000 ) == WAIT_TIMEOUT )
        installed_package_refresh( subscription, FALSE );
    installed_package_release( subscription );
    return 0;
}

static HRESULT installed_package_register( XTaskQueueHandle queue, void *context, XPackageInstalledCallback *callback,
                                           XTaskQueueRegistrationToken *token )
{
    if ( !token ) return E_INVALIDARG;
    token->token = 0;
    if ( !callback ) return E_INVALIDARG;
    IXThreadingImpl *threading = nullptr;
    HRESULT hr = QueryApiImpl( CLSID_XThreadingImpl, IID_IXThreadingImpl, reinterpret_cast<void **>( &threading ) );
    if ( FAILED( hr ) ) return hr;
    auto *subscription = static_cast<installed_package_subscription *>( calloc( 1, sizeof( installed_package_subscription ) ) );
    if ( !subscription ) { threading->Release(); return E_OUTOFMEMORY; }
    subscription->refs = 1;
    subscription->threading = threading;
    subscription->callback = callback;
    subscription->context = context;
    hr = queue ? threading->XTaskQueueDuplicateHandle( queue, &subscription->queue )
               : threading->XTaskQueueGetCurrentProcessTaskQueue( &subscription->queue ) ? S_OK : E_FAIL;
    if ( SUCCEEDED( hr ) ) hr = installed_package_refresh( subscription, TRUE );
    if ( SUCCEEDED( hr ) && !( subscription->stop = CreateEventW( NULL, TRUE, FALSE, NULL ) ) )
        hr = HRESULT_FROM_WIN32( GetLastError() );
    if ( FAILED( hr ) ) { installed_package_release( subscription ); return hr; }
    AcquireSRWLockExclusive( &installed_packages_lock );
    UINT count = 0;
    for ( auto *it = installed_package_subscriptions; it; it = it->next ) ++count;
    if ( count >= 64 )
    {
        ReleaseSRWLockExclusive( &installed_packages_lock );
        installed_package_release( subscription );
        return E_OUTOFMEMORY;
    }
    InterlockedIncrement( &subscription->refs ); /* worker */
    HANDLE thread = CreateThread( NULL, 0, installed_package_worker, subscription, 0, NULL );
    if ( !thread )
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        ReleaseSRWLockExclusive( &installed_packages_lock );
        installed_package_release( subscription );
        installed_package_release( subscription );
        return hr;
    }
    CloseHandle( thread );
    if ( !++installed_package_next_token ) ++installed_package_next_token;
    token->token = subscription->token = installed_package_next_token;
    subscription->next = installed_package_subscriptions;
    installed_package_subscriptions = subscription;
    ReleaseSRWLockExclusive( &installed_packages_lock );
    return S_OK;
}

static BOOLEAN installed_package_unregister( XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    AcquireSRWLockExclusive( &installed_packages_lock );
    auto *subscription = installed_package_subscriptions;
    while ( subscription && subscription->token != token.token ) subscription = subscription->next;
    if ( !subscription ) { ReleaseSRWLockExclusive( &installed_packages_lock ); return TRUE; }
    InterlockedIncrement( &subscription->refs );
    subscription->removed = TRUE;
    SetEvent( subscription->stop );
    while ( wait && subscription->running && subscription->callback_thread != GetCurrentThreadId() )
        SleepConditionVariableSRW( &installed_packages_changed, &installed_packages_lock, INFINITE, 0 );
    BOOL pending = subscription->running;
    BOOL retired = !pending && installed_package_retire( subscription );
    ReleaseSRWLockExclusive( &installed_packages_lock );
    if ( retired ) installed_package_release( subscription );
    installed_package_release( subscription );
    return !pending;
}
