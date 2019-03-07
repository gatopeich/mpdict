# mpdict
A Multi-Process Dict for Python 3

* Based on Boost::interprocess library
* 20X faster than using Redis, with lower memory usage and no background daemon.
* 1 Million "set" op/s on i7@3.5GHz CPU

Designed to serve as and in-memory database for Python processes in a multi-worker service, allowing to scale at a lower CPU & memory cost.

## Installation

**$ sudo apt install g++ libboost-container-dev**

<...>

**$ python3 setup.py install --user**
```
running install
running build
running build_ext
building 'mpdict' extension
creating build/temp.linux-x86_64-3.6
x86_64-linux-gnu-gcc -pthread -DNDEBUG -g -fwrapv -O2 -Wall -g -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -fPIC -I/usr/include/python3.6m -c mpdict.cpp -o build/temp.linux-x86_64-3.6/mpdict.o -std=gnu++1z -Wall -Werror -mtune=native
creating build/lib.linux-x86_64-3.6
x86_64-linux-gnu-g++ -pthread -shared -Wl,-O1 -Wl,-Bsymbolic-functions -Wl,-Bsymbolic-functions -Wl,-z,relro -Wl,-Bsymbolic-functions -Wl,-z,relro -g -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 build/temp.linux-x86_64-3.6/mpdict.o -lstdc++ -lboost_container -lrt -o build/lib.linux-x86_64-3.6/mpdict.cpython-36m-x86_64-linux-gnu.so
running install_lib
copying build/lib.linux-x86_64-3.6/mpdict.cpython-36m-x86_64-linux-gnu.so -> /home/user/.local/lib/python3.6/site-packages
running install_egg_info
Writing /home/user/.local/lib/python3.6/site-packages/mpdict-0.1.egg-info
```

## Performance Check

(On AMD A8-4500M based laptop at ~2 GHz)

**Adding 1 Million items to a "mpdict" takes 3 seconds and uses 80 MB**
```
$ /usr/bin/time -v python3.7 mp_dict_test.py mpdict 3 1000000
Testing mpdict on 3 processes X 1e+06 items...
Elapsed 3.13951 seconds
d[999781]: ('Foreground0', 'Foreground') ('Foreground1', 'Foreground') ('Foreground10', 'Foreground') ('Foreground100', 'Foreground') ('Foreground1000', 'Foreground') ('Foreground10000', 'Foreground') ('Foreground100000', 'Foreground') ('Foreground100001', 'Foreground') ('Foreground100002', 'Foreground')
	Command being timed: "python3.7 mp_dict_test.py mpdict 3 1000000"
	User time (seconds): 4.40
	System time (seconds): 0.57
	Percent of CPU this job got: 153%
	Elapsed (wall clock) time (h:mm:ss or m:ss): 0:03.25
	Average shared text size (kbytes): 0
	Average unshared data size (kbytes): 0
	Average stack size (kbytes): 0
	Average total size (kbytes): 0
	Maximum resident set size (kbytes): 80632
	Average resident set size (kbytes): 0
	Major (requiring I/O) page faults: 0
	Minor (reclaiming a frame) page faults: 45307
	Voluntary context switches: 27576
	Involuntary context switches: 200
	Swaps: 0
	File system inputs: 0
	File system outputs: 0
	Socket messages sent: 0
	Socket messages received: 0
	Signals delivered: 0
	Page size (bytes): 4096
	Exit status: 0
```

### Comparison with a Redis based dictionary

**Starting redis-server with some optimization**
```
$ sudo sysctl vm.overcommit_memory=1
vm.overcommit_memory = 1
$ echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
never
$ redis-server --port 0 --unixsocket /tmp/testRedisDict
27237:C 07 Mar 18:31:39.779 # oO0OoO0OoO0Oo Redis is starting oO0OoO0OoO0Oo
27237:C 07 Mar 18:31:39.779 # Redis version=4.0.9, bits=64, commit=00000000, modified=0, pid=27237, just started
...
27237:M 07 Mar 18:31:39.782 * The server is now ready to accept connections at /tmp/testRedisDict
```

**Add 100,000 items to Redis-based dict from 3 processes**
```
$ /usr/bin/time -v python3.7 mp_dict_test.py redis
Testing redis on 3 processes X 100000 items...
Elapsed 7.81296 seconds
d[99999]: (b'Foreground12887', b'Foreground') (b'Foreground23199', b'Foreground') (b'Foreground9280', b'Foreground') (b'b25846', b'bbbbbbbbbb') (b'Foreground5959', b'Foreground') (b'a31296', b'aaaaaaaaaa') (b'a31122', b'aaaaaaaaaa') (b'a29595', b'aaaaaaaaaa') (b'a24278', b'aaaaaaaaaa')
	Command being timed: "python3.7 mp_dict_test.py redis"
	User time (seconds): 9.67
	System time (seconds): 1.84
	Percent of CPU this job got: 126%
	Elapsed (wall clock) time (h:mm:ss or m:ss): 0:09.06
	Average shared text size (kbytes): 0
	Average unshared data size (kbytes): 0
	Average stack size (kbytes): 0
	Average total size (kbytes): 0
	Maximum resident set size (kbytes): 20496
	Average resident set size (kbytes): 0
	Major (requiring I/O) page faults: 0
	Minor (reclaiming a frame) page faults: 6581
	Voluntary context switches: 131071
	Involuntary context switches: 363
	Swaps: 0
	File system inputs: 0
	File system outputs: 0
	Socket messages sent: 0
	Socket messages received: 0
	Signals delivered: 0
	Page size (bytes): 4096
	Exit status: 0
```

Note that this takes more than double the time to process 10X less items, and uses ~4X more memory per item
