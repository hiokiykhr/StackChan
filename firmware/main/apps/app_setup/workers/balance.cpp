/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <apps/common/loading_page/loading_page.h>
#include <hal/nfc/nfc_service.h>
#include <esp_timer.h>
#include <mooncake_log.h>

using namespace uitk::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-TransitBalance";

class TransitBalanceWorker::PageBalance {
public:
    PageBalance(const hal::TransitIcBalanceInfo& info)
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0xF6F6F6));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setPadding(0, 20, 0, 0);
        _panel->setRadius(0);

        _title = std::make_unique<Label>(_panel->get());
        _title->setTextFont(&lv_font_montserrat_20);
        _title->setTextColor(lv_color_hex(0x7E7B9C));
        _title->align(LV_ALIGN_TOP_MID, 0, 12);
        _title->setText("IC Balance");

        _status = std::make_unique<Label>(_panel->get());
        _status->setTextFont(&lv_font_montserrat_20);
        _status->setTextColor(info.success ? lv_color_hex(0x14532D) : lv_color_hex(0x9A3412));
        _status->align(LV_ALIGN_TOP_MID, 0, 58);
        _status->setText(info.success ? "Read OK" : "Read Failed");

        _balance = std::make_unique<Label>(_panel->get());
        _balance->setTextFont(&lv_font_montserrat_24);
        _balance->setTextColor(lv_color_hex(0x07162C));
        _balance->align(LV_ALIGN_TOP_MID, 0, 96);
        _balance->setWidth(300);
        _balance->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _balance->setText(info.success ? (std::string("Balance: ") + std::to_string(info.balance_yen) + " yen") : info.detail);

        _detail = std::make_unique<Label>(_panel->get());
        _detail->setTextFont(&lv_font_montserrat_16);
        _detail->setTextColor(lv_color_hex(0x334155));
        _detail->align(LV_ALIGN_TOP_MID, 0, 144);
        _detail->setWidth(296);
        _detail->setTextAlign(LV_TEXT_ALIGN_CENTER);
        if (info.success) {
            _detail->setText(std::string("IDm: ") + info.idm_hex + "\n" + std::string("System: ") + info.system_code_hex);
        } else {
            _detail->setText("Hold card near head and retry");
        }

        _btn_quit = std::make_unique<Button>(_panel->get());
        apply_button_common_style(*_btn_quit);
        _btn_quit->align(LV_ALIGN_TOP_MID, 0, 198);
        _btn_quit->setSize(290, 36);
        _btn_quit->label().setText("Back");
        _btn_quit->label().setTextFont(&lv_font_montserrat_20);
        _btn_quit->onClick().connect([this]() { _is_quit_clicked = true; });
    }

    bool isQuitClicked() const
    {
        return _is_quit_clicked;
    }

private:
    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _title;
    std::unique_ptr<Label> _status;
    std::unique_ptr<Label> _balance;
    std::unique_ptr<Label> _detail;
    std::unique_ptr<Button> _btn_quit;
    bool _is_quit_clicked = false;
};

TransitBalanceWorker::TransitBalanceWorker()
{
    _page_loading = std::make_unique<view::LoadingPage>(0xF6F6F6, 0x26206A);
    _page_loading->setMessage("Hold IC card near head");
    _created_us = static_cast<uint64_t>(esp_timer_get_time());
    _next_probe_us = _created_us + 200000;
}

TransitBalanceWorker::~TransitBalanceWorker()
{
}

void TransitBalanceWorker::update()
{
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());

    if (_page_balance) {
        if (_page_balance->isQuitClicked()) {
            mclog::tagInfo(_tag, "quit clicked");
            _page_balance.reset();
            _is_done = true;
        }
        return;
    }

    if (!_page_loading) {
        return;
    }

    if (now_us < _next_probe_us) {
        return;
    }

    hal::TransitIcBalanceInfo info;
    const bool ok = hal::NfcService::GetInstance().TryReadTransitIcBalance(info);
    if (ok) {
        mclog::tagInfo(_tag, "balance read success: {}", info.detail);
        _page_loading.reset();
        _page_balance = std::make_unique<PageBalance>(info);
        return;
    }

    _last_error = info.detail.empty() ? "Read failed" : info.detail;
    mclog::tagWarn(_tag, "balance probe failed: {}", _last_error);
    _page_loading->setMessage("Hold IC card near head\nRetrying...");

    constexpr uint64_t kRetryIntervalUs = 700000;
    constexpr uint64_t kMaxWaitUs = 15000000;
    if (now_us - _created_us >= kMaxWaitUs) {
        hal::TransitIcBalanceInfo failed_info;
        failed_info.detail = _last_error;
        _page_loading.reset();
        _page_balance = std::make_unique<PageBalance>(failed_info);
        return;
    }

    _next_probe_us = now_us + kRetryIntervalUs;
}
