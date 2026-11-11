#pragma once
#include<string>
#include<cstdint>
#include<cstddef>
#include<cerrno>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/time.h>
using namespace std;

// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 
inline void datatimeoutcheck(int socknum,int secs){
    timeval check;
    check.tv_sec=secs;
    check.tv_usec=0;
    setsockopt(socknum,SOL_SOCKET,SO_RCVTIMEO,&check,sizeof(check));
}
class Crc32{
    uint32_t set;
    public:
    Crc32() {
        set=0xFFFFFFFF;
    }
    void update(const char* data,size_t s){
        for(size_t i=0;i<s;i++){
            set^=(uint8_t)data[i];
            for(int b=0;b<8;b++){
                if(set & 1){
                    set=(set>>1) ^ 0xEDB88320;
                }
                else{
                    set=set>>1;
                }
            }
        }
    }
    uint32_t value(){
        return set ^ 0xFFFFFFFF;
    }
    inline void put32(uint32_t _s_et,char c[4]){
        c[0]=char(_s_et&0xFF);
        c[1]=char((_s_et>>8)& 0xFF);
        c[2]=char((_s_et>>16)& 0xFF);
        c[3]=char((_s_et>>24)& 0xFF);
    }
};
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};
