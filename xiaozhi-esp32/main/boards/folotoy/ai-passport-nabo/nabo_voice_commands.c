#include "nabo_voice_commands.h"
#include <stddef.h>
#include <string.h>

static int take_prefix(const char** at, const char* prefix) {
    size_t n = strlen(prefix);
    if (strncmp(*at, prefix, n))
        return 0;
    *at += n;
    return 1;
}
const char* nabo_voice_launch_app(const char* transcript) {
    if (!transcript)
        return NULL;
    char clean[192];
    size_t used = 0;
    for (const char* p = transcript; *p;) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',' || *p == '.' ||
            *p == '!' || *p == '?') {
            ++p;
            continue;
        }
        const char* punctuation[] = {"，", "。", "！", "？", "、"};
        int skipped = 0;
        for (size_t i = 0; i < sizeof(punctuation) / sizeof(*punctuation); ++i) {
            size_t n = strlen(punctuation[i]);
            if (!strncmp(p, punctuation[i], n)) {
                p += n;
                skipped = 1;
                break;
            }
        }
        if (skipped)
            continue;
        if (used + 1 >= sizeof(clean))
            return NULL;
        clean[used++] = *p++;
    }
    clean[used] = 0;
    const char* at = clean;
    take_prefix(&at, "小智");
    take_prefix(&at, "请");
    take_prefix(&at, "你");
    take_prefix(&at, "帮我");
    take_prefix(&at, "给我");
    take_prefix(&at, "我想");
    take_prefix(&at, "我要");
    const char* verbs[] = {"打开", "启动", "开启", "进入", "运行", "玩一下", "玩"};
    int launch = 0;
    for (size_t i = 0; i < sizeof(verbs) / sizeof(*verbs); ++i)
        if (take_prefix(&at, verbs[i])) {
            launch = 1;
            break;
        }
    if (!launch)
        return NULL;
    take_prefix(&at, "一下");
    struct {
        const char* spoken;
        const char* app;
    } aliases[] = {{"牌桌", "table"},           {"大话骰", "table"},       {"二十一点", "table"},
                   {"21点", "table"},           {"打牌", "table"},         {"跳一跳", "jump"},
                   {"更多设置", "settings"},
                   {"设置", "settings"},       {"工具", "tools"},        {"谁是卧底", "undercover"},
                   {"卧底", "undercover"},       {"联机对决", "duel"},     {"对决", "duel"},
                   {"快枪手", "duel"}};
    for (size_t i = 0; i < sizeof(aliases) / sizeof(*aliases); ++i) {
        const char* rest = at;
        if (!take_prefix(&rest, aliases[i].spoken))
            continue;
        take_prefix(&rest, "程序");
        take_prefix(&rest, "小游戏");
        take_prefix(&rest, "一下");
        take_prefix(&rest, "吧");
        if (!*rest)
            return aliases[i].app;
    }
    return NULL;
}
