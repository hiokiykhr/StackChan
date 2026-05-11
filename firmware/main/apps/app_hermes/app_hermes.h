/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>

class AppHermes : public mooncake::AppAbility {
public:
    AppHermes();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
};
