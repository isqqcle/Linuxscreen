float GetOverlayAnimationDeltaTime() {
    const float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.0f && dt < 0.2f) {
        return dt;
    }
    return 1.0f / 60.0f;
}

void SubmitCursorBoundaryItem() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window || window->SkipItems) {
        return;
    }

    const ImVec2 cursor = window->DC.CursorPos;
    const ImRect bb(cursor, cursor);
    ImGui::ItemAdd(bb, 0);
}

void GarbageCollectButtonRippleStates() {
    if ((g_imguiOverlayState.frameCount % 300) != 0) {
        return;
    }

    constexpr std::uint64_t kStaleFrameThreshold = 600;
    auto garbageCollectMap = [&](std::unordered_map<ImGuiID, ButtonRippleState>& stateMap) {
        for (auto it = stateMap.begin(); it != stateMap.end();) {
            const bool stale = !it->second.active &&
                               (g_imguiOverlayState.frameCount > it->second.lastTouchedFrame) &&
                               ((g_imguiOverlayState.frameCount - it->second.lastTouchedFrame) > kStaleFrameThreshold);
            if (stale) {
                it = stateMap.erase(it);
            } else {
                ++it;
            }
        }
    };

    garbageCollectMap(g_buttonRippleStates);
    garbageCollectMap(g_headerRippleStates);
    garbageCollectMap(g_rebindKeyCyclePulseStates);

    for (auto it = g_headerContentRevealStates.begin(); it != g_headerContentRevealStates.end();) {
        const bool stale = (g_imguiOverlayState.frameCount > it->second.lastTouchedFrame) &&
                           ((g_imguiOverlayState.frameCount - it->second.lastTouchedFrame) > kStaleFrameThreshold);
        if (stale) {
            it = g_headerContentRevealStates.erase(it);
        } else {
            ++it;
        }
    }
}

void FinalizeButtonRippleStatesForFrame() {
    for (auto& entry : g_buttonRippleStates) {
        ButtonRippleState& ripple = entry.second;
        if (ripple.active && ripple.lastTouchedFrame != g_imguiOverlayState.frameCount) {
            ripple.active = false;
        }
    }
}

void TriggerRebindKeyCyclePulse(ImGuiID itemId) {
    if (itemId == 0) {
        return;
    }

    ButtonRippleState& pulse = g_rebindKeyCyclePulseStates[itemId];
    pulse.active = true;
    pulse.elapsedSeconds = 0.0f;
    pulse.lastTouchedFrame = g_imguiOverlayState.frameCount;
}

void DrawRebindKeyButtonEffects(ImGuiID itemId) {
    if (itemId == 0) {
        return;
    }

    const float dt = GetOverlayAnimationDeltaTime();
    const float styleAlpha = ImGui::GetStyle().Alpha;
    const ImVec2 rectMin = ImGui::GetItemRectMin();
    const ImVec2 rectMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    auto pulseIt = g_rebindKeyCyclePulseStates.find(itemId);
    if (pulseIt == g_rebindKeyCyclePulseStates.end()) {
        return;
    }

    ButtonRippleState& pulse = pulseIt->second;
    pulse.lastTouchedFrame = g_imguiOverlayState.frameCount;
    if (!pulse.active) {
        return;
    }

    constexpr float kPulseDurationSeconds = 0.18f;
    pulse.elapsedSeconds += dt;
    const float progress = std::min(1.0f, pulse.elapsedSeconds / kPulseDurationSeconds);
    const float eased = iam_eval_preset(iam_ease_out_cubic, progress);
    const float expand = eased * 2.5f;
    const float thickness = 2.2f * (1.0f - progress);

    ImVec4 pulseColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    pulseColor.w = styleAlpha * (0.62f * (1.0f - progress));
    if (pulseColor.w > 0.001f) {
        drawList->AddRect(ImVec2(rectMin.x - expand, rectMin.y - expand),
                          ImVec2(rectMax.x + expand, rectMax.y + expand),
                          ImGui::ColorConvertFloat4ToU32(pulseColor),
                          ImGui::GetStyle().FrameRounding + expand,
                          0,
                          std::max(1.0f, thickness));
    }

    if (progress >= 1.0f) {
        pulse.active = false;
    }
}

