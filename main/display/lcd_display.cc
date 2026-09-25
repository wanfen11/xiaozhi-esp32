#include "lcd_display.h"
#include <vector>
#include "font_awesome_symbols.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "assets/lang_config.h"
#include <cstring>
#include <ctime>
#include "board.h"

#define TAG "LcdDisplay"

// Dark mode color definitions
#if CONFIG_USE_DARK_MODE
#define BACKGROUND_COLOR        lv_color_hex(0x121212)
#define TEXT_COLOR              lv_color_white()
#define CHAT_BACKGROUND_COLOR   lv_color_hex(0x1E1E1E)
#define USER_BUBBLE_COLOR       lv_color_hex(0x1A6C37)
#define ASSISTANT_BUBBLE_COLOR  lv_color_hex(0x333333)
#define SYSTEM_BUBBLE_COLOR     lv_color_hex(0x2A2A2A)
#define SYSTEM_TEXT_COLOR       lv_color_hex(0xAAAAAA)
#define BORDER_COLOR            lv_color_hex(0x333333)
#define LOW_BATTERY_COLOR       lv_color_hex(0xFF0000)
#define CLOCK_TIME_COLOR        lv_color_white()
#define CLOCK_DATE_COLOR        lv_color_hex(0xAAAAAA)
#else
#define BACKGROUND_COLOR        lv_color_white()
#define TEXT_COLOR              lv_color_black()
#define CHAT_BACKGROUND_COLOR   lv_color_hex(0xE0E0E0)
#define USER_BUBBLE_COLOR       lv_color_hex(0x95EC69)
#define ASSISTANT_BUBBLE_COLOR  lv_color_white()
#define SYSTEM_BUBBLE_COLOR     lv_color_hex(0xE0E0E0)
#define SYSTEM_TEXT_COLOR       lv_color_hex(0x666666)
#define BORDER_COLOR            lv_color_hex(0xE0E0E0)
#define LOW_BATTERY_COLOR       lv_color_black()
#define CLOCK_TIME_COLOR        lv_color_black()
#define CLOCK_DATE_COLOR        lv_color_hex(0x888888)
#endif

LV_FONT_DECLARE(font_awesome_30_4);

// 时钟标签指针（供 LVGL 定时器回调使用）
static lv_obj_t* s_clock_time_label = NULL;
static lv_obj_t* s_clock_date_label = NULL;
static lv_obj_t* s_clock_container = NULL;

// LVGL 定时器回调：每秒更新时钟
static void clock_update_timer_cb(lv_timer_t* timer) {
    (void)timer;
    if (s_clock_time_label == NULL || s_clock_date_label == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);

    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M", timeinfo);
    lv_label_set_text(s_clock_time_label, time_buf);

    const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%d月%d日 星期%s",
             timeinfo->tm_mon + 1, timeinfo->tm_mday, weekdays[timeinfo->tm_wday]);
    lv_label_set_text(s_clock_date_label, date_buf);
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                            int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                            DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts) {
    width_ = width;
    height_ = height;

    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD screen");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = NULL,
        .buffer_size = static_cast<uint32_t>(width_ * 10),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == NULL) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                            int width, int height, int offset_x, int offset_y,
                            bool mirror_x, bool mirror_y, bool swap_xy,
                            DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts) {
    width_ = width;
    height_ = height;

    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD screen");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 10),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }
    };

    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == NULL) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

