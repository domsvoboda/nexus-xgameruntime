// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "../../private.h"
#include <string>
#include <vector>

// The game bridge is the launcher or background service that started this
// game. It speaks the invite bridge's NXSI framing on its own loopback port
// and key, so a launcher that only knows invites is never sent other verbs.
namespace nexus::bridge
{
class Transport
{
public:
    virtual ~Transport() = default;
    virtual HRESULT Exchange( const std::vector<std::string> &request, std::vector<std::string> &reply ) noexcept = 0;
};
// Null unless NEXUS_GAME_BRIDGE_PORT and NEXUS_GAME_BRIDGE_KEY are valid.
Transport *Get() noexcept;
}
