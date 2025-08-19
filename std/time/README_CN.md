# import time

时间相关功能，包括时间表示、格式化和操作

## fn unix

```
fn unix():i64
```

返回当前的 Unix 时间戳（以秒为单位）

## type time_t

```
type time_t = struct {
    i64 sec
    i64 nsec
    rawptr<libc.tm> tm
}
```

表示具有秒和纳秒精度的时间点，包含系统时间结构

### fn now

```
fn now():time_t
```

返回当前时间，使用系统时间进行初始化

### fn time_t.timestamp

```
fn time_t.timestamp():i64
```

返回时间戳（以秒为单位）

### fn time_t.ms_timestamp

```
fn time_t.ms_timestamp():i64
```

返回时间戳（以毫秒为单位）

### fn time_t.ns_timestamp

```
fn time_t.ns_timestamp():i64
```

返回时间戳（以纳秒为单位）

### fn time_t.datetime

```
fn time_t.datetime():string
```

返回格式化的日期时间字符串，格式为 "YYYY-MM-DD HH:MM:SS"