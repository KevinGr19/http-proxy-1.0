# http-proxy-1.0
Basic `HTTP/1.0` proxy for Linux, compliant with RFC 1945.  
Self-study assignment from the book "Computer Science: A Programmer's Perspective, 3rd edition" (`proxylab`).  

## Prerequisites
To build this project, you need the `gcc` compiler and the `make` utility.

## Usage
To run the proxy, build the executables with `make`, and run the proxy, specifying the port to listen on :
```
cd <path_to_dir>
make
./proxy <port>
```

> [!NOTE]
> The proxy needs to run on a Linux machine. Clients connecting to the proxy can run on any OS.

## Features
* Multi-threaded *(no thread pool)*
* Supported methods: `HEAD`, `GET`, `POST`, `PUT`, `DELETE`, `LINK`, `UNLINK`
* Request and response sanitization
* Response caching

## Behaviour
This proxy is RFC 1945 compliant. Otherwise, the behaviour changes depending on the request's version :
* `HTTP/0.9`: Discarded
* `HTTP/1.x`: Downgraded to `HTTP/1.0`, unknown headers are forwarded untouched
* `HTTP/2.0` or higher: Discarded
