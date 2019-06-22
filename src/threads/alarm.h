#ifndef THREADS_ALARM_H
#define THREADS_ALARM_H

#include <list.h>

struct alarm{
    struct thread* thrd;
    struct list_elem elem;
    int64_t ticks;
};

void alarm_init (void);

void alarm_set (int64_t);

void alarm_check (void);

#endif