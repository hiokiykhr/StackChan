/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "M5Unified.h"
#include "sdkconfig.h"

extern "C" {
#include <stdio.h>
#include "esp_log.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "ui.h"
#include "esp_now_init.h"
#include "joystick_handle.h"

#include "lvgl_port.h"

using namespace m5;

joystick_data_t joystick_data;

static void resolve_joystick_i2c_pins(int &sda_pin, int &scl_pin, const char *&board_name, const char *&port_name)
{
#if CONFIG_STACKCHAN_TARGET_M5STICK_C_PLUS
    board_name = "M5StickC Plus";
#elif CONFIG_STACKCHAN_TARGET_M5STICK_S3
    board_name = "M5StickS3";
#else
    board_name = "Unknown";
#endif

#if CONFIG_STACKCHAN_JOYSTICK_PORT_LEGACY_STICKC_HAT
    port_name = "StickC Plus HAT / legacy";
    sda_pin   = 0;
    scl_pin   = 26;
#elif CONFIG_STACKCHAN_JOYSTICK_PORT_MINIJOYC_HAT
    port_name = "MiniJoyC HAT";
    // MiniJoyC is a StickC-family HAT. On M5StickS3 the HAT I2C pins are
    // different from M5Unified's Grove/Ex_I2C pins (SDA=GPIO9, SCL=GPIO10).
    sda_pin = 2;
    scl_pin = 1;
#elif CONFIG_STACKCHAN_JOYSTICK_PORT_GROVE
    port_name = "Grove / M5.Ex_I2C";
    sda_pin   = M5.Ex_I2C.getSDA();
    scl_pin   = M5.Ex_I2C.getSCL();
#else
    port_name = "Fallback M5.Ex_I2C";
    sda_pin   = M5.Ex_I2C.getSDA();
    scl_pin   = M5.Ex_I2C.getSCL();
#endif

    if (sda_pin < 0 || scl_pin < 0) {
        ESP_LOGW("StackChan", "Resolved invalid I2C pins for %s/%s (%d, %d), falling back to legacy StickC+ pins",
                 board_name, port_name, sda_pin, scl_pin);
        port_name = "Fallback StickC Plus HAT / legacy";
        sda_pin   = 0;
        scl_pin   = 26;
    }
}

// extern void lvgl_port_init(M5GFX &gfx);

/**
 * @brief Handle Button Press.
 * 1. Press BtnA to switch setup_mode UI and running_mode UI.
 * 2. Press BtnB to switch espnow-channel or id on setup_mode;
 * 3. Press BtnB to send btnB_status to remote on running_mode.
 */
void handle_button_press()
{
    static uint8_t screen_mode = MODE_SETUP;
    // check if BtnA is pressed
    if (M5.BtnA.wasPressed()) {
        // use BtnA to switch mode
        screen_mode = (screen_mode + 1) % 3;

        if (screen_mode == MODE_SETUP) {
            // in setup mode, press A to enter running mode
            joystick_data.screen_mode = MODE_SETUP;
            switch_screen(joystick_data.screen_mode);
        } else if (screen_mode == MODE_RUNNING) {
            // in running mode, press A to enter IMU mode
            wifi_espnow_reinit(joystick_data.channel);
            joystick_data.screen_mode = MODE_RUNNING;
            switch_screen(joystick_data.screen_mode);
        } else if (screen_mode == MODE_IMU) {
            // in IMU mode, press A to return to setup mode
            joystick_data.screen_mode = MODE_IMU;
            switch_screen(joystick_data.screen_mode);
        }
    }
    if (M5.BtnB.wasPressed()) {
        if (joystick_data.screen_mode == MODE_SETUP) {
            joystick_data.select_mode = !joystick_data.select_mode;
        } else if ((joystick_data.screen_mode == MODE_RUNNING) || (joystick_data.screen_mode == MODE_IMU)) {
            joystick_data.btnB_status = !joystick_data.btnB_status;
        }
    }
}

void app_main(void)
{
    imu_data_t imu_data;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    auto cfg = M5.config();
#if CONFIG_STACKCHAN_TARGET_M5STICK_S3
    cfg.fallback_board = m5gfx::board_t::board_M5StickS3;
#elif CONFIG_STACKCHAN_TARGET_M5STICK_C_PLUS
    cfg.fallback_board = m5gfx::board_t::board_M5StickCPlus;
#endif
    cfg.clear_display = true;
    cfg.output_power   = true;
    cfg.pmic_button    = true;
    cfg.internal_imu   = true;
    cfg.internal_rtc   = true;
    cfg.internal_mic   = true;
    cfg.internal_spk   = true;
    M5.begin(cfg);
    M5.Power.begin();
    M5.Power.setExtOutput(true);  // enable external port power for StickS3 / Grove devices
    M5.Lcd.setBrightness(100);    // set brightness to 100
    M5.Imu.init(&M5.In_I2C);      // init IMU with internal I2C port
    printf("Board: %d\n", (int)M5.getBoard());
    printf("IN_I2C port: %d\n", M5.In_I2C.getPort());
    printf("EX_I2C port: %d (SDA=%d, SCL=%d)\n", M5.Ex_I2C.getPort(), M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL());
    printf("M5 Display width: %ld, height: %ld\n", M5.Display.width(), M5.Display.height());

    int joystick_sda = 0;
    int joystick_scl = 0;
    const char *board_name = nullptr;
    const char *joystick_port_name = nullptr;
    resolve_joystick_i2c_pins(joystick_sda, joystick_scl, board_name, joystick_port_name);
    printf("Board profile: %s, joystick port: %s, I2C SDA=%d, SCL=%d\n", board_name, joystick_port_name,
           joystick_sda, joystick_scl);

    joystick_data = joystick_init(joystick_sda, joystick_scl);  // init joystick

    lvgl_port_init();  // init LVGL
    ui_init();         // init UI

    // init WiFi and ESP-NOW
    wifi_espnow_init(joystick_data.channel);

    xTaskCreate(handle_setup_screen, "handle_setup_screen", 8192, &joystick_data, 5, NULL);      // handle setup mode
    xTaskCreate(handle_running_screen, "handle_running_screen", 8192, &joystick_data, 5, NULL);  // handle running mode
    xTaskCreate(handle_imu_screen, "handle_imu_screen", 8192, &joystick_data, 5, NULL);

    while (1) {
        M5.update();
        // Handle button press
        handle_button_press();
        joystick_data.bat = (M5.Power.getBatteryLevel());  // update battery level

        joystick_data.bat = (joystick_data.bat > 100) ? 100 : joystick_data.bat;
        joystick_data.bat = (joystick_data.bat < 0) ? 0 : joystick_data.bat;

        M5.Imu.update();                              // update IMU data
        imu_data              = M5.Imu.getImuData();  // get IMU data
        joystick_data.accel_x = imu_data.accel.x;
        joystick_data.accel_y = imu_data.accel.y;
        joystick_data.accel_z = imu_data.accel.z;

#if 0
        printf("Accel: (%.2f, %.2f, %.2f), Gyro: (%.2f, %.2f, %.2f)\n",
               joystick_data.accel_x, joystick_data.accel_y, joystick_data.accel_z,
               joystick_data.gyro_x, joystick_data.gyro_y, joystick_data.gyro_z);
#endif
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
}