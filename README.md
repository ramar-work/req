# req

`req` is a super simple request tester for Linux.   It is not nearly as robust or feature complete as something like <a href="https://github.com/wg/wrk">wrk</a>, but it still gets the job done.  If you have a need for heavy concurrency testing, you should probably look there first.
 
`req` currently only uses one dependency, cURL.  It also only builds on Linux at the moment.


## Usage

### Intro

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
$ ./req -X -u http://arch1.local:2000 -t 128 -S -P files
Making requests to http://arch1.local:2000
Test suite will use: 7812 kb of memory
Time elapsed: ~1m15s
REQ 0000 (0x507000000800) = Time: 47.62s, Size: 24235118b, Status: 200, - 
REQ 0001 (0x507000000870) = Time: 51.46s, Size: 24235118b, Status: 200, -
REQ 0002 (0x5070000008e0) = Time: 49.30s, Size: 24235118b, Status: 200, -
REQ 0003 (0x507000000950) = Time: 52.33s, Size: 24235118b, Status: 200, -
<snip for brevity>
REQ 0126 (0x507000003f20) = Time: 64.46s, Size: 24235118b, Status: 200, -
REQ 0127 (0x507000003f90) = Time: 73.65s, Size: 24235118b, Status: 200, -
```


### Errors

If there are any errors, they will be displayed in a manner similar to the following:
```
REQ    1 = Error: File transfer failed.
```

### Verification

`req` can also be used to stress-test throughput of large files.  

Consider the following, I have a file named `video-test.mp4` that comes out to somewhere
around 68mb.   I've uploaded it to my server, and plan to see how quickly the server can 
deliver this file to some arbitrary amount of simultaneously connected clients. If disk 
space were not an issue, I could do something like the following to verify that all 
requests were completing properly.

```
# Run a suite of tests and save files to a directory titled 'bacon'
$ ./req -X -u http://arch1.local:2000 -t 128 -S -P bacon

# Then compare against everything downloaded
find bacon/ -type f -name "file-*" | xargs md5sum > sums.txt

# Find the number of files that transferred incorrectly by comparing 
# against the checksum of the source file
grep -v `md5sum video-test.mp4 | awk '{ print $1 }'` sums.txt | wc -l
```

This works, but is a bit slow because the shell will need to run `md5sum` on EVERY FILE 
DOWNLOADED. It also will eat up a ton of disk space. (Just think about a 2gb video * 128 
for instance...)

Using the -V flag will allow `req` to run fetches for a file (or endpoint), but
discard the resultant data.  (NOTE: You can combine both the -V and -S flags should you
want to do so.)  `req` will check the bytes in the supplied file (the argument to -V) 
against what was received by the request.

Here's are the results of a test I ran against a virtual machine on my own system:

```
./req -X -u http://192.168.56.188:2000 -t 128 -V video-test.mp4
 
Making requests to [http://192.168.56.188:2000]
Test suite will use: 20312 kb of memory
Time elapsed: ~1m40s
128/128 requests completed
Fastest request: ~0m59s
Slowest request: ~1m39s
REQ 0000 (0x511000000680) = Time: 74.78s, Size: 70770286b, Status: 200, S
REQ 0001 (0x5110000007c0) = Time: 72.67s, Size: 70770286b, Status: 200, S
REQ 0002 (0x511000000900) = Time: 71.10s, Size: 70770286b, Status: 200, S
REQ 0003 (0x511000000a40) = Time: 81.92s, Size: 70770286b, Status: 200, S
<snip for brevity>
REQ 0126 (0x511000010300) = Time: 99.52s, Size: 70770286b, Status: 200, S
REQ 0127 (0x511000010440) = Time: 99.36s, Size: 70770286b, Status: 200, S

```

Notice the upper case 'S' on the far right?  This tells me that the bytes
I've received match up with the bytes in the file I supplied via -V.  We'll
get an 'F' if for some reason the files don't match.  And if -V is not specified
at all, we'll see a '-'.


### Options

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
-V, -verify <arg>           Verify that files transferred successfully by comparing against file <arg>.
```


## Building

There is no (and probably never will be a) package for this.  It's a pretty simple program and should compile on most any system that has cURL and an accompanying development library installed.

The supplied Makefile references `clang` as the default C compiler.  That can be overriden via the CC environment variable to whatever you like.



## Why Did I Write This? 

I needed something a little more reliable than `xargs` magic to get some server testing done.   It ended up being far simpler to just write a quick C program to get this done. 
