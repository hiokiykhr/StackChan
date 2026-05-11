/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_hermes.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppHermes::AppHermes()
{
    setAppInfo().name = "HERMES";
    static auto icon  = assets::get_image("icon_ai_agent.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t theme_color = 0x6B4EFF;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppHermes::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppHermes::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Reuse the existing XiaoZhi start bridge because the server-side path
    // already points to the Hermes-compatible OTA/WebSocket backend.
    GetHAL().requestXiaozhiStart();
}

void AppHermes::onRunning()
{
}

void AppHermes::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}
