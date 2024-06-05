/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-05 14:48:31
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include <stdio.h>

int main() {
    int a = 3;
    int b[a];

    printf("%d\n", sizeof(b) / sizeof(b[0]));
    return 0;
}