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
    static auto icon  = assets::get_image("icon_hermes_agent.bin");
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

    // Start Hermes through the existing protocol-compatible voice bridge.
    GetHAL().requestXiaozhiStart();
}

void AppHermes::onRunning()
{
}

void AppHermes::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}