bool AnimatedButton(const char* label, const ImVec2& size) {
    const bool clicked = ImGui::Button(label, size);
    const ImGuiID itemId = ImGui::GetItemID();
    if (itemId == 0) {
        return clicked;
    }

    const ImVec2 rectMin = ImGui::GetItemRectMin();
    const ImVec2 rectMax = ImGui::GetItemRectMax();
    const ImVec2 rectCenter = ImVec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
    const float dt = GetOverlayAnimationDeltaTime();

    ButtonRippleState& ripple = g_buttonRippleStates[itemId];
    ripple.lastTouchedFrame = g_imguiOverlayState.frameCount;

    if (clicked) {
        ripple.active = true;
        ripple.elapsedSeconds = 0.0f;
        ripple.origin = ImGui::GetIO().MousePos;
        if (ripple.origin.x < rectMin.x || ripple.origin.x > rectMax.x ||
            ripple.origin.y < rectMin.y || ripple.origin.y > rectMax.y) {
            ripple.origin = rectCenter;
        }
    }

    const float styleAlpha = ImGui::GetStyle().Alpha;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (ripple.active) {
        constexpr float kRippleDurationSeconds = 0.55f;
        ripple.elapsedSeconds += dt;
        const float progress = std::min(1.0f, ripple.elapsedSeconds / kRippleDurationSeconds);
        const float eased = iam_eval_preset(iam_ease_out_cubic, progress);

        float maxDistance = 0.0f;
        const ImVec2 corners[] = {
            rectMin,
            ImVec2(rectMax.x, rectMin.y),
            rectMax,
            ImVec2(rectMin.x, rectMax.y),
        };
        for (const ImVec2& corner : corners) {
            const float dx = corner.x - ripple.origin.x;
            const float dy = corner.y - ripple.origin.y;
            maxDistance = std::max(maxDistance, std::sqrt(dx * dx + dy * dy));
        }

        const float radius = maxDistance * eased;
        const float alpha = styleAlpha * (0.22f * (1.0f - progress));
        if (alpha > 0.001f) {
            drawList->PushClipRect(rectMin, rectMax, true);
            drawList->AddCircleFilled(ripple.origin, radius, IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
            drawList->PopClipRect();
        }

        if (progress >= 1.0f) {
            ripple.active = false;
        }
    }

    return clicked;
}

bool AnimatedCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags) {
    const bool open = ImGui::CollapsingHeader(label, flags);
    const ImGuiID itemId = ImGui::GetItemID();
    if (itemId == 0) {
        return open;
    }

    const float dt = GetOverlayAnimationDeltaTime();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const float styleAlpha = ImGui::GetStyle().Alpha;
    const ImVec2 mousePos = ImGui::GetIO().MousePos;

    const ImVec2 rectMin = ImGui::GetItemRectMin();
    const ImVec2 rectMax = ImGui::GetItemRectMax();
    const ImVec2 rectCenter = ImVec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (clicked) {
        ButtonRippleState& ripple = g_headerRippleStates[itemId];
        ripple.active = true;
        ripple.elapsedSeconds = 0.0f;
        ripple.origin = mousePos;
        if (ripple.origin.x < rectMin.x || ripple.origin.x > rectMax.x ||
            ripple.origin.y < rectMin.y || ripple.origin.y > rectMax.y) {
            ripple.origin = rectCenter;
        }
        ripple.lastTouchedFrame = g_imguiOverlayState.frameCount;
    }
    HeaderContentRevealState& revealState = g_headerContentRevealStates[itemId];
    revealState.lastTouchedFrame = g_imguiOverlayState.frameCount;
    if (!revealState.initialized) {
        revealState.initialized = true;
        revealState.previousOpen = open;
        revealState.progress = open ? 1.0f : 0.0f;
    }
    constexpr float kRevealOpenDurationSeconds = 0.14f;
    constexpr float kRevealCloseDurationSeconds = 0.08f;
    const float targetProgress = open ? 1.0f : 0.0f;
    const float step = dt / (open ? kRevealOpenDurationSeconds : kRevealCloseDurationSeconds);
    if (revealState.progress < targetProgress) {
        revealState.progress = std::min(targetProgress, revealState.progress + step);
    } else if (revealState.progress > targetProgress) {
        revealState.progress = std::max(targetProgress, revealState.progress - step);
    }
    revealState.previousOpen = open;
    const bool contentVisible = open || revealState.progress > 0.001f;
    const float contentRevealProgress = contentVisible ? revealState.progress : 0.0f;
    auto rippleIt = g_headerRippleStates.find(itemId);
    if (rippleIt != g_headerRippleStates.end()) {
        ButtonRippleState& ripple = rippleIt->second;
        ripple.lastTouchedFrame = g_imguiOverlayState.frameCount;
        if (ripple.active) {
            constexpr float kHeaderRippleDurationSeconds = 0.42f;
            ripple.elapsedSeconds += dt;
            const float progress = std::min(1.0f, ripple.elapsedSeconds / kHeaderRippleDurationSeconds);
            const float eased = iam_eval_preset(iam_ease_out_cubic, progress);

            float maxDistance = 0.0f;
            const ImVec2 corners[] = {
                rectMin,
                ImVec2(rectMax.x, rectMin.y),
                rectMax,
                ImVec2(rectMin.x, rectMax.y),
            };
            for (const ImVec2& corner : corners) {
                const float dx = corner.x - ripple.origin.x;
                const float dy = corner.y - ripple.origin.y;
                maxDistance = std::max(maxDistance, std::sqrt(dx * dx + dy * dy));
            }

            const float radius = maxDistance * eased;
            const float alpha = styleAlpha * (0.18f * (1.0f - progress));
            if (alpha > 0.001f) {
                ImVec4 rippleColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                drawList->PushClipRect(rectMin, rectMax, true);
                drawList->AddCircleFilled(ripple.origin,
                                          radius,
                                          ImGui::ColorConvertFloat4ToU32(ImVec4(rippleColor.x, rippleColor.y, rippleColor.z, alpha)));
                drawList->PopClipRect();
            }

            if (progress >= 1.0f) {
                ripple.active = false;
            }
        }
    }

    g_pendingHeaderRevealProgress = contentRevealProgress;
    g_pendingHeaderRevealValid = contentVisible;
    g_pendingHeaderRevealClosing = !open;
    g_pendingHeaderRevealId = itemId;

    return contentVisible;
}

