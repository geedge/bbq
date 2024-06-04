/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-04 18:05:02
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct test {
    int count;
    size_t per_size;
    char *entries;
} entries_s;

void entries_print_magic(entries_s *q) {
    printf("zend:%c\n", q->entries[q->count * q->per_size]);
}

void entries_init(entries_s *q, int count, size_t obj_size) {
    q->count = count;
    q->per_size = obj_size;
    q->entries = malloc(count * obj_size + 1);
    q->entries[count * obj_size] = 'e';
}

void entries_print_obj(entries_s *q) {
    for (int i = 0; i < q->count; i++) {
        int *tmp = (int *)(&(q->entries[i * q->per_size]));
        printf("%p\n", *tmp);
    }
    entries_print_magic(q);
}

void test1() {
    entries_s q;
    size_t count = 10;
    entries_init(&q, count, sizeof(int));

    for (int i = 0; i < count; i++) {
        memcpy(&q.entries[q.per_size * i], &i, sizeof(int));
    }

    for (int i = 0; i < count; i++) {
        int tmp = 0;
        memcpy(&tmp, &q.entries[q.per_size * i], sizeof(int));
        printf("%d\n", tmp);
    }
}

void test2() {
    entries_s q;
    size_t count = 10;
    entries_init(&q, count, sizeof(int *));
    int **arr = malloc(sizeof(int *) * count);
    for (int i = 0; i < count; i++) {
        arr[i] = malloc(sizeof(int));
        *arr[i] = i;
    }

    for (int i = 0; i < count; i++) {
        memcpy(&q.entries[q.per_size * i], &arr[i], sizeof(int *));
    }

    for (int i = 0; i < count; i++) {
        int *tmp = NULL;
        memcpy(&tmp, &q.entries[q.per_size * i], sizeof(int *));
        printf("%d\n", *tmp);
    }
}

int main() {
    // test1();
    test2();
}