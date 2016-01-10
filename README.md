MainMemory
===========

MainMemory is a light-weight framework for networked servers of all sorts.
However efficient handling of in-memory workloads is its primary priority.
As its first application MainMemory provides almost complete implementation
of the memcached protocol.

Potentially it could be used to implement any other protcol, for instance,
redis protocol. Or something completely different like HTTP protocol thus
allowing to implement a caching HTTP server.

The purpose of the MainMemory project is a bit akin to SGI's State Threads
Library (http://state-threads.sourceforge.net/). But MainMemory also aims
to take full advantage of modern multi-core systems with larger RAM sizes.

# Target Platforms

MainMemory intends to run on x86/x86-64 boxes with Linux or any BSD flavor
OS including Mac OS/X. The key system requirement is the availability of
either epoll or kqueue API in addition to the standard POSIX API.

However so far it has only been extensively tested on Linux and Mac OS/X.
And on a single FreeBSD instance running on a VM. Therefore any reports
and/or patches for other platforms are welcome.

As there is a not-yet-tested "generic architecture" port there is a slim
chance MainMemory will run on any non-x86 platform too. But most likely
it will take some extra work yet. In the future it is planed to have a
native ARM port too,

# Build

MainMemory relies on the venerable autotools suite for builds. The build
procedure is as follows:

```
> ./bootstrap
> ./configure <config-options>
> make
```

The resulting binary file is named 'mmem' and is located in the 'src'
subdirectory.

## Single-Threaded and Multi-Threaded Builds

MainMemory can be built in one of the two configurations:

- single-threaded
- or multi-threaded (SMT).

For the former option, run the configure script as follows:

```
> ./configure
```

For the later option, run the configure script as follows:

```
> ./configure --enable-smp
```

## What Build to Use

The single-threaded version does not use the synchronization mechanisms of the
multi-threaded version. Therefore it performs better (with higher throughput
and smaller latency) until it hits the single-core execution limits.

As a rule of thumb if the required throughput of the server is less than about
100k requests per second then it might be benefitical to use the single-threaded
build. Otherwise a multi-threaded build should be used.