HeaderRevealScope BeginAnimatedHeaderContentReveal() {
    if (!g_pendingHeaderRevealValid) {
        g_pendingHeaderRevealClosing = false;
        g_pendingHeaderRevealId = 0;
        return HeaderRevealScope{};
    }

    g_pendingHeaderRevealValid = false;
    const float t = std::clamp(g_pendingHeaderRevealProgress, 0.0f, 1.0f);
    const bool closing = g_pendingHeaderRevealClosing;
    const ImGuiID headerId = g_pendingHeaderRevealId;
    g_pendingHeaderRevealClosing = false;
    g_pendingHeaderRevealId = 0;

    const float yOffset = (1.0f - t) * 9.0f;
    float alphaScale = t;
    if (!closing) {
        const float eased = iam_eval_preset(iam_ease_out_cubic, t);
        alphaScale = 0.45f + (0.55f * eased);
    }
    alphaScale = std::clamp(alphaScale, 0.0f, 1.0f);
    return HeaderRevealScope(yOffset, alphaScale, headerId, t, closing);
}

void StartThemeTransitionToStyle(const ImGuiStyle& targetStyle) {
    static std::uint32_t s_themeTransitionStyleSerial = 0;

    if (g_overlayUiAnimationState.themeTransitionTargetStyleId != 0) {
        iam_style_unregister(g_overlayUiAnimationState.themeTransitionTargetStyleId);
        g_overlayUiAnimationState.themeTransitionTargetStyleId = 0;
    }

    const ImGuiID targetStyleId = ImHashData(&s_themeTransitionStyleSerial,
                                             sizeof(s_themeTransitionStyleSerial),
                                             static_cast<ImGuiID>(0x7A1D5B3Cu));
    ++s_themeTransitionStyleSerial;

    ImGuiStyle adjustedTargetStyle = targetStyle;
    auto config = platform::config::GetConfigSnapshot();
    if (config) {
        adjustedTargetStyle.Alpha = config->guiOpacity;
    }
    iam_style_register(targetStyleId, adjustedTargetStyle);

    g_overlayUiAnimationState.themeTransitionTargetStyleId = targetStyleId;
    g_overlayUiAnimationState.themeTransitionElapsedSeconds = 0.0f;
    g_overlayUiAnimationState.themeTransitionActive = true;
}

