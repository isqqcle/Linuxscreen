#pragma once

#include <string_view>
#include <string>
#include <vector>

namespace platform::config {

void StartGameStateMonitor();
void StopGameStateMonitor();
std::string GetCurrentGameState();
bool IsGameStateMonitorAvailable();
bool MatchesGameStateCondition(std::string_view configuredState, std::string_view currentState);
bool HasMatchingGameStateCondition(const std::vector<std::string>& configuredStates, std::string_view currentState);
void RemoveMatchingGameStateConditions(std::vector<std::string>& configuredStates, std::string_view gameState);

} // namespace platform::config