LcdDisplay::~LcdDisplay() {
    if (content_ != NULL) {
        lv_obj_del(content_);
    }
    if (status_bar_ != NULL) {
        lv_obj_del(status_bar_);
    }
    if (side_bar_ != NULL) {
        lv_obj_del(side_bar_);
    }
    if (container_ != NULL) {
        lv_obj_del(container_);
    }
    if (display_ != NULL) {
        lv_display_delete(display_);
    }
    if (panel_ != NULL) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != NULL) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void LcdDisplay::Unlock() {
    lvgl_port_unlock();
}

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, TEXT_COLOR, 0);
    lv_obj_set_style_bg_color(screen, BACKGROUND_COLOR, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, BACKGROUND_COLOR, 0);
    lv_obj_set_style_border_color(container_, BORDER_COLOR, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.emoji_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, BACKGROUND_COLOR, 0);
    lv_obj_set_style_text_color(status_bar_, TEXT_COLOR, 0);

    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 5, 0);
    lv_obj_set_style_bg_color(content_, CHAT_BACKGROUND_COLOR, 0);
    lv_obj_set_style_border_color(content_, BORDER_COLOR, 0);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, 10, 0);

    chat_message_label_ = NULL;

    // ===== 待机时钟：屏幕中央显示时间和日期 =====
    s_clock_container = lv_obj_create(content_);
    lv_obj_set_size(s_clock_container, LV_HOR_RES - 10, LV_VER_RES - fonts_.emoji_font->line_height - 30);
    lv_obj_set_style_bg_opa(s_clock_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_clock_container, 0, 0);
    lv_obj_set_style_pad_all(s_clock_container, 0, 0);
    lv_obj_clear_flag(s_clock_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(s_clock_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_clock_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 时间（大字）
    s_clock_time_label = lv_label_create(s_clock_container);
    lv_obj_set_style_text_font(s_clock_time_label, fonts_.emoji_font, 0);
    lv_obj_set_style_text_color(s_clock_time_label, CLOCK_TIME_COLOR, 0);
    lv_label_set_text(s_clock_time_label, "--:--");

    // 日期（小字）
    s_clock_date_label = lv_label_create(s_clock_container);
    lv_obj_set_style_text_font(s_clock_date_label, fonts_.text_font, 0);
    lv_obj_set_style_text_color(s_clock_date_label, CLOCK_DATE_COLOR, 0);
    lv_label_set_text(s_clock_date_label, "Loading...");

    // 立即更新一次时钟
    clock_update_timer_cb(NULL);
    // 启动 LVGL 定时器，每秒更新一次
    lv_timer_create(clock_update_timer_cb, 1000, NULL);

    /* Status bar 内容 */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, TEXT_COLOR, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, TEXT_COLOR, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, TEXT_COLOR, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, TEXT_COLOR, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, TEXT_COLOR, 0);

    emotion_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(emotion_label_, TEXT_COLOR, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    lv_obj_set_style_margin_left(emotion_label_, 5, 0);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, LOW_BATTERY_COLOR, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    lv_obj_t* low_battery_label = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label, lv_color_white(), 0);
    lv_obj_center(low_battery_label);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

#define MAX_MESSAGES 50

void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (content_ == NULL) {
        return;
    }
    if (strlen(content) == 0) return;

    // 收到新消息时，隐藏待机时钟
    if (s_clock_container != NULL) {
        lv_obj_add_flag(s_clock_container, LV_OBJ_FLAG_HIDDEN);
    }

    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 1, 0);
    lv_obj_set_style_border_color(msg_bubble, BORDER_COLOR, 0);
    lv_obj_set_style_pad_all(msg_bubble, 8, 0);

    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);

    lv_coord_t text_width = lv_txt_get_width(content, strlen(content), fonts_.text_font, 0);
    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;
    lv_coord_t min_width = 20;
    lv_coord_t bubble_width;

    if (text_width < min_width) {
        text_width = min_width;
    }
    if (text_width < max_width) {
        bubble_width = text_width;
    } else {
        bubble_width = max_width;
    }

    lv_obj_set_width(msg_text, bubble_width);
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(msg_text, fonts_.text_font, 0);
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    if (strcmp(role, "user") == 0) {
        // 用户消息：绿色气泡，右对齐
        lv_obj_set_style_bg_color(msg_bubble, USER_BUBBLE_COLOR, 0);
        lv_obj_set_style_text_color(msg_text, TEXT_COLOR, 0);
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_margin_right(msg_bubble, 10, 0);
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // AI助手消息：白色气泡，左对齐
        lv_obj_set_style_bg_color(msg_bubble, ASSISTANT_BUBBLE_COLOR, 0);
        lv_obj_set_style_text_color(msg_text, TEXT_COLOR, 0);
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_margin_left(msg_bubble, 0, 0);
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        // 系统消息：灰色，居中
        lv_obj_set_style_bg_color(msg_bubble, SYSTEM_BUBBLE_COLOR, 0);
        lv_obj_set_style_text_color(msg_text, SYSTEM_TEXT_COLOR, 0);
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }

    if (strcmp(role, "user") == 0) {
        // 用户消息：全宽容器内右对齐
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        lv_obj_set_parent(msg_bubble, container);
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else if (strcmp(role, "system") == 0) {
        // 系统消息：全宽容器内居中
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        lv_obj_set_parent(msg_bubble, container);
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else {
        // AI助手消息：左对齐
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
    }

    chat_message_label_ = msg_text;

    // 限制最大消息数
    uint32_t msg_count = lv_obj_get_child_cnt(content_);
    while (msg_count >= MAX_MESSAGES) {
        lv_obj_t* oldest_msg = lv_obj_get_child(content_, 0);
        if (oldest_msg != NULL) {
            lv_obj_del(oldest_msg);
            msg_count--;
        } else {
            break;
        }
    }
}
#else
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, TEXT_COLOR, 0);
    lv_obj_set_style_bg_color(screen, BACKGROUND_COLOR, 0);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, BACKGROUND_COLOR, 0);
    lv_obj_set_style_border_color(container_, BORDER_COLOR, 0);

    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.emoji_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, BACKGROUND_COLOR, 0);
    lv_obj_set_style_text_color(status_bar_, TEXT_COLOR, 0);

    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_bg_color(content_, BACKGROUND_COLOR, 0);
    lv_obj_set_style_border_color(content_, BORDER_COLOR, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(emotion_label_, TEXT_COLOR, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);

    // 待机时钟
    s_clock_time_label = lv_label_create(content_);
    lv_obj_set_style_text_font(s_clock_time_label, fonts_.emoji_font, 0);
    lv_obj_set_style_text_color(s_clock_time_label, CLOCK_TIME_COLOR, 0);
    lv_label_set_text(s_clock_time_label, "--:--");

    s_clock_date_label = lv_label_create(content_);
    lv_obj_set_style_text_font(s_clock_date_label, fonts_.text_font, 0);
    lv_obj_set_style_text_color(s_clock_date_label, CLOCK_DATE_COLOR, 0);
    lv_label_set_text(s_clock_date_label, "Loading...");

    clock_update_timer_cb(NULL);
    lv_timer_create(clock_update_timer_cb, 1000, NULL);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, TEXT_COLOR, 0);

    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, TEXT_COLOR, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, TEXT_COLOR, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, TEXT_COLOR, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, TEXT_COLOR, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, TEXT_COLOR, 0);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, LOW_BATTERY_COLOR, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    lv_obj_t* low_battery_label = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label, lv_color_white(), 0);
    lv_obj_center(low_battery_label);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}