void UpdateThemeTransitionForFrame() {
    if (!g_overlayUiAnimationState.themeTransitionActive ||
        g_overlayUiAnimationState.themeTransitionTargetStyleId == 0) {
        return;
    }

    const float dt = GetOverlayAnimationDeltaTime();
    g_overlayUiAnimationState.themeTransitionElapsedSeconds += dt;

    constexpr ImGuiID kThemeTweenId = static_cast<ImGuiID>(0x6C03E251u);
    iam_style_tween(kThemeTweenId,
                    g_overlayUiAnimationState.themeTransitionTargetStyleId,
                    g_overlayUiAnimationState.themeTransitionDurationSeconds,
                    iam_ease_preset(iam_ease_out_cubic),
                    iam_col_oklab,
                    dt);

    if (g_overlayUiAnimationState.themeTransitionElapsedSeconds >=
        g_overlayUiAnimationState.themeTransitionDurationSeconds) {
        // Snap exactly to the target style at transition end.
        iam_style_blend(g_overlayUiAnimationState.themeTransitionTargetStyleId,
                        g_overlayUiAnimationState.themeTransitionTargetStyleId,
                        1.0f,
                        iam_col_oklab);
        iam_style_unregister(g_overlayUiAnimationState.themeTransitionTargetStyleId);
        g_overlayUiAnimationState.themeTransitionTargetStyleId = 0;
        g_overlayUiAnimationState.themeTransitionActive = false;
    }
}

void NotifyMainTabSelected(MainSettingsTab selectedTab) {
    if (!g_overlayUiAnimationState.mainTabInitialized) {
        g_overlayUiAnimationState.mainTabInitialized = true;
        g_overlayUiAnimationState.activeMainTab = selectedTab;
        g_overlayUiAnimationState.mainTabTransitionProgress = 1.0f;
        return;
    }

    if (g_overlayUiAnimationState.activeMainTab != selectedTab) {
        g_overlayUiAnimationState.activeMainTab = selectedTab;
        g_overlayUiAnimationState.mainTabTransitionProgress = 0.0f;
    }
}

void PushMainTabContentAnimationStyle() {
    if (g_overlayUiAnimationState.mainTabTransitionProgress < 1.0f) {
        const float dt = GetOverlayAnimationDeltaTime();
        const float step = dt / std::max(g_overlayUiAnimationState.mainTabTransitionDurationSeconds, 0.01f);
        g_overlayUiAnimationState.mainTabTransitionProgress =
            std::min(1.0f, g_overlayUiAnimationState.mainTabTransitionProgress + step);
    }

    const float eased = iam_eval_preset(iam_ease_out_cubic, g_overlayUiAnimationState.mainTabTransitionProgress);
    const float alpha = 0.35f + (0.65f * eased);
    const float yOffset = (1.0f - eased) * 8.0f;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOffset);
    SubmitCursorBoundaryItem();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
}
