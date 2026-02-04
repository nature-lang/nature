# import time

Time-related functionality, including time representation, formatting, and operations

## fn unix

```
fn unix():i64
```

Returns the current Unix timestamp in seconds

## type time_t

```
type time_t = struct {
    i64 sec
    i64 nsec
    ptr<libc.tm> tm
}
```

Represents a point in time with second and nanosecond precision, including system time structure

### fn now

```
fn now():time_t
```

Returns the current time, initialized with system time

### fn time_t.timestamp

```
fn time_t.timestamp():i64
```

Returns the timestamp in seconds

### fn time_t.ms_timestamp

```
fn time_t.ms_timestamp():i64
```

Returns the timestamp in milliseconds

### fn time_t.ns_timestamp

```
fn time_t.ns_timestamp():i64
```

Returns the timestamp in nanoseconds

### fn time_t.datetime

```
fn time_t.datetime():string
```

Returns a formatted date-time string in "YYYY-MM-DD HH:MM:SS" format