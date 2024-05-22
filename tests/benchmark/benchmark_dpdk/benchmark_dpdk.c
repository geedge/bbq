/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-22 22:05:54
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "common.h"
#include "iniparser.h"

int main() {
    // 加载配置
    dictionary *ini = iniparser_load("./config/case1.ini");
    if (ini == NULL) {
        return -1;
    }

    // 获取键值
    const char *cHeadHost = iniparser_getstring(ini, "head:host", "127.0.0.1");
    printf("cHeadHost %s\n", cHeadHost);

    const char *cBodyData = iniparser_getstring(ini, "body:data", "welcome");
    printf("cBodyData %s\n", cBodyData);

    int iHeadPort = iniparser_getint(ini, "head:port", 80);
    printf("iHeadPort %d\n", iHeadPort);

    double iHeadReate = iniparser_getdouble(ini, "head:rate", 9.99);
    printf("iHeadReate %.2lf\n", iHeadReate);

    // 释放dictionary对象
    iniparser_freedict(ini);
    return 0;
}