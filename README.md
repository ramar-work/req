# req

`req` is a super simple request tester for Linux.   It is not nearly as robust or feature complete as something like <a href="https://github.com/wg/wrk">wrk</a>, but it still gets the job done.  If you have a need for heavy concurrency testing, you should probably look there first.
 
`req` currently only uses one dependency, cURL.  It also only builds on Linux at the moment.


## Usage

Running `req` is almost identical to doing something like:

```
for n in `seq 1 128`; do
curl http://the-site-you-want-to-test.com &
done
```

The key here is that `req` will allow you to see more information about the request, such as:
- the HTTP status of the request
- the total received payload size
- the total time it took for the request to complete


The following command would be equivalent to the cURL invocation above: 

```
./req -t 128 -u http://the-site-you-want-to-test.com
```


At its completion, `req` will show you a nicely formatted menu.  Here's a test I ran earlier against a virtual machine.

```
$ ./req -X -u http://arch1.local:2000 -t 128 -S -P filesTest suite will use: 7812 kb of memory
Making requests to http://arch1.local:2000
Time elapsed: ~1m15s
REQ 0000 (0x507000000800) = Time: 47.62s, Size: 24235118b, Status: 200, Url: http://arch1.local:2000
REQ 0001 (0x507000000870) = Time: 51.46s, Size: 24235118b, Status: 200, Url: http://arch1.local:2000
REQ 0002 (0x5070000008e0) = Time: 49.30s, Size: 24235118b, Status: 200, Url: http://arch1.local:2000
REQ 0003 (0x507000000950) = Time: 52.33s, Size: 24235118b, Status: 200, Url: http://arch1.local:2000
REQ 0004 (0x5070000009c0) = Time: 54.39s, Size: 24235118b, Status: 200, Url: http://arch1.local:2000
<snip for brevity>
REQ 0126 (0x507000003f20) = Time: 64.46s, Size: 24235118b, Status: 200, Url: http://arch1.local:2000
REQ 0127 (0x507000003f90) = Time: 73.65s, Size: 24235118b, Status: 200, Url: http://arch1.local:2000
```

If there are any errors, they will be displayed in a manner similar to the following:
```
REQ    1 = Error: File transfer failed.
```


A quick list of `req`'s options is as follows:

```
Usage: ./req
-F, -file <arg>             Use file <arg> as the comparison file.
-S, -save                   Save the payload with a sensible suffix.
-P, -path <arg>             Define an alternate path to save files (default is the current directory).
-u, -url <arg>              Fetch URL <arg>.
-X, -stats                  Display statistics for the current system's memory usage.
-t, -threads <arg>          Start <arg> threads (current max 9999)
-h, -help                   Show help and quit.
```


## Building

There is no (and probably never will be a) package for this.  It's incredibly and should compile on most any system that has cURL and an accompanying development library installed.

The Makefile here references `clang` as the default C compiler.  Though that can be overriden via the CC environment variable.



## Why Did I Write This? 

I needed something a little more reliable than `xargs` magic to get some server testing done.   It ended up being far simpler to just write a quick C program to get this done than trying to muck about that...
