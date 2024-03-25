# Parker

Lightweight packaging tool and container runtime, a single command packages the working directory into an executable file.

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230922113619.png)


The example demonstrates a C language-based IP resolution service (`gcc -o ipservice`), dependent on the ipdb resource file.

Use Parker to compress the executable file `ipservice` and its dependent asset into a new executable file named `ipservice-c`.

When `ipservice-c` is run on the target machine, it will create a lightweight container environment to operate the original `ipservice` service.

## ⚙️ Installation

Download and unpack the Parker installation package from [github releases](https://github.com/weiwenhao/parker/releases). It's recommended to move the unzipped parker folder to `/usr/local/` and add the `/usr/local/parker/bin` directory to your system's environment variables.

```
> parker --version
1.0.1
```

## 📦 Usage

`cd` to your working directory and run `parker :target`. This command packages the `:target` along with the current working directory into an executable file named `:target-c`. Once packaged, move this executable file to your target machine and run it.

```
> cd :workdir && parker :target
```

#### Example

The above example of packaging the executable file and resource file is a **standard usage** example. However, there are non-standard usage scenarios as well, such as with a server written in python3.11:

```
> tree .
├── bar.png
├── foo.txt
├── python # cp /usr/bin/python3.11 ./
└── server.py
```

Content of server.py:

```python
from http.server import SimpleHTTPRequestHandler, HTTPServer

def run():
    print("listen on http://127.0.0.1:8000")
    server_address = ('127.0.0.1', 8000)
    httpd = HTTPServer(server_address, SimpleHTTPRequestHandler)
    httpd.serve_forever()

run()
```

When you `cd` to the working directory and run `parker python`, you'll receive a `python-c` file. This is the packaged executable file. Upload it to the target machine and run it.

```
> parker python
python-c
├── server.py
├── python
├── foo.txt
└── bar.png
🍻 parker successful

------------------------------------------------------------------------ move pyhon-c to target
> tree .
.
└── python-c

0 directories, 1 file

------------------------------------------------------------------------ run python-c
> ./python-c server.py
listen on http://127.0.0.1:8000
```

Here, `python-c` passes arguments to the python process.

> ❗️ Parker does not solve python's dynamic compilation issues.

## 🚢 Runtime

`python-c` is a lightweight container runtime created by Parker. It's a statically compiled executable file. When executed, it uses the linux namespace to create an isolated environment and then unpacks the working directory to run the target python.

`python-c` monitors the python main process during its execution. If the python process stops or encounters an error, `python-c` will utilize cgroup to clean up the container environment and kill all of python's child processes.

All arguments and signals passed to `python-c` will be relayed directly to the python process.

## 🐧 Dependencies

The container runtime depends on cgroup and namespace, requiring a linux kernel version higher than 2.6.24. Also, cgroup should be correctly mounted. Check for the existence of either `/sys/fs/cgroup/cgroup.controllers` or `/sys/fs/cgroup/freezer` directories.

Tested environments: ubuntu:22 / ubuntu:20

## 🛠️ Make build

The source code is developed in the [nature](https://github.com/nature-lang/nature) programming language. The nature compiler version needs to be >= 0.4.0. After installation, execute `make amd64 && make install` in the source code directory to install to the /usr/local/parker directory.

> Nature currently primarily supports amd64 builds. Executables built by nature have a smaller size and higher efficiency. For other architectures, the main repository provides a golang version.

## 🎉 Thanks

[nature](https://github.com/nature-lang/nature) is a modern system-level programming language and compiler. It works hand in hand with C for high-performance and efficient development.

The community-available version of nature is about to be released. Early experiences and feedback are welcome. You're also invited to contribute to the standard library; all contributions will be merged into the main repository.

## 🪶 License

This project is open-sourced software licensed under the [MIT license](https://opensource.org/licenses/MIT).

Copyright (c) 2020-2023 WEIWENHAO, all rights reserved.