#endif

void LcdDisplay::SetEmotion(const char* emotion) {
    struct Emotion {
        const char* icon;
        const char* text;
    };
    static const Emotion emotions[] = {
        {"\xEF\x98\xB6", "neutral"},
        {"\xEF\x99\x82", "happy"},
        {"\xEF\x98\x86", "laughing"},
        {"\xEF\x98\x82", "funny"},
        {"\xEF\x98\x94", "sad"},
        {"\xEF\x98\xA0", "angry"},
        {"\xEF\x98\xAD", "crying"},
        {"\xEF\x98\x8D", "loving"},
        {"\xEF\x98\xB3", "embarrassed"},
        {"\xEF\x99\xAF", "surprised"},
        {"\xEF\x98\xB1", "shocked"},
        {"\xF0\x9F\xA4\x94", "thinking"},
        {"\xEF\x98\x89", "winking"},
        {"\xEF\x98\x8E", "cool"},
        {"\xEF\x98\x8C", "relaxed"},
        {"\xF0\x9F\xA4\xA4", "delicious"},
        {"\xEF\x98\x98", "kissy"},
        {"\xEF\x98\x8F", "confident"},
        {"\xF0\x9F\x98\xB4", "sleepy"},
        {"\xF0\x9F\x98\x9C", "silly"},
        {"\xF0\x9F\x99\x84", "confused"}
    };
    static const int emotions_count = sizeof(emotions) / sizeof(emotions[0]);

    DisplayLockGuard lock(this);
    if (emotion_label_ == NULL) {
        return;
    }

    bool found = false;
    for (int i = 0; i < emotions_count; i++) {
        if (strcmp(emotion, emotions[i].text) == 0) {
            lv_obj_set_style_text_font(emotion_label_, fonts_.emoji_font, 0);
            lv_label_set_text(emotion_label_, emotions[i].icon);
            found = true;
            break;
        }
    }
    if (!found) {
        lv_obj_set_style_text_font(emotion_label_, fonts_.emoji_font, 0);
        lv_label_set_text(emotion_label_, "\xEF\x98\xB6");
    }
}

void LcdDisplay::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == NULL) {
        return;
    }
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_label_set_text(emotion_label_, icon);
}
