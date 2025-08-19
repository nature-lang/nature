# import [http](https://github.com/nature-lang/nature/tree/master/std/http/main.n)

HTTP server and client library for building web applications and making HTTP requests.

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
type callback_fn = fn(request_t, ptr<response_t>):void!
```

Function type for HTTP request handlers.

## fn server

```
fn server():ptr<server_t>
```

Create a new HTTP server instance.

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

HTTP server implementation.

### server_t.get

```
fn server_t.get(string path, callback_fn callback)
```

Register a GET route handler.

### server_t.post

```
fn server_t.post(string path, callback_fn callback)
```

Register a POST route handler.

### server_t.put

```
fn server_t.put(string path, callback_fn callback)
```

Register a PUT route handler.

### server_t.delete

```
fn server_t.delete(string path, callback_fn callback)
```

Register a DELETE route handler.

### server_t.listen

```
fn server_t.listen(int port):void!
```

Start the HTTP server on specified port.

### server_t.close

```
fn server_t.close()
```

Close the HTTP server.

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

HTTP request representation.

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

HTTP response representation.

### response_t.send

```
fn response_t.send(string msg)
```

Send response with specified message.

# import [http.client](https://github.com/nature-lang/nature/tree/master/std/http/client.n)

HTTP client for making HTTP requests.

## fn get

```
fn get(string url, config_t c):ptr<response_t>!
```

Make a GET request.

## fn post

```
fn post(string url, string body, config_t c):ptr<response_t>!
```

Make a POST request.

## type multipart_t

```
type multipart_t = struct{
    string boundary
    [multipart_item_t] items
}
```

Multipart form data builder.

### multipart_t.text

```
fn multipart_t.text(string name, string value):ptr<multipart_t>
```

Add text field to multipart form.

### multipart_t.file

```
fn multipart_t.file(string name, string filename, string data):ptr<multipart_t>
```

Add file field to multipart form.

### multipart_t_new

```
fn multipart_t_new():ptr<multipart_t>
```

Create a new multipart form data builder.

## type multipart_item_t

```
type multipart_item_t = struct{
    string name
    string filename 
    string value
    string content_type    
}
```

Multipart form data item.

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

HTTP client request.

### new

```
fn new():ptr<request_t>
```

Create a new HTTP client request.

### request_t.get

```
fn request_t.get(string url):ptr<request_t>
```

Set request method to GET.

### request_t.post

```
fn request_t.post(string url):ptr<request_t>
```

Set request method to POST.

### request_t.put

```
fn request_t.put(string url):ptr<request_t>
```

Set request method to PUT.

### request_t.delete

```
fn request_t.delete(string url):ptr<request_t>
```

Set request method to DELETE.

### request_t.timeout

```
fn request_t.timeout(int timeout):ptr<request_t>
```

Set request timeout in milliseconds.

### request_t.header

```
fn request_t.header(string key, string value):ptr<request_t>
```

Add request header.

### request_t.content

```
fn request_t.content(string content):ptr<request_t>
```

Set request body content.

### request_t.json

```
fn request_t.json(any b):ptr<request_t>!
```

Set request body as JSON.

### request_t.form

```
fn request_t.form({string:string} data):ptr<request_t>
```

Set request body as form data.

### request_t.send

```
fn request_t.send():ptr<response_t>!
```

Send the HTTP request.

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
    ptr<buf.reader<types.connable>> buf_conn
}
```

HTTP client response.

### response_t.text

```
fn response_t.text():string!
```

Get response body as text.