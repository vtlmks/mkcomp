// Precision timer adapted from mkfw_linux_timer.c
// Copyright (c) 2025-2026 Peter Fors
// SPDX-License-Identifier: MIT

#include <pthread.h>
#include <sys/syscall.h>
#include <linux/futex.h>

struct timer {
	uint64_t interval_ns;
	struct timespec next_deadline;
	uint32_t running;
	pthread_t thread;
	int futex_word;
};

// [=]===^=[ timespec_add_ns ]===============================[=]
static void timespec_add_ns(struct timespec *ts, uint64_t ns) {
	ts->tv_nsec += ns;
	while(ts->tv_nsec >= 1000000000L) {
		ts->tv_nsec -= 1000000000L;
		ts->tv_sec++;
	}
}

// [=]===^=[ timer_thread_func ]=============================[=]
static void *timer_thread_func(void *arg) {
	struct timer *t = (struct timer *)arg;

	while(__atomic_load_n(&t->running, __ATOMIC_ACQUIRE)) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC_RAW, &now);

		int64_t sec = t->next_deadline.tv_sec - now.tv_sec;
		int64_t nsec = t->next_deadline.tv_nsec - now.tv_nsec;
		if(nsec < 0) {
			nsec += 1000000000L;
			sec -= 1;
		}
		int64_t diff_ns = sec * 1000000000LL + nsec;

		if(diff_ns > 0) {
			struct timespec sleep_time;
			sleep_time.tv_sec = diff_ns / 1000000000;
			sleep_time.tv_nsec = diff_ns % 1000000000;
			nanosleep(&sleep_time, 0);
		}

		__atomic_store_n(&t->futex_word, 1, __ATOMIC_RELEASE);
		syscall(SYS_futex, &t->futex_word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);

		timespec_add_ns(&t->next_deadline, t->interval_ns);
	}

	return 0;
}

// [=]===^=[ timer_new ]=====================================[=]
static struct timer *timer_new(uint64_t interval_ns) {
	struct timer *t = calloc(1, sizeof(struct timer));
	t->interval_ns = interval_ns;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t->next_deadline);
	timespec_add_ns(&t->next_deadline, interval_ns);
	t->running = 1;
	t->futex_word = 0;
	pthread_create(&t->thread, NULL, timer_thread_func, t);
	return t;
}

// [=]===^=[ timer_wait ]====================================[=]
static void timer_wait(struct timer *t) {
	syscall(SYS_futex, &t->futex_word, FUTEX_WAIT_PRIVATE, 0, 0, 0, 0);
	__atomic_store_n(&t->futex_word, 0, __ATOMIC_RELEASE);
}

// [=]===^=[ timer_destroy ]=================================[=]
static void timer_destroy(struct timer *t) {
	if(!t) {
		return;
	}
	__atomic_store_n(&t->running, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&t->futex_word, 1, __ATOMIC_RELEASE);
	syscall(SYS_futex, &t->futex_word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
	pthread_join(t->thread, NULL);
	free(t);
}
