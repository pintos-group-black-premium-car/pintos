#include <list.h>
#include "threads/alarm.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/interrupt.h"

#define ALARM_MAGIC 0x67452301  /* ALARM_MAGIC */

static struct list alarm_list;
static int64_t cur_ticks;

static void alarm_dismiss (struct alarm *);
static bool is_alarm (struct alarm *);

void alarm_init (void){
    list_init (&alarm_list);
    ASSERT (list_empty (&alarm_list));
    cur_ticks = timer_ticks ();
}

void alarm_set (int64_t ticks){
    struct thread* thrd = thread_current();
    ASSERT(thrd->status == THREAD_RUNNING);
    if (ticks == 0) return;
    struct alarm* alrm = &thrd->alrm;
    alrm->thrd = thrd;
    alrm->ticks = ticks + timer_ticks ();

    /* interrupt disabled to add the alarm to the alarm_list. */
    intr_disable ();
    list_push_back (&alarm_list, &alrm->elem);
    /* block the thread and enable the interrupt. */
    thread_block ();
    intr_enable ();
}

void alarm_check (void){
    struct list_elem *cur = list_begin (&alarm_list), *next;
    struct alarm *alrm;
    while (cur != list_end (&alarm_list)){
        alrm = list_entry (cur, struct alarm, elem);
        next = list_next (cur);
        if (alrm->ticks <= timer_ticks ()){
            alarm_dismiss (alrm);
        }
        cur = next;
    }
}

/* dismiss the alarm of the thread and unblock the thread. */
static void alarm_dismiss (struct alarm *alrm){
    enum intr_level pre_level = intr_disable ();
    list_remove (&alrm->elem);
    thread_unblock (&alrm->elem);
    intr_set_level (pre_level);
}

/* check whether is an alarm. */
static bool is_alarm (struct alarm *alrm){
    return (alrm != NULL && alrm -> magic == ALARM_MAGIC);
}