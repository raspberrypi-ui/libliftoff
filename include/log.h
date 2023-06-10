#ifndef LOG_H
# define LOG_H

# include <libliftoff_rpi.h>

# ifdef __GNUC__
#  define _LIFTOFF_RPI_ATTRIB_PRINTF(start, end) \
   __attribute__((format(printf, start, end)))
# else
#  define _LIFTOFF_RPI_ATTRIB_PRINTF(start, end)
# endif

bool liftoff_rpi_log_has(enum liftoff_rpi_log_priority priority);
void liftoff_rpi_log(enum liftoff_rpi_log_priority priority, const char *format, ...)
_LIFTOFF_RPI_ATTRIB_PRINTF(2, 3);

void liftoff_rpi_log_errno(enum liftoff_rpi_log_priority priority, const char *msg);

#endif
