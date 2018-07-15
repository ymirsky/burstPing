#ifndef PARPINGER_H
/* Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */


#define PARPINGER_H


#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/time.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
////#include <netinet/ip_var.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
//#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <iostream>
#include<vector>
#include<ctime>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <time.h>
#include <condition_variable>

//ICMP packet constants
#define	DEFDATALEN	(ICMP_MINLEN)	/* default data length */
#define	MAXIPLEN	60
#define	MAXICMPLEN	76
#define	MAXPACKET	(65536 - 60 - ICMP_MINLEN)/* max packet size */


using std::chrono::steady_clock;

using namespace std;

struct point{
    timespec t;
    uint16_t indx;
};

class parPinger
{
    public:
    parPinger(string ip, int burst_len, long interval, int payload, string filename);
    ~parPinger();
    pthread_t probeSenderThread;
    pthread_t probeRecverThread;
    static void* sendMain(void * args);
    static void* recvMain(void * args);
    string targetIP;
    int burst_len;
    long interval;
    int payload;
    string filename;
    vector<point> send_times;


    private:
    double burstTime;
    long long burst_count;
    timespec ping_interval;
    long double avrgRTT;
    int send_count;
    static mutex probe_mtx;
    static mutex id_mtx;
    uint16_t scnrID;
    static int n_probeThreads;

    int send_pulse(int length);
    uint16_t in_cksum(uint16_t *addr, unsigned len);
    vector<double> probe(string targetIP);
    static void logf( const std::string &filename,const std::string &text );
    double probe_avrgRTT(int count); //sends count pings to measure RTT. Each ping is sent AFTER each response.
    void timespec_diff(struct timespec &startOverhead, struct timespec &stopOverhead, struct timespec &sendInterval, struct timespec &computed_sleepInterval);
    static struct  timespec  tsSubtract (struct  timespec  time1, struct  timespec  time2);
    static string ts2string(struct timespec t);

};




#endif // PARPINGER_H
