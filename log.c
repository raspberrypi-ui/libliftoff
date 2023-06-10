#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

static enum liftoff_rpi_log_priority log_priority = LIFTOFF_RPI_ERROR;

static void
log_stderr(enum liftoff_rpi_log_priority priority, const char *fmt, va_list args)
{
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
}

static liftoff_rpi_log_handler log_handler = log_stderr;

void
liftoff_rpi_log_priority_set(enum liftoff_rpi_log_priority priority)
{
   log_priority = priority;
}

void
liftoff_rpi_log_handler_set(liftoff_rpi_log_handler handler)
{
   if (handler)
     log_handler = handler;
   else
     log_handler = log_stderr;
}

bool
liftoff_rpi_log_has(enum liftoff_rpi_log_priority priority)
{
   return priority <= log_priority;
}

void
liftoff_rpi_log(enum liftoff_rpi_log_priority priority, const char *fmt, ...)
{
   va_list args;

   if (!liftoff_rpi_log_has(priority)) return;

   va_start(args, fmt);
   log_handler(priority, fmt, args);
   va_end(args);
}

void
liftoff_rpi_log_errno(enum liftoff_rpi_log_priority priority, const char *msg)
{
   /* Ensure errno is still set to its original value when we return */
   int prev_errno = errno;
   liftoff_rpi_log(priority, "%s: %s", msg, strerror(errno));
   errno = prev_errno;
}
