// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include "../../src/System/Threading/QueueHandles.h"

TEST(InlineQueue, OnlyQueuesWithBothPortsImmediateQualify)
{
    struct Case { XTaskQueueDispatchMode work, completion; bool expected; };
    const Case cases[] = {
        {XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Immediate, true},
        {XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, false},
        {XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Manual, false},
        {XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Immediate, false},
        {XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::Immediate, false},
        {XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::SerializedThreadPool, false},
    };
    EXPECT_FALSE(nexus::queue_dispatches_inline(nullptr));
    for (const auto &c : cases)
    {
        XTaskQueueHandle queue = nullptr;
        ASSERT_EQ(XTaskQueueCreate(c.work, c.completion, &queue), S_OK);
        EXPECT_EQ(nexus::queue_dispatches_inline(queue), c.expected);
        XTaskQueueCloseHandle(queue);
    }
}

TEST(InlineQueue, CompositeQueueRequiresBothUnderlyingPortsToBeImmediate)
{
    XTaskQueueHandle inline_queue = nullptr, manual_queue = nullptr, composite = nullptr;
    ASSERT_EQ(XTaskQueueCreate(XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Immediate, &inline_queue), S_OK);
    ASSERT_EQ(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &manual_queue), S_OK);
    XTaskQueuePortHandle work = nullptr, completion = nullptr;
    ASSERT_EQ(XTaskQueueGetPort(inline_queue, XTaskQueuePort::Work, &work), S_OK);
    ASSERT_EQ(XTaskQueueGetPort(manual_queue, XTaskQueuePort::Completion, &completion), S_OK);
    ASSERT_EQ(XTaskQueueCreateComposite(work, completion, &composite), S_OK);
    EXPECT_FALSE(nexus::queue_dispatches_inline(composite));
    XTaskQueueCloseHandle(composite);
    ASSERT_EQ(XTaskQueueGetPort(inline_queue, XTaskQueuePort::Completion, &completion), S_OK);
    ASSERT_EQ(XTaskQueueCreateComposite(work, completion, &composite), S_OK);
    EXPECT_TRUE(nexus::queue_dispatches_inline(composite));
    XTaskQueueCloseHandle(composite);
    XTaskQueueCloseHandle(manual_queue);
    XTaskQueueCloseHandle(inline_queue);
}
