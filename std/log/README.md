# [import log](https://github.com/nature-lang/nature/blob/master/std/log/main.n)

Logging library providing structured logging functionality with different log levels, colored output, and flexible configuration options.

## const TRACE

```
const TRACE = 0
```

Trace log level constant for detailed diagnostic information.

## const DEBUG

```
const DEBUG = 1
```

Debug log level constant for debugging information during development.

## const INFO

```
const INFO = 2
```

Info log level constant for general informational messages (default level).

## const WARN

```
const WARN = 3
```

Warning log level constant for warning messages that indicate potential issues.

## const ERROR

```
const ERROR = 4
```

Error log level constant for error messages that indicate failures.

## const FATAL

```
const FATAL = 5
```

Fatal log level constant for fatal errors that cause program termination.

## var DEFAULT_LEVEL

```
var DEFAULT_LEVEL = INFO
```

Default logging level applied to new loggers and the global default logger.

## fn trace

```
fn trace(string format, ...[any] args):void!
```

Log a trace-level message using the default logger.

## fn debug

```
fn debug(string format, ...[any] args):void!
```

Log a debug-level message using the default logger.

## fn info

```
fn info(string format, ...[any] args):void!
```

Log an info-level message using the default logger.

## fn warn

```
fn warn(string format, ...[any] args):void!
```

Log a warning-level message using the default logger.

## fn error

```
fn error(string format, ...[any] args):void!
```

Log an error-level message using the default logger.

## fn fatal

```
fn fatal(string format, ...[any] args):void!
```

Log a fatal-level message using the default logger and exit the program.

## fn trace_at

```
fn trace_at(string file, int line, string format, ...[any] args):void!
```

Log a trace-level message with file and line information.

## fn debug_at

```
fn debug_at(string file, int line, string format, ...[any] args):void!
```

Log a debug-level message with file and line information.

## fn info_at

```
fn info_at(string file, int line, string format, ...[any] args):void!
```

Log an info-level message with file and line information.

## fn warn_at

```
fn warn_at(string file, int line, string format, ...[any] args):void!
```

Log a warning-level message with file and line information.

## fn error_at

```
fn error_at(string file, int line, string format, ...[any] args):void!
```

Log an error-level message with file and line information.

## fn fatal_at

```
fn fatal_at(string file, int line, string format, ...[any] args):void!
```

Log a fatal-level message with file and line information and exit the program.

## fn set_level

```
fn set_level(u8 level):void
```

Set the global logging level. Messages below this level will be ignored.

## fn set_output_file

```
fn set_output_file(string file_path):void!
```

Set the output file for the default logger. Logs will be written to this file instead of stdout.

## fn set_color

```
fn set_color(bool show):void
```

Enable or disable colored output for the default logger.

## fn set_time_display

```
fn set_time_display(bool show):void
```

Enable or disable timestamp display in log messages.

## fn set_file_display

```
fn set_file_display(bool show):void
```

Enable or disable file and line information display in log messages.

## fn new_logger

```
fn new_logger(u8 level):logger_t
```

Create a new logger instance with the specified log level.

## type logger_t

```
type logger_t = struct {
    u8 level
    bool show_color
    bool show_time
    bool show_file
    ptr<fs.file_t> output_file
}
```

Logger represents a configurable logging instance with customizable output formatting and destination.

### fn set_output_file

```
fn logger_t.set_output_file(string file_path):logger_t!
```

Set the output file for this logger instance. Returns an error if the file cannot be opened.

### fn set_level

```
fn logger_t.set_level(u8 level):logger_t
```

Set the logging level for this logger instance.

### fn set_color

```
fn logger_t.set_color(bool show):logger_t
```

Enable or disable colored output for this logger instance.

### fn set_time

```
fn logger_t.set_time(bool show):logger_t
```

Enable or disable timestamp display for this logger instance.

### fn set_file_info

```
fn logger_t.set_file_info(bool show):logger_t
```

Enable or disable file and line information display for this logger instance.

### fn log

```
fn logger_t.log(u8 level, string file, int line, string format, ...[any] args):void!
```

Core logging method that formats and outputs a log message with the specified level, file information, and formatted message.

## Examples

### Basic Usage

```nature
import log

// Simple logging
log.info('Application started')
log.warn('This is a warning: %s', 'low disk space')
log.error('Failed to connect to database: %s', error_message)

// Set global log level
log.set_level(log.DEBUG)
log.debug('Debug information: %d', debug_value)
```

### Custom Logger

```nature
import log

// Create custom logger
var logger = log.new_logger(log.WARN)
logger.set_color(false)
logger.set_output_file('/var/log/app.log')

// Use custom logger
logger.log(log.ERROR, 'main.n', 42, 'Custom error: %s', error_msg)
```

### Configuration

```nature
import log

// Configure global logger
log.set_level(log.DEBUG)
log.set_output_file('/tmp/app.log')
log.set_color(true)
log.set_time_display(true)
log.set_file_display(false)

// Now all log calls use these settings
log.info('Configured logging')
```