#include <stdio.h>
#include <iostream>
#include <string>
#include <unistd.h> // getuid
#include "parpinger.h"
using namespace std;

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

int main(int argc, char *argv[])
{
    cout<<"===================="<<endl;
    cout<<"** burstPing v0.1 **"<<endl;
    cout<<"Yisroel Mirsky 2018"<<endl;
    cout<<"===================="<<endl<<endl;

    //check if root (needed for sockets)
    if (getuid()){
        cout<<"You are not root. You may need to run burstPing as root in order for it to work correctly."<<endl;
        cout<<"Example: sudo ./burstPing 192.168.0.1"<<endl<<endl;
        cout<<"Do you wich to proceed anyways? [y/n]"<<endl;
        string q;
        cin>>q;
        if(q.compare("n")==0)
            return -1;
    }

    //default paramters
    string targetIP = string(argv[1]);//First argument is target IP
    int burst_size = 1000;
    int payload = 56; //ping utility default
    long interval = -1; //will be set to 0.5 RTT
    string filename = "";

    //Parse arguments
    if(argc < 2){
        cout<<"You must supply a target IP address."<<endl;
        cout<<"Example: ./burstPing 192.168.0.1"<<endl;
        return -1;
    }

    if(string(argv[1]).compare("-h")==0){ //help
        cout<<"burstPing is a utility for sending an intense burst of pings, and mesuring their response times in nanosecond resolution."<<endl;
        cout<<"ICMP Echo Requests are sent back-to-back (with out waiting for a response) and at a rate of 0.5*meanRTT. By default, 1000 requests are made. You can optionally configure the number of packets, the transmission rate (in nanoseconds), and the payload size (max 1500 Bytes)."<<endl<<endl;
        cout<<"Usage: ./burstPing destination [-s payload_size]"<<endl<<"\t[-i interval_nsec] [-c burst_size] [-f save_filename]"<<endl;
        cout<<"Example: ./burstPing 192.168.0.1 -i 100 -c 10000 -s 1500 -f out.csv"<<endl;
        return -1;
    }

    //optional arguments
    for(int i =2; i<argc; i++){
        if(string(argv[i]).compare("-c")==0){ // packet count (burst size)
            burst_size = atoi(argv[i+1]);
            if(burst_size<1)
                burst_size = 1;
        }else if(string(argv[i]).compare("-i")==0){ // send interval (nanoseconds)
            interval = stol(argv[i+1]);
            if(interval<0)
                interval = 0;
        }else if(string(argv[i]).compare("-s")==0){ // ICMP payload size in Bytes (max is 1500)
            payload = atoi(argv[i+1]);
            if(payload<0)
                payload = 0;
            else if(payload > 1500)
                payload = 1500;
        }else if(string(argv[i]).compare("-f")==0){ // filename to save RTTs
            filename = string(argv[i+1]);
        }
    }


    cout<<"Parameters:"<<endl;
    cout<<"\tTarget IP: "<<targetIP<<endl;
    cout<<"\tBurst Size: "<<burst_size<<" packets"<<endl;
    cout<<"\tPayload Size: "<<payload<<" Bytes"<<endl;
    if(interval!=-1)
        cout<<"\tPacket rate: "<<1.0/(interval*1000000.0)<<" KHz (thousands of packets persecond)"<<endl;


    parPinger pinger(targetIP,burst_size,interval,payload,filename);

    return -1;
}
