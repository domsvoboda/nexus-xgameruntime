// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "XTaskQueue.h"
namespace nexus
{
HRESULT register_queue_object( TaskQueueImpl *queue ) noexcept;
void unregister_queue_object( TaskQueueImpl *queue ) noexcept;
referenced_ptr<ITaskQueue> acquire_queue( XTaskQueueHandle handle ) noexcept;
HRESULT create_queue_handle( ITaskQueue *queue, XTaskQueueHandle *out ) noexcept;
HRESULT reference_queue_handle( ITaskQueue *queue, XTaskQueueHandle *out ) noexcept;
void close_queue_handle( XTaskQueueHandle handle ) noexcept;
bool queue_dispatches_inline( XTaskQueueHandle handle ) noexcept;
}
