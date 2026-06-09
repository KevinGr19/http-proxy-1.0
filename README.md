# http-proxy-1.0
Basic `HTTP/1.0` proxy, compliant with RFC 1945.  
Self-study assignment from the book "Computer Science: A Programmer's Perspective, 3rd edition" (`proxylab`).  

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
