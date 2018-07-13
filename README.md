# Overview
In this repository you will find a linux utility, written in c++, for sending an intense burst of pings to a target IP address, and then computing the round-trip-time (RTT) statistics.


## ping vs burstPing
The diffrence between the classic ping utility (which comes with linux) and burstPing is the burstPing is asynchronous: The ping utility sends an ICMP Echo Request, and then waits for an ICMP Echo Response before sending the next request. In contrast, burstPing sends the ICMP Echo Requests back-to-back on one thread, and listens for ICMP Echo Replies on another thread. By doing so, burstPing can capture how well the network reponsds to intense traffic, and gets a better statistic on the RTT timings. The figure below shows that the RTTs in a burst of pings can capture the true RTT better then a single ping sent periodically (after receiving a response). Moreover, burstPing measures times with nanosecond resolution (timespec) as opposed to the microsecond resolution (timeval) used in ping. To avoid clock inaccuracies, times are retrieved using clock_gettime(CLOCK_MONOTONIC, &timespec_o) from the time.h library.

![The distribution of each sequential ping's RTT, when sending a burst of 50 pings back-to-back. The data was collected over 1500 trials with a direct host-to-host Ethernet connection.](https://raw.githubusercontent.com/ymirsky/burstPing/master/BurstStat.png)


## burstPing features
* Send a burst of pings to a target IP address
* Measures RTTs with nanosecond resolution
* Configure the packet tranmission interval or let burstPing find it for you (~0.5*meanRTT to avoid packet loss) 
* Configure the payload size: 0-1500 Bytes
* Configure the burst size (number of pings)
* Save the transmission, reception, and RTT times directly to a file


# Installation
Provided in the repo is the Qt project which can be compiled using qmake. To install qmake in Ubuntu, run the following in the terminal:

```
sudo apt-get install qt4-qmake
sudo apt-get install libqt4-dev
```

To compile burstPing, run the following from the source directory:

```
qmake burstPing.pro
make
```

# Usage

After compiling burstPing, you can execute burstPing as you would execute the regular ping utility. Note, you must run burstPing with sudo since Linux only allows root users to send ICMP requests.

To run burstPing execute the following command ftom the terminal:
```
sudo ./burstPing IP
```
where IP is the target IP address (e.g., 192.168.0.1).

For advanced parameters, execute help to see the options:
```
$ sudo ./burstPing -h
   burstPing is a utility for sending an intense burst of pings, and mesuring their response times in nanosecond resolution.
   ICMP Echo Requests are sent back-to-back (with out waiting for a response) and at a rate of 0.5*meanRTT. By default, 1000 requests are made. You can optionally configure the number of packets, the transmission rate (in nanoseconds), and the payload size (max 1500 Bytes)."<<endl<<endl;
   Usage: ./burstPing destination [-s payload_size]"<<endl<<"\t[-i interval_nsec] [-c burst_size] [-f save_filename]
   Example: ./burstPing 192.168.0.1 -i 100 -c 10000 -s 1500 -f out.csv
```

# License
This project is licensed under the MIT License - see the [LICENSE.txt](LICENSE.txt) file for details

Yisroel Mirsky
yisroel@post.bgu.ac.il
