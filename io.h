#pragma once


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#ifndef TEMP_FAILURE_RETRY

#define TEMP_FAILURE_RETRY(expression) ({ \
	__typeof(expression) __temp_result; \
	do { \
		__temp_result = (expression); \
	} while (__temp_result == (__typeof(expression))-1 && errno == EINTR); \
	__temp_result; \
})

#endif

#define io_write(fd, buf, n) \
	TEMP_FAILURE_RETRY(write((fd), (buf), (n)))

#define io_read(fd, buf, nbytes) \
	TEMP_FAILURE_RETRY(read((fd), (buf), (nbytes)))

#define io_fcntl(fd, flags, ...) \
	TEMP_FAILURE_RETRY(fcntl((fd), (flags), ##__VA_ARGS__))

