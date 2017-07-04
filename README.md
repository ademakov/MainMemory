MainMemory
==========

The MainMemory project is intended to provide

- a framework for network servers of all sorts;
- some specific implementations of such servers.

The purpose of the MainMemory project is akin to SGI's State Threads
Library (http://state-threads.sourceforge.net/). But MainMemory also
aims to take full advantage of modern multi-core systems with large
RAM sizes.

As the first application and testbed for the MainMemory framework
the project provides almost complete implementation of the memcached
protocol.

In the future it could also be used to implement any other protocol,
for instance, redis. Or something completely different like HTTP,
WebDAV, WebSocket protocols thus allowing to implement a caching HTTP
server, proxy, storage platform, or a pub-sub system.

# Status

The project has been slowly brewing as a personal research vehicle for
some time already. A number of different approaches has been tried and
abandoned. All this time it was run mostly against memslap and memtier
benchmarks.

Now it has matured enough to consider it ready for experimental use by a
wide audience. Hopefully in a short term it could be declared production
ready as well.

# Target Platforms

MainMemory is intended to run on x86/x86-64 boxes with Linux or any BSD
flavor OS including Mac OS/X. The key requirement is the availability of
either epoll or kqueue API in addition to the standard POSIX API.

However so far it has only been extensively tested on Linux and Mac OS/X.
And on a single FreeBSD instance running on a VM. Therefore any portability
reports and/or patches are welcome.

MainMemory includes a not-much-tested "generic architecture" port so there
is a slim chance it will run on any non-x86 platform easily enough. But most
likely it will take some extra work. In the future it is planed to have a
native ARM port too.

# Build

MainMemory relies on the venerable autotools suite to build. The build
procedure is as follows:

```
> ./bootstrap
> ./configure <config-options>
> make
```

This produces few libraries and a binary file named 'maind' located in the
'src' subdirectory. The 'maind' file could be used as a replacement for your
memcached.

## Single-Threaded and Multi-Threaded Builds

MainMemory can be built in one of the two configurations:

- single-threaded
- or multi-threaded (SMP).

For the former option, run the configure script as follows:

```
> ./configure --disable-smp
```

For the later option, run the configure script as follows:

```
> ./configure --enable-smp
```

## What Build to Use

The single-threaded version does not use the synchronization mechanisms of
the multi-threaded version. Therefore it performs better (with higher
throughput and smaller latency) until it hits the single-core execution
limits.

As a rule of thumb if the required throughput of the server is less than
about 100k requests per second then it might be beneficial to use the
single-threaded build. Otherwise the multi-threaded build is preferred.

## Development Builds

There are a few other configure options that might be useful during
development. The following options enable collection and output (in
somewhat cryptic format) of event loop and locking statistics:

```
> ./configure --enable-event-stats --enbale-lock-stats
```

The following options enable lots and lots of useless output (it
was helpful however in the early days of the project):

```
> ./configure --enable-debug --enbale-trace
```
