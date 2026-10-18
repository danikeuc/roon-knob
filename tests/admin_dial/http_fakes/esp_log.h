#pragma once
#include <stdio.h>
const char *esp_err_to_name(int);
void fixture_log(const char *format,...);
#define ESP_LOGI(tag,...) ((void)(tag),fixture_log(__VA_ARGS__))
#define ESP_LOGW(tag,...) ((void)(tag),fixture_log(__VA_ARGS__))
#define ESP_LOGE(tag,...) ((void)(tag),fixture_log(__VA_ARGS__))
