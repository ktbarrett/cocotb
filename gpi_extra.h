#ifndef GPI_EXTRA_H
#define GPI_EXTRA_H

#include <stdarg.h>

#include "./gpi.h"

/* This file contains functionality provided by cocotb's GPI implementation that
 * isn't part of the GPI */

/** Check if there is a registered GPI implementation.
 *
 * Useful for checking if a simulator is running.
 *
 * @return `1` if there is a registered GPI implementation, `0` otherwise.
 */
GPI_EXPORT bool gpi_has_registered_impl(void);

/** Register a callback to run just before the GPI terminates.
 *
 * @param cb        Callback function pointer.
 * @param cb_data   Pointer to user data to be passed to callback function.
 * @return          Handle to callback object.
 */
GPI_EXPORT int gpi_register_finalize_callback(void (*cb)(void *),
                                              void *cb_data);

/** @defgroup Logging GPI Logging Dependency Injection
 * These functions are for registering logging implementations into the GPI for
 * its logging.
 * @{
 */

/** Named logging level
 *
 *  The native logger only logs level names at these log level values.
 *  They were specifically chosen to align with the default level values in the
 *  Python logging module. Implementers of custom loggers should emit human
 *  readable level names for these value, but may support other values.
 */
typedef enum {
    GPI_NOTSET = 0,  ///< Lets the parent logger in the hierarchy decide the
                     ///< effective log level. By default this behaves like
                     ///< `INFO`.
    GPI_TRACE = 5,   ///< Prints `TRACE` by default. Information about execution
                     ///< of simulator callbacks and Python/simulator contexts.
    GPI_DEBUG = 10,  ///< Prints `DEBUG` by default. Verbose information, useful
                     ///< for debugging.
    GPI_INFO = 20,   ///< Prints `INFO` by default. Information about major
                     ///< events in the current program.
    GPI_WARNING = 30,  ///< Prints `WARN` by default. Encountered a recoverable
                       ///< bug, or information about surprising behavior.
    GPI_ERROR = 40,    ///< Prints `ERROR` by default. An unrecoverable error.
    GPI_CRITICAL = 50  ///< Prints `CRITICAL` by default. An unrecoverable
                       ///< error, to be followed by immediate simulator
                       ///< shutdown.
} gpi_log_level;

/** Type of a logger handler function.
 * @param userdata  Private implementation data registered with this function
 * @param name      Name of the logger
 * @param level     Level at which to log the message
 * @param pathname  Name of the file where the call site is located
 * @param funcname  Name of the function where the call site is located
 * @param lineno    Line number of the call site
 * @param msg       The message to log, uses C-sprintf-style format specifier
 * @param args      Additional arguments; formatted and inserted in message
 *                  according to format specifier in msg argument
 */
typedef void (*gpi_log_handler_ftype)(void *userdata, const char *name,
                                      gpi_log_level level, const char *pathname,
                                      const char *funcname, long lineno,
                                      const char *msg, va_list args);

/** Retrieve the current log handler.
 * @param handler   Location to return current log handler function.
 * @param userdata  Location to return log handler userdata.
 */
GPI_EXPORT void gpi_get_log_handler(gpi_log_handler_ftype *handler,
                                    void **userdata);

/** Set custom log handler
 * @param handler   Logger handler function.
 * @param userdata  Data passed to the above functions.
 */
GPI_EXPORT void gpi_set_log_handler(gpi_log_handler_ftype handler,
                                    void *userdata);

/** Set minimum logging level of the native logger.
 *
 * If a logging request occurs where the logging level is lower than the level
 * set by this function, it is not logged. Only affects the native logger.
 * @param level     Logging level
 * @return          Previous logging level
 */
GPI_EXPORT int gpi_native_logger_set_level(gpi_log_level level);

/** @} */  // End of group Logging

#endif /* GPI_EXTRA_H */
