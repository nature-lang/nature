# import [net.dns](https://github.com/nature-lang/nature/blob/master/std/net/dns.n)

DNS resolution utilities for hostname lookup

## fn uv_dns_lookup

```
fn uv_dns_lookup(string host, [string] ips):void!
```

Low-level DNS lookup function using libuv backend

## fn lookup

```
fn lookup(string host):[string]!
```

Resolve hostname to IP addresses and return as string array

# import [net.tcp](https://github.com/nature-lang/nature/blob/master/std/net/tcp.n)

TCP networking for server and client connections

## fn uv_tcp_listen

```
fn uv_tcp_listen(ptr<server_t> s):void!
```

Low-level TCP server listening using libuv backend

## fn uv_tcp_connect

```
fn uv_tcp_connect(ptr<conn_t> conn, string ip, int port, int timeout):void!
```

Low-level TCP connection establishment using libuv backend

## fn uv_tcp_accept

```
fn uv_tcp_accept(ptr<server_t> server, ptr<conn_t> conn):void!
```

Low-level TCP connection acceptance using libuv backend

## fn listen

```
fn listen(string host):ptr<server_t>!
```

Create and start a TCP server listening on the specified host and port

## fn connect

```
fn connect(string host):ptr<conn_t>!
```

Establish a TCP connection to the specified host and port

## fn connect_timeout

```
fn connect_timeout(string host, int timeout):ptr<conn_t>!
```

Establish a TCP connection with specified timeout

## type server_t

```
type server_t = struct{
    string ip
    int port
    anyptr handle
    anyptr listen_co
    co_types.linkco_list_t waiters
    anyptr accept_list
    bool closed
}
```

TCP server instance managing listening socket and connections

### server_t.accept

```
fn server_t.accept():ptr<conn_t>!
```

Accept incoming TCP connection and return connection object

### server_t.close

```
fn server_t.close()
```

Close the TCP server and stop accepting connections

## type conn_t

```
type conn_t:types.connable = struct{
    anyptr conn
    bool closed
}
```

TCP connection instance implementing connable interface

### conn_t.read

```
fn conn_t.read([u8] buf):int!
```

Read data from TCP connection into buffer

### conn_t.write

```
fn conn_t.write([u8] buf):int!
```

Write data to TCP connection from buffer

### conn_t.close

```
fn conn_t.close()
```

Close the TCP connection

# import [net.tls](https://github.com/nature-lang/nature/blob/master/std/net/tls.n)

TLS/SSL secure networking for encrypted connections

## fn uv_tls_connect

```
fn uv_tls_connect(ptr<conn_t> conn, string ip, int port, int timeout):void!
```

Low-level TLS connection establishment using libuv backend

## fn connect

```
fn connect(string host):ptr<conn_t>!
```

Establish a TLS connection to the specified host and port

## fn connect_timeout

```
fn connect_timeout(string host, int timeout):ptr<conn_t>!
```

Establish a TLS connection with specified timeout

## type conn_t

```
type conn_t:types.connable = struct{
    anyptr conn
    bool closed
}
```

TLS connection instance implementing connable interface

### conn_t.read

```
fn conn_t.read([u8] buf):int!
```

Read data from TLS connection into buffer

### conn_t.write

```
fn conn_t.write([u8] buf):int!
```

Write data to TLS connection from buffer

### conn_t.close

```
fn conn_t.close()
```

Close the TLS connection

# import [net.udp](https://github.com/nature-lang/nature/blob/master/std/net/udp.n)

UDP networking for connectionless packet communication

## fn uv_udp_bind

```
fn uv_udp_bind(ptr<socket_t> s):void!
```

Low-level UDP socket binding using libuv backend

## fn uv_udp_recvfrom

```
fn uv_udp_recvfrom(ptr<socket_t> s, [u8] buf, rawptr<addr_t> addr):int!
```

Low-level UDP packet reception using libuv backend

## fn bind

