#pragma once

#define NUM_LENGTH 6
#define ANSWER_WAIT_MS 180000 // 180с ожидания ответа по стандарту
#define DIAL_WAIT_MS 30000    // 30с - придумано от балды

#define LINE_READY_OR_BUSY 425
#define OFF_HOOK_WARNING 950
#define BUSY_SIG_INTERVAL 400
#define OVERLOAD_SIG_INTERVAL 150

#define LINE1_PROBE A0
#define LINE2_PROBE A1
#define LINE1_SIGNAL 9
#define LINE2_SIGNAL 10
#define LINE1_RING 4
#define LINE2_RING 5
#define LINE_CONNECT 6

#define OFF_HOOK_ADC 800