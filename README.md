About the project:
Pintos is a homework for cs318 operating system.
++++++++++++++++++++++++++++++++++++++++++++++++++
Project1:
1. alarm clock
* intro : when function timer_sleep is called, alarm is added to the thread and alarm checked when function timer_interrupt is called.
* files changed : timer.c, threads.c.
* files added : alarm.h, alarm.c.
* about interrupt : interrupt disabled when alarm added to the list, enabled when operation completed.
                    interrupt disabled when dismissing the alarm, enabled when operation completed.
2. priority scheduling
* intro :