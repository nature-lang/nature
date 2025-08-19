# import [net.dns](https://github.com/nature-lang/nature/blob/master/std/net/dns.n)

DNS解析工具，用于主机名查找

## fn uv_dns_lookup

```
fn uv_dns_lookup(string host, [string] ips):void!
```

使用libuv后端的底层DNS查找函数

## fn lookup

```
fn lookup(string host):[string]!
```

将主机名解析为IP地址并返回字符串数组

# import [net.tcp](https://github.com/nature-lang/nature/blob/master/std/net/tcp.n)

TCP网络，用于服务器和客户端连接

## fn uv_tcp_listen

```
fn uv_tcp_listen(ptr<server_t> s):void!
```

使用libuv后端的底层TCP服务器监听

## fn uv_tcp_connect

```
fn uv_tcp_connect(ptr<conn_t> conn, string ip, int port, int timeout):void!
```

使用libuv后端的底层TCP连接建立

## fn uv_tcp_accept

```
fn uv_tcp_accept(ptr<server_t> server, ptr<conn_t> conn):void!
```

使用libuv后端的底层TCP连接接受

## fn listen

```
fn listen(string host):ptr<server_t>!
```

创建并启动在指定主机和端口上监听的TCP服务器

## fn connect

```
fn connect(string host):ptr<conn_t>!
```

建立到指定主机和端口的TCP连接

## fn connect_timeout

```
fn connect_timeout(string host, int timeout):ptr<conn_t>!
```

建立带有指定超时的TCP连接

## type server_t

```
type server_t = struct{
    string ip
    int port
    anyptr server_handle
    anyptr listen_co
    co_types.linkco_list_t waiters
    anyptr accept_list
    bool closed
}
```

TCP服务器实例，管理监听套接字和连接

### server_t.accept

```
fn server_t.accept():ptr<conn_t>!
```

接受传入的TCP连接并返回连接对象

### server_t.close

```
fn server_t.close()
```

关闭TCP服务器并停止接受连接

## type conn_t

```
type conn_t:types.connable = struct{
    anyptr conn
    bool closed
}
```

TCP连接实例，实现connable接口

### conn_t.read

```
fn conn_t.read([u8] buf):int!
```

从TCP连接读取数据到缓冲区

### conn_t.write

```
fn conn_t.write([u8] buf):int!
```

从缓冲区向TCP连接写入数据

### conn_t.close

```
fn conn_t.close()
```

关闭TCP连接

# import [net.tls](https://github.com/nature-lang/nature/blob/master/std/net/tls.n)

TLS/SSL安全网络，用于加密连接

## fn uv_tls_connect

```
fn uv_tls_connect(ptr<conn_t> conn, string ip, int port, int timeout):void!
```

使用libuv后端的底层TLS连接建立

## fn connect

```
fn connect(string host):ptr<conn_t>!
```

建立到指定主机和端口的TLS连接

## fn connect_timeout

```
fn connect_timeout(string host, int timeout):ptr<conn_t>!
```

建立带有指定超时的TLS连接

## type conn_t

```
type conn_t:types.connable = struct{
    anyptr conn
    bool closed
}
```

TLS连接实例，实现connable接口

### conn_t.read

```
fn conn_t.read([u8] buf):int!
```

从TLS连接读取数据到缓冲区

### conn_t.write

```
fn conn_t.write([u8] buf):int!
```

从缓冲区向TLS连接写入数据

### conn_t.close

```
fn conn_t.close()
```

关闭TLS连接

# import [net.udp](https://github.com/nature-lang/nature/blob/master/std/net/udp.n)

UDP网络，用于无连接数据包通信

## fn uv_udp_bind

```
fn uv_udp_bind(ptr<socket_t> s):void!
```

使用libuv后端的底层UDP套接字绑定

## fn uv_udp_recvfrom

```
fn uv_udp_recvfrom(ptr<socket_t> s, [u8] buf, rawptr<addr_t> addr):int!
```

使用libuv后端的底层UDP数据包接收

## fn bind

```
fn bind(string host):ptr<socket_t>!
```

创建并绑定UDP套接字到指定主机和端口

## fn connect

```
fn connect(string host):ptr<conn_t>!
```

创建到指定主机和端口的UDP连接

## type addr_t

```
type addr_t = struct{
    string ip
    int port
}
```

包含IP和端口信息的网络地址

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

UDP套接字实例，管理数据包通信

### socket_t.recvfrom

```
fn socket_t.recvfrom([u8] buf):(int, addr_t)!
```

接收UDP数据包并返回数据长度和发送者地址

### socket_t.sendto

```
fn socket_t.sendto([u8] buf, addr_t addr):int!
```

向指定地址发送UDP数据包

### socket_t.connect

```
fn socket_t.connect(string host):ptr<conn_t>!
```

创建连接的UDP套接字以与特定主机通信

### socket_t.close

```
fn socket_t.close()
```

关闭UDP套接字

## type conn_t

```
type conn_t:io.reader, io.writer = struct{
    ptr<socket_t> socket
    addr_t remote_addr
}
```

UDP连接实例，实现reader和writer接口

### conn_t.read

```
fn conn_t.read([u8] buf):int!
```

从UDP连接读取数据到缓冲区

### conn_t.write

```
fn conn_t.write([u8] buf):int!
```

从缓冲区向UDP连接写入数据

### conn_t.close

```
fn conn_t.close()
```

关闭UDP连接

# import [net.url](https://github.com/nature-lang/nature/blob/master/std/net/url.n)

URL解析和操作工具

## const SCHEME

```
const SCHEME = 1
```

URL解析状态常量，用于scheme组件

## const AUTHORITY

```
const AUTHORITY = 2
```

URL解析状态常量，用于authority组件

## const PATH

```
const PATH = 3
```

URL解析状态常量，用于path组件

## const QUERY

```
const QUERY = 4
```

URL解析状态常量，用于query组件

## const FRAGMENT

```
const FRAGMENT = 5
```

URL解析状态常量，用于fragment组件

## fn parse

```
fn parse(string url):url_t!
```

将URL字符串解析为结构化URL组件

## fn parse_authority

```
fn parse_authority(rawptr<url_t> result, string authority)
```

将URL的authority组件解析为主机名和端口

## fn decode

```
fn decode(string s):string
```

将URL编码的字符串解码为原始形式

## fn hex_to_int

```
fn hex_to_int(u8 ch):int
```

将十六进制字符转换为整数值

## fn encode

```
fn encode(string s):string
```

将字符串编码为URL编码形式

## fn int_to_hex

```
fn int_to_hex(int val):u8
```

将整数值转换为十六进制字符

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

带有解析组件的结构化URL表示

### url_t.query

```
fn url_t.query():query_t
```

解析并返回查询参数作为键值映射

### url_t.request_uri

```
fn url_t.request_uri():string
```

获取包含路径和查询字符串的请求URI

## type query_t

```
type query_t = {string:[string]}
```

查询参数映射，具有字符串键和字符串数组值

### query_t.get

```
fn query_t.get(string key):string
```

获取指定查询参数键的第一个值

# import [net.utils](https://github.com/nature-lang/nature/blob/master/std/net/utils.n)

网络工具函数，用于常见操作

## fn split_host

```
fn split_host(string host):(string, int)!
```

将主机字符串分割为IP地址和端口号

# import [net.types](https://github.com/nature-lang/nature/blob/master/std/net/types.n)

网络类型定义和接口

## type connable

```
type connable:io.writer,io.reader = interface{}
```

支持读写操作的网络连接接口