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


#include "parpinger.h"
#include <fstream>
#include <sys/types.h>
#include <sys/syscall.h>
#include <math.h>
#include <algorithm>

mutex parPinger::probe_mtx;
mutex parPinger::id_mtx;
int parPinger::n_probeThreads;

parPinger::parPinger(string ip, int burst_length, long interval, int payload, string filename)
{
    //parameters
    targetIP = ip;
    burst_len = burst_length;
    this->interval = interval;
    this->payload = payload;
    this->filename = filename;

    //internal counters
    send_count = 0;
    burst_count = 0;

    //Generate shared ID for the threads (used to identify the ICMP responses)
    parPinger::id_mtx.lock();
    scnrID = parPinger::n_probeThreads++;
    parPinger::id_mtx.unlock();

    //Make threads
    pthread_create(&probeRecverThread, NULL, parPinger::recvMain, (void*)this);
    pthread_create(&probeSenderThread, NULL, parPinger::sendMain, (void*)this);

    pthread_join(probeRecverThread,NULL);//wait for threads to complete
}

parPinger::~parPinger()
{
}




//Sends a burst of ICMP ECHO REQUESTS back-to-back at a rate of 0.5*RTT, and notes their tx times.
void* parPinger::sendMain(void *args)
{
    parPinger* prthr = (parPinger*)args;
    struct timespec curTime;
    prthr->burstTime = 9999999; //init the time it took to send the burst

    //set ping tx interval
    if(prthr->interval == -1){// find RTT/2
        // Get average RTT:
        cout<<"\tPacket rate: "; cout.flush();
        long double RTT = (double)prthr->probe_avrgRTT(1000);
        prthr->ping_interval.tv_sec = (long) (RTT*0.55);
        prthr->ping_interval.tv_nsec = (RTT*0.55 - prthr->ping_interval.tv_sec)* 1000000000L;
        cout<<1.0/(RTT*0.55*1000)<<" KHz (thousands of packets persecond)"<<endl; cout.flush();

    }else{//use the user given parameter
        prthr->ping_interval.tv_sec = 0;
        prthr->ping_interval.tv_nsec = prthr->interval;
    }
    usleep(100000); // 100ms

    /* Send Burst */
    cout<<endl<<"Sending ICMP ECHO RQ Burst: (*=100 packets)"<<endl;
    prthr->send_pulse(prthr->burst_len);
}

