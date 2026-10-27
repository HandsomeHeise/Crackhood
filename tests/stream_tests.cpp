#include <winsock2.h>
#include <ws2tcpip.h>
#include "test.hpp"
#include "broker/xbdm_stream.hpp"
#include <thread>
#include <fstream>
#include <atomic>
using namespace crackhood;
struct MockXbdm{
    SOCKET listener=INVALID_SOCKET;std::jthread worker;std::atomic<bool>valid=false;bool zero;std::string received;
    explicit MockXbdm(bool empty):zero(empty){WSADATA data{};CHECK(WSAStartup(MAKEWORD(2,2),&data)==0);listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(listener!=INVALID_SOCKET);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(730);inet_pton(AF_INET,"127.0.0.1",&address.sin_addr);CHECK(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);CHECK(listen(listener,1)==0);
        worker=std::jthread([this]{WSAPOLLFD p{listener,POLLRDNORM,0};if(WSAPoll(&p,1,3000)<=0)return;auto client=accept(listener,nullptr,nullptr);if(client==INVALID_SOCKET)return;DWORD timeout=3000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));const char greeting[]="201- connected\r\n";send(client,greeting,sizeof(greeting)-1,0);std::string command;char c=0;while(command.size()<512&&recv(client,&c,1,0)==1){command+=c;if(c=='\n')break;}const char ready[]="204- send binary\r\n";send(client,ready,sizeof(ready)-1,0);
            if(zero){valid=command.find("length=0")!=std::string::npos&&recv(client,&c,1,0)==0;}
            else{while(received.size()<3){char buffer[3];auto n=recv(client,buffer,static_cast<int>(3-received.size()),0);if(n<=0)break;received.append(buffer,n);}valid=received=="abc"&&command.find("length=3")!=std::string::npos;const char done[]="200- OK\r\n";send(client,done,sizeof(done)-1,0);}closesocket(client);
        });
    }
    ~MockXbdm(){if(worker.joinable())worker.join();closesocket(listener);WSACleanup();}
};
TEST(stream_empty_upload_closes_body){auto path=std::filesystem::temp_directory_path()/L"Crackhood-stream-empty.bin";{std::ofstream f(path,std::ios::binary);}MockXbdm mock(true);auto r=send_xbdm_file("127.0.0.1",{"HDD:\\empty"},path,0,{},{});mock.worker.join();std::filesystem::remove(path);CHECK(std::holds_alternative<Unit>(r));CHECK(mock.valid);}
TEST(stream_upload_reports_bytes){auto path=std::filesystem::temp_directory_path()/L"Crackhood-stream-three.bin";{std::ofstream f(path,std::ios::binary);f<<"abc";}MockXbdm mock(false);uint64_t done=0,total=0;auto r=send_xbdm_file("127.0.0.1",{"HDD:\\payload"},path,3,{},[&](uint64_t d,uint64_t t){done=d;total=t;});mock.worker.join();std::filesystem::remove(path);CHECK(std::holds_alternative<Unit>(r));CHECK(mock.valid);CHECK(done==3&&total==3);}
