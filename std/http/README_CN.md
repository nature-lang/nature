# import [http](https://github.com/nature-lang/nature/tree/master/std/http/main.n)

HTTP 服务器和客户端库，用于构建 Web 应用程序和发送 HTTP 请求。

## const HTTP_DELETE

```
const HTTP_DELETE = 0
```

## const HTTP_GET

```
const HTTP_GET = 1
```

## const HTTP_POST

```
const HTTP_POST = 3
```

## const HTTP_PUT

```
const HTTP_PUT = 4
```

## const ROUTES_GET

```
const ROUTES_GET = 0
```

## const ROUTES_POST

```
const ROUTES_POST = 1
```

## const ROUTES_PUT

```
const ROUTES_PUT = 2
```

## const ROUTES_DELETE

```
const ROUTES_DELETE = 3
```

## type callback_fn

```
type callback_fn = fn(request_t, ref<response_t>):void!
```

HTTP 请求处理函数类型。

## fn server

```
fn server():ref<server_t>
```

创建新的 HTTP 服务器实例。

## type server_t

```
type server_t = struct{
    fn():void! handler_fn
    string addr
    int port
    [{string:callback_fn};8] routers
    anyptr uv_server_handler
    anyptr listen_co
}
```

HTTP 服务器实现。

### server_t.get

```
fn server_t.get(string path, callback_fn callback)
```

注册 GET 路由处理器。

### server_t.post

```
fn server_t.post(string path, callback_fn callback)
```

注册 POST 路由处理器。

### server_t.put

```
fn server_t.put(string path, callback_fn callback)
```

注册 PUT 路由处理器。

### server_t.delete

```
fn server_t.delete(string path, callback_fn callback)
```

注册 DELETE 路由处理器。

### server_t.listen

```
fn server_t.listen(int port):void!
```

在指定端口启动 HTTP 服务器。

### server_t.close

```
fn server_t.close()
```

关闭 HTTP 服务器。

## type request_t

```
type request_t = struct{
    u8 method
    {string:string} headers
    int length
    string host
    string url
    string path
    string query
    string body
}
```

HTTP 请求表示。

## type response_t

```
type response_t = struct{
    string version
    {string:string} headers
    int status
    string message
    int length
    string body
    string content_type
    string charset
}
```

HTTP 响应表示。

### response_t.send

```
fn response_t.send(string msg)
```

发送带有指定消息的响应。

# import [http.client](https://github.com/nature-lang/nature/tree/master/std/http/client.n)

用于发送 HTTP 请求的 HTTP 客户端。

## fn get

```
fn get(string url, config_t c):ref<response_t>!
```

发送 GET 请求。

## fn post

```
fn post(string url, string body, config_t c):ref<response_t>!
```

发送 POST 请求。

## type multipart_t

```
type multipart_t = struct{
    string boundary
    [multipart_item_t] items
}
```

多部分表单数据构建器。

### multipart_t.text

```
fn multipart_t.text(string name, string value):ref<multipart_t>
```

向多部分表单添加文本字段。

### multipart_t.file

```
fn multipart_t.file(string name, string filename, string data):ref<multipart_t>
```

向多部分表单添加文件字段。

### multipart_t_new

```
fn multipart_t_new():ref<multipart_t>
```

创建新的多部分表单数据构建器。

## type multipart_item_t

```
type multipart_item_t = struct{
    string name
    string filename 
    string value
    string content_type    
}
```

多部分表单数据项。

## type request_t

```
type request_t = struct{
    string method
    {string:string} headers
    int timeout
    string body
    string url
    url.url_t u
    string remote_ip
    int remote_port
    string version
}
```

HTTP 客户端请求。

### new

```
fn new():ref<request_t>
```

创建新的 HTTP 客户端请求。

### request_t.get

```
fn request_t.get(string url):ref<request_t>
```

设置请求方法为 GET。

### request_t.post

```
fn request_t.post(string url):ref<request_t>
```

设置请求方法为 POST。

### request_t.put

```
fn request_t.put(string url):ref<request_t>
```

设置请求方法为 PUT。

### request_t.delete

```
fn request_t.delete(string url):ref<request_t>
```

设置请求方法为 DELETE。

### request_t.timeout

```
fn request_t.timeout(int timeout):ref<request_t>
```

设置请求超时时间（毫秒）。

### request_t.header

```
fn request_t.header(string key, string value):ref<request_t>
```

添加请求头。

### request_t.content

```
fn request_t.content(string content):ref<request_t>
```

设置请求体内容。

### request_t.json

```
fn request_t.json(any b):ref<request_t>!
```

设置请求体为 JSON 格式。

### request_t.form

```
fn request_t.form({string:string} data):ref<request_t>
```

设置请求体为表单数据。

### request_t.send

```
fn request_t.send():ref<response_t>!
```

发送 HTTP 请求。

## type response_t

```
type response_t = struct{
    string version
    {string:string} headers
    int status
    string message
    int length
    string body
    string content_type
    string charset
    bool body_read
    ref<buf.reader<types.connable>> buf_conn
}
```

HTTP 客户端响应。

### response_t.text

```
fn response_t.text():string!
```

获取响应体文本。