//Receives a burst of ICMP ECHO REPLIES, and measures their RTT times.
void* parPinger::recvMain(void *args)
{
    parPinger* prthr = (parPinger*)args;
    struct timespec receptionTime;
    vector<point> recv_times; //temporary place to store the points from a single PIR
    point rx;
    clock_gettime(CLOCK_MONOTONIC, &rx.t);

    /*Setup receive socket*/
    int s, i, cc, packlen, datalen = 1500 + ICMP_MINLEN;
    struct sockaddr_in to, from;
    fd_set rfds;
    int ret, fromlen, hlen;
    int retval;
    struct icmp *icp;
    to.sin_family = AF_INET;
    string hostname;
    u_char *packet, outpack[MAXPACKET];
    struct ip *ip;

    // try to convert as dotted decimal address, else if that fails assume it's a hostname
    to.sin_addr.s_addr = inet_addr(prthr->targetIP.c_str());
    if (to.sin_addr.s_addr != (u_int)-1)
        hostname = prthr->targetIP;
    else
    {
        cerr << "unknown host "<< prthr->targetIP << endl;
        return NULL;
    }
    packlen = datalen + MAXIPLEN + MAXICMPLEN;
    if ( (packet = (u_char *)malloc((u_int)packlen)) == NULL)
    {
        cerr << "malloc error\n";
        return NULL;
    }
    if ( (s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0)
    {
        perror("socket");	/* probably not running as superuser */
        free(packet);
        return NULL;
    }
    // Watch for socket inputs.
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);

    // Timeout: after each reception, wait up to X micro seconds before giving up on remaining replies.
    timeval timeout;
    struct timespec curTime;

    /* Main loop */
    int rx_count = 0;
    for(;;)
    {
        /* Receive ICMP ECHO Response */
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        retval = select(s+1, &rfds, NULL, NULL, &timeout);
        clock_gettime(CLOCK_MONOTONIC, &receptionTime); //reception time
        if (retval == -1)
        {
            perror("select()");
            break;
        }
        if (retval == 0)//timeout
        {
            break;
        }
        fromlen = sizeof(sockaddr_in);
        if ( (ret = recvfrom(s, (char *)packet, packlen, 0,(struct sockaddr *)&from, (socklen_t*)&fromlen)) < 0)
        {
            perror("recvfrom error");
            //break;
        }

        // Check the IP header
        ip = (struct ip *)((char*)packet);
        hlen = sizeof( struct ip );
        if (ret < (hlen + ICMP_MINLEN))
        {
            cout<<"ECHO receive Error"<<endl;
            //break;
        }

        // Now the ICMP part
        icp = (struct icmp *)(packet + hlen);
        if (icp->icmp_type == ICMP_ECHOREPLY)
        {
            //cout << "Recv: echo reply"<< endl;
            if (icp->icmp_id != prthr->scnrID)
                continue;
        }
        else
        {// cout << "Recv: not an echo reply" << endl;
            continue;
        }

        /* Capture the response */
        rx.t = receptionTime;
        rx.indx = icp->icmp_seq;
        recv_times.push_back(rx);
        rx_count++;

        if(rx_count == prthr->burst_len)
            break;
    }

    /*Clean up*/
    close(s);
    free(packet);


    if(recv_times.size()!=0){
        /////// Compute RTTs ///////
        vector<int> indexes(prthr->send_times.size(),0);
        vector<double> tx_time(prthr->send_times.size(),0);
        vector<double> rx_time(prthr->send_times.size(),0);
        vector<double> rtt(prthr->send_times.size(),0);

        int rx_indx = 0;
        for(int i=0;i<prthr->send_times.size();i++)//assume that replies are in same order as requests
        {
            indexes[i] = prthr->send_times[i].indx;
            tx_time[i] = (double)(prthr->send_times[i].t.tv_sec) + ((double)(prthr->send_times[i].t.tv_nsec)/1000000000.0);
            if(prthr->send_times[i].indx==recv_times[rx_indx].indx)
            {
                rx_time[i] = (double)(recv_times[rx_indx].t.tv_sec) + ((double)(recv_times[rx_indx].t.tv_nsec)/1000000000.0);
                rtt[i] = rx_time[i] - tx_time[i];
                rx_indx++;
            }else{
                rx_time[i] = nan("");
                rtt[i] = nan("");
            }
        }
        //////// Print Stats ////////
        vector<double> rtt_vals;
        double N =0;
        double var = 0;
        double sum = 0;
        double mean = 0;
        double sd = 0;
        double min = 9999999999;
        double max = -1;

        //collect real values
        for(int i =0;i<rtt.size();i++)
        {
            if(!isnan(rtt[i])){
                rtt_vals.push_back(rtt[i]);
                sum+=rtt[i];
                if(rtt[i]<min)
                    min = rtt[i];
                if(rtt[i]>max)
                    max = rtt[i];
            }
        }

        //compute stats
        N = rtt_vals.size();
        mean = sum/N;
        for(int n = 0; n < N; n++ )
        {
          var += (rtt_vals[n] - mean) * (rtt_vals[n] - mean);
        }
        var /= N;
        sd = sqrt(var);

        //print stats
        cout<<"--- "<<prthr->targetIP<<" burst ping statistics ---"<<endl;
        double total_sent = (double)prthr->send_times.size();
        cout<<prthr->send_times.size()<<" packets transmitted, "<<N<<" received, "<<100.0*((total_sent-N)/total_sent)<<"% packet loss, total burst time "<<prthr->burstTime<<" sec"<<endl;
        cout<<"rtt min/avg/max/mdev = "<<min*1000<<"/"<<mean*1000<<"/"<<max*1000<<"/"<<sd*1000<<" ms"<<endl;

        //////// Save RTTs to file? /////
        if(prthr->filename.compare("")!=0){
            logf(prthr->filename,"index, tx_time, rx_time, rtt_time"); //header
            string row;
            for(int i=0;i<prthr->send_times.size();i++)
            {
                row = to_string(indexes[i]) + ", " + to_string(tx_time[i])+ ", " + to_string(rx_time[i])+ ", " + to_string(rtt[i]);
                logf(prthr->filename,row);
            }
        }
    }

}


//////////////// Utility Functions for the Threads ///////////////

//Sends a burst of ICMP Echo requests to the targetIP at a rate of 0.5*meanRTT
int parPinger::send_pulse(int length)
{
    /* Open Sending Socket */
    int s, i, cc, packlen, datalen = payload;
    struct hostent *hp;
    struct sockaddr_in to, from;
    //struct protoent	*proto;
    struct ip *ip;
    u_char *packet, outpack[MAXPACKET];
    string hostname;
    struct icmp *icp;


    to.sin_family = AF_INET;

    // try to convert as dotted decimal address, else if that fails assume it's a hostname
    to.sin_addr.s_addr = inet_addr(targetIP.c_str());
    if (to.sin_addr.s_addr != (u_int)-1)
        hostname = targetIP;
    else
    {
        cerr << "unknown host "<< targetIP << endl;
        return -1;
    }
    packlen = datalen + MAXIPLEN + MAXICMPLEN;
    if ( (packet = (u_char *)malloc((u_int)packlen)) == NULL)
    {
        cerr << "malloc error\n";
        return -1;
    }

    if ( (s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0)
    {
        perror("socket");	/* probably not running as superuser */
        free(packet);
        return -1;
    }

    /* Execute Burst */
    icp = (struct icmp *)outpack;
    icp->icmp_type = ICMP_ECHO;
    icp->icmp_code = 0;
    icp->icmp_cksum = 0;
    icp->icmp_seq = 0;	/* seq and id must be reflected */
    icp->icmp_id = scnrID;
    cc = datalen + ICMP_MINLEN;
    icp->icmp_cksum = in_cksum((unsigned short *)icp,cc);
    struct timespec tmp;

    struct timespec start;
    struct timespec stop;
    struct timespec i_start;i_start.tv_sec=0;i_start.tv_nsec=0;
    struct timespec i_stop;i_stop.tv_sec=0;i_stop.tv_nsec=0;
    struct timespec wait_time;wait_time.tv_sec=0;wait_time.tv_nsec=0;


    clock_gettime(CLOCK_MONOTONIC, &start); //start pir time
int count = 0;
point tx;

    for(int k = 0; k < length; k++)
    {
        //calc wait time
        //wait_time.tv_nsec = max(ping_interval.tv_nsec - (i_stop.tv_nsec - i_start.tv_nsec), 0L );//trying to account for processing time and timer accuracy
        timespec_diff(i_start,i_stop,ping_interval,wait_time);

        //wait_time.tv_nsec = 0L ;//trying to account for processing time and timer accuracy

        //wait
        nanosleep(&wait_time,&tmp);

        //setup ping
        cc = 1500 + ICMP_MINLEN;
        //update checksum
        icp->icmp_cksum = 0;
        icp->icmp_cksum = in_cksum((unsigned short *)icp,cc);
        //send
        i = sendto(s, (char *)outpack, cc, 0, (struct sockaddr*)&to, (socklen_t)sizeof(struct sockaddr_in));
        clock_gettime(CLOCK_MONOTONIC, &i_start);
        tx.indx = icp->icmp_seq;
        tx.t = i_start;
        send_times.push_back(tx);

        if (i < 0 || i != cc)
        {
            if (i < 0)
                perror("sendto error");
            cout << "wrote " << hostname << " " <<  cc << " chars, ret= " << i << endl;
        }
        //increment seq
        icp->icmp_seq++;

        count++;
        if(count%100==0){
            cout<<"*";
            cout.flush();
        }
        clock_gettime(CLOCK_MONOTONIC, &i_stop);
    }
    clock_gettime(CLOCK_MONOTONIC, &stop); //reception time
    cout<<endl<<endl;

    /* Close Sending Socket */
    close(s);
    free(packet);
    double start1 = (double)(start.tv_sec) + (double)(start.tv_nsec)/1000000000;
    double stop1 = (double)(stop.tv_sec) + (double)(stop.tv_nsec)/1000000000;

    burstTime = stop1-start1;

    return 0;
}

uint16_t parPinger::in_cksum(uint16_t *addr, unsigned len)
{
    uint16_t answer = 0;
    /*
       * Our algorithm is simple, using a 32 bit accumulator (sum), we add
       * sequential 16 bit words to it, and at the end, fold back all the
       * carry bits from the top 16 bits into the lower 16 bits.
       */
    uint32_t sum = 0;
    while (len > 1)  {
        sum += *addr++;
        len -= 2;
    }

    // mop up an odd byte, if necessary
    if (len == 1) {
        *(unsigned char *)&answer = *(unsigned char *)addr ;
        sum += answer;
    }

    // add back carry outs from top 16 bits to low 16 bits
    sum = (sum >> 16) + (sum & 0xffff); // add high 16 to low 16
    sum += (sum >> 16); // add carry
    answer = ~sum; // truncate to 16 bits
    return answer;
}


//gets average RTT in seconds time between this host and target IP
//Each ping is sent AFTER each response. Note: parPinger sends requests back-to-back and receives them in parrallel.
double parPinger::probe_avrgRTT(int count)
{
    double sumRTTs = 0;
    int s, i, cc, packlen, datalen = payload;
    struct hostent *hp;
    struct sockaddr_in to, from;
    //struct protoent	*proto;
    struct ip *ip;
    u_char *packet, outpack[MAXPACKET];
    char hnamebuf[MAXHOSTNAMELEN];
    string hostname;
    struct icmp *icp;
    int ret, fromlen, hlen;
    fd_set rfds;
    struct timeval tv;
    int retval;
    struct timespec start, end;

    double /*start_t, */end_t;
    bool cont;

    to.sin_family = AF_INET;

    // try to convert as dotted decimal address, else if that fails assume it's a hostname
    to.sin_addr.s_addr = inet_addr(targetIP.c_str());
    if (to.sin_addr.s_addr != (u_int)-1)
        hostname = targetIP;
    else
    {
        cerr << "unknown host "<< targetIP << endl;
        return -1;
    }
    packlen = datalen + MAXIPLEN + MAXICMPLEN;
    if ( (packet = (u_char *)malloc((u_int)packlen)) == NULL)
    {
        cerr << "malloc error\n";
        return -1;
    }


    if ( (s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0)
    {
        perror("socket");	/* probably not running as superuser */
        free(packet);
        return -1;
    }
    pid_t my_tid = syscall(__NR_gettid);

    for(int k = 0; k < count; k++){
        icp = (struct icmp *)outpack;
        icp->icmp_type = ICMP_ECHO;
        icp->icmp_code = 0;
        icp->icmp_cksum = 0;
        icp->icmp_seq = k;	/* seq and id must be reflected */
        icp->icmp_id = my_tid % 65000;


        cc = datalen + ICMP_MINLEN;
        icp->icmp_cksum = in_cksum((unsigned short *)icp,cc);

        // Watch stdin (fd 0) to see when it has input.
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        // Wait up to X micro seconds.
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        i = sendto(s, (char *)outpack, cc, 0, (struct sockaddr*)&to, (socklen_t)sizeof(struct sockaddr_in));
        clock_gettime(CLOCK_MONOTONIC, &start); //use CLOCK_MONOTONIC in deployment

        if (i < 0 || i != cc)
        {
            if (i < 0){
                close(s);
                free(packet);
                perror("sendto error");
            }
            cout << "wrote " << hostname << " " <<  cc << " chars, ret= " << i << endl;
        }


        cont = true;
        while(cont)
        {
            retval = select(s+1, &rfds, NULL, NULL, &tv);
            clock_gettime(CLOCK_MONOTONIC, &end); //use CLOCK_MONOTONIC in deployment
            if (retval == -1)
            {
                perror("select()");
                close(s);
                free(packet);
                return 0.001;
            }
            else if (retval)
            {
                fromlen = sizeof(sockaddr_in);
                if ( (ret = recvfrom(s, (char *)packet, packlen, 0,(struct sockaddr *)&from, (socklen_t*)&fromlen)) < 0)
                {
                    perror("recvfrom error");
                    close(s);
                    free(packet);
                    return 0.001;
                }

                // Check the IP header
                ip = (struct ip *)((char*)packet);
                hlen = sizeof( struct ip );
                if (ret < (hlen + ICMP_MINLEN))
                {
                    //   cerr << "packet too short (" << ret  << " bytes) from " << hostname << endl;
                    close(s);
                    free(packet);
                    return 0.001;
                }

                // Now the ICMP part
                icp = (struct icmp *)(packet + hlen);
                if (icp->icmp_type == ICMP_ECHOREPLY)
                {
                    //cout << "Recv: echo reply"<< endl;
                    if (icp->icmp_seq != k)
                    {
                        // cout << "received sequence # " << icp->icmp_seq << endl;
                        continue;
                    }
                    if (icp->icmp_id != my_tid % 65000)
                    {
                        //   cout << "received id " << icp->icmp_id << endl;
                        continue;
                    }
                    cont = false;
                }
                else
                {
                    // cout << "Recv: not an echo reply" << endl;
                    continue;
                }

                end_t = (double)(1000000000*(end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec));

                // auto end_T = steady_clock::now();
                // double elapsed_T = ((end_T-start_T).count())*steady_clock::period::num / static_cast<double>(steady_clock::period::den);
                sumRTTs +=end_t;
                break;
            }
            else
            {

                clock_gettime(CLOCK_MONOTONIC, &end); //use CLOCK_MONOTONIC in deployment
                end_t = (double)(1000000000*(end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec));
                //cout <<end_t<< "   ping timed-out. "+targetIP+"\n";
                sumRTTs +=end_t;
                break;
            }
        }
    }
    close(s);
    free(packet);
    return (sumRTTs/double(count))/1000000000.0; //sec
}

//function for computing send interval based on overhead
void parPinger::timespec_diff(struct timespec &startOverhead, struct timespec &stopOverhead, struct timespec &sendInterval, struct timespec &computed_sleepInterval)
{
    computed_sleepInterval.tv_sec = sendInterval.tv_sec;
    computed_sleepInterval.tv_nsec = sendInterval.tv_nsec;

    //Compute overhead
    struct timespec overhead;
    if ((stopOverhead.tv_nsec - startOverhead.tv_nsec) < 0) {
        overhead.tv_sec = stopOverhead.tv_sec - startOverhead.tv_sec - 1;
        overhead.tv_nsec = stopOverhead.tv_nsec - startOverhead.tv_nsec + 1000000000;
    } else {
        overhead.tv_sec = stopOverhead.tv_sec - startOverhead.tv_sec;
        overhead.tv_nsec = stopOverhead.tv_nsec - startOverhead.tv_nsec;
    }


    //compute interval
    if ((stopOverhead.tv_nsec - startOverhead.tv_nsec) < 0) {
        computed_sleepInterval.tv_sec = computed_sleepInterval.tv_sec - overhead.tv_sec - 1;
        computed_sleepInterval.tv_nsec = computed_sleepInterval.tv_nsec - overhead.tv_nsec + 1000000000;
    } else {
        computed_sleepInterval.tv_sec = computed_sleepInterval.tv_sec - overhead.tv_sec;
        computed_sleepInterval.tv_nsec = computed_sleepInterval.tv_nsec - overhead.tv_nsec;
    }

    if(computed_sleepInterval.tv_sec < 0 || computed_sleepInterval.tv_nsec < 0)
    {
        computed_sleepInterval.tv_sec = 0;
        computed_sleepInterval.tv_nsec = 0;
    }
    return;
}

void parPinger::logf( const std::string &filename,const std::string &text )
{
    std::ofstream log_file(
                filename, std::ios_base::out | std::ios_base::app );
    log_file << text << endl;
}
