
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

enum {DEBUG=0, INFO, WARN, ERROR};
inline void fine_log(int const type, char *restrict format, ...) {
	va_list args;
	va_start(args, format);
	FILE *stream = type>INFO? stderr : stdout;
	vfprintf(stream, format, args);
	if(format[strlen(format)-1] != '\n') fputc('\n', stream);
	va_end(args);
}

inline void fine_exit(char *restrict format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	if(format[strlen(format)-1] != '\n') fputc('\n', stderr);
	va_end(args);
	exit(EXIT_FAILURE);
}