```
fn bind(string host):ptr<socket_t>!
```

Create and bind a UDP socket to the specified host and port

## fn connect

```
fn connect(string host):ptr<conn_t>!
```

Create a UDP connection to the specified host and port

## type addr_t

```
type addr_t = struct{
    string ip
    int port
}
```

Network address containing IP and port information

## type socket_t

```
type socket_t = struct{
    addr_t addr
    anyptr handle
    anyptr bind_co
    anyptr data
    anyptr head
    anyptr tail
    anyptr packet_count
    anyptr buf
    u64 suggested_size
    anyptr write_buf
    anyptr read_buf
    bool recv_start
    bool closed
}
```

UDP socket instance managing packet communication

### socket_t.recvfrom

```
fn socket_t.recvfrom([u8] buf):(int, addr_t)!
```

Receive UDP packet and return data length and sender address

### socket_t.sendto

```
fn socket_t.sendto([u8] buf, addr_t addr):int!
```

Send UDP packet to specified address

### socket_t.connect

```
fn socket_t.connect(string host):ptr<conn_t>!
```

Create a connected UDP socket for communication with specific host

### socket_t.close

```
fn socket_t.close()
```

Close the UDP socket

## type conn_t

```
type conn_t:io.reader, io.writer = struct{
    ptr<socket_t> socket
    addr_t remote_addr
}
```

UDP connection instance implementing reader and writer interfaces

### conn_t.read

```
fn conn_t.read([u8] buf):int!
```

Read data from UDP connection into buffer

### conn_t.write

```
fn conn_t.write([u8] buf):int!
```

Write data to UDP connection from buffer

### conn_t.close

```
fn conn_t.close()
```

Close the UDP connection

# import [net.url](https://github.com/nature-lang/nature/blob/master/std/net/url.n)

URL parsing and manipulation utilities

## const SCHEME

```
const SCHEME = 1
```

URL parsing state constant for scheme component

## const AUTHORITY

```
const AUTHORITY = 2
```

URL parsing state constant for authority component

## const PATH

```
const PATH = 3
```

URL parsing state constant for path component

## const QUERY

```
const QUERY = 4
```

URL parsing state constant for query component

## const FRAGMENT

```
const FRAGMENT = 5
```

URL parsing state constant for fragment component

## fn parse

```
fn parse(string url):url_t!
```

Parse URL string into structured URL components

## fn parse_authority

```
fn parse_authority(rawptr<url_t> result, string authority)
```

Parse authority component of URL into hostname and port

## fn decode

```
fn decode(string s):string
```

Decode URL-encoded string to original form

## fn hex_to_int

```
fn hex_to_int(u8 ch):int
```

Convert hexadecimal character to integer value

## fn encode

```
fn encode(string s):string
```

Encode string to URL-encoded form

## fn int_to_hex

```
fn int_to_hex(int val):u8
```

Convert integer value to hexadecimal character

## type url_t

```
type url_t = struct{
    string url
    string scheme
    string authority
    string hostname
    int port
    string path
    string fragment
    string raw_query
}
```

Structured URL representation with parsed components

### url_t.query

```
fn url_t.query():query_t
```

Parse and return query parameters as key-value map

### url_t.request_uri

```
fn url_t.request_uri():string
```

Get request URI including path and query string

## type query_t

```
type query_t = {string:[string]}
```

Query parameters map with string keys and string array values

### query_t.get

```
fn query_t.get(string key):string
```

Get first value for specified query parameter key

# import [net.utils](https://github.com/nature-lang/nature/blob/master/std/net/utils.n)

Network utility functions for common operations

## fn split_host

```
fn split_host(string host):(string, int)!
```

Split host string into IP address and port number

# import [net.types](https://github.com/nature-lang/nature/blob/master/std/net/types.n)

Network type definitions and interfaces

## type connable

```
type connable:io.writer,io.reader = interface{}
```

Interface for network connections supporting read and write operations