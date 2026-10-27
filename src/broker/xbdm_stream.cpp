#include <winsock2.h>
#include <ws2tcpip.h>
#include "xbdm_stream.hpp"
#include "core/path.hpp"
#include <fstream>
#include <array>
namespace crackhood {
struct Socket{SOCKET value=INVALID_SOCKET;~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}};
static Result<Unit> transfer(const std::string&address,const RemotePath&remote,const std::filesystem::path&local,uint64_t expected,std::stop_token stop,Progress progress,bool uploading){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return Error{ErrorCode::Offline,"Windows sockets are unavailable"};
    struct Cleanup{~Cleanup(){WSACleanup();}}cleanup;
    try {
        if(std::holds_alternative<Error>(normalize_remote(remote.value)))return Error{ErrorCode::InvalidName,"Invalid Xbox path"};
        Socket socket;socket.value=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(socket.value==INVALID_SOCKET)throw std::runtime_error("Could not create socket");
        u_long nonblocking=1;ioctlsocket(socket.value,FIONBIO,&nonblocking);sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(730);
        if(inet_pton(AF_INET,address.c_str(),&target.sin_addr)!=1)return Error{ErrorCode::InvalidName,"Invalid Xbox IP address"};
        int connected=connect(socket.value,reinterpret_cast<sockaddr*>(&target),sizeof(target));
        if(connected==SOCKET_ERROR&&WSAGetLastError()!=WSAEWOULDBLOCK)return Error{ErrorCode::Offline,"Xbox is unreachable"};
        WSAPOLLFD poll{socket.value,POLLWRNORM,0};if(WSAPoll(&poll,1,3000)<=0)return Error{ErrorCode::Timeout,"Xbox connection timed out"};
        int error=0,len=sizeof(error);getsockopt(socket.value,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&len);if(error)return Error{ErrorCode::Offline,"Xbox connection failed",error};
        auto exact=[&](char*buffer,size_t size,bool write){size_t offset=0;auto deadline=GetTickCount64()+5000;while(offset<size){if(stop.stop_requested())throw std::runtime_error("Transfer cancelled");if(GetTickCount64()>deadline)throw std::runtime_error("XBDM data transfer timed out");WSAPOLLFD p{socket.value,static_cast<SHORT>(write?POLLWRNORM:POLLRDNORM),0};auto ready=WSAPoll(&p,1,100);if(ready<0)throw std::runtime_error("Socket error");if(!ready)continue;auto n=write?send(socket.value,buffer+offset,static_cast<int>(std::min<size_t>(size-offset,65536)),0):recv(socket.value,buffer+offset,static_cast<int>(std::min<size_t>(size-offset,65536)),0);if(n<=0)throw std::runtime_error("Xbox connection lost");offset+=n;deadline=GetTickCount64()+5000;}};
        auto line=[&]{std::string s;char c=0;auto deadline=GetTickCount64()+5000;while(s.size()<1024){if(GetTickCount64()>deadline)throw std::runtime_error("XBDM response timed out");exact(&c,1,false);if(c=='\n')return s;if(c!='\r')s+=c;}throw std::runtime_error("XBDM response is too long");};
        if(!line().starts_with("201"))throw std::runtime_error("Invalid XBDM greeting");
        if(uploading){
            if(expected>UINT32_MAX)return Error{ErrorCode::Unsupported,"File is larger than 4 GiB"};
            std::ifstream source(local,std::ios::binary);if(!source)return Error{ErrorCode::AccessDenied,"Could not open source file"};
            auto command="sendfile name=\""+remote.value+"\" length="+std::to_string(expected)+"\r\n";exact(command.data(),command.size(),true);
            auto status=line();if(!status.starts_with("204"))return Error{ErrorCode::AccessDenied,"XBDM: "+status};
            std::array<char,65536>buffer{};uint64_t offset=0;if(progress)progress(0,expected);
            while(offset<expected){auto count=static_cast<size_t>(std::min<uint64_t>(buffer.size(),expected-offset));source.read(buffer.data(),count);if(static_cast<size_t>(source.gcount())!=count)return Error{ErrorCode::Protocol,"Source changed during upload"};exact(buffer.data(),count,true);offset+=count;if(progress)progress(offset,expected);}
            // This RGH handler creates/truncates the file before 204, but never
            // completes its receive callback for a zero-length body. Close the
            // stream; TransferJob confirms the resulting zero-byte file via stat.
            if(expected==0){shutdown(socket.value,SD_SEND);return Unit{};}
            status=line();if(!status.starts_with("200"))return Error{ErrorCode::Uncertain,"XBDM upload was not confirmed: "+status};return Unit{};
        }
        auto command="getfile name=\""+remote.value+"\"\r\n";exact(command.data(),command.size(),true);
        auto status=line();if(!status.starts_with("203"))return Error{ErrorCode::AccessDenied,"XBDM: "+status};
        std::array<unsigned char,4>length{};exact(reinterpret_cast<char*>(length.data()),4,false);
        uint64_t size=uint64_t(length[0])|(uint64_t(length[1])<<8)|(uint64_t(length[2])<<16)|(uint64_t(length[3])<<24);
        if(size!=expected)return Error{ErrorCode::Protocol,"XBDM file size does not match its attributes"};
        std::ofstream file(local,std::ios::binary|std::ios::trunc);if(!file)return Error{ErrorCode::AccessDenied,"Could not open local destination file"};
        std::array<char,65536>buffer{};uint64_t offset=0;if(progress)progress(0,size);
        while(offset<size){auto count=static_cast<size_t>(std::min<uint64_t>(buffer.size(),size-offset));exact(buffer.data(),count,false);file.write(buffer.data(),count);if(!file)return Error{ErrorCode::DiskFull,"Local file write failed"};offset+=count;if(progress)progress(offset,size);}
        file.flush();if(!file)return Error{ErrorCode::DiskFull,"Local file write failed"};return Unit{};
    }catch(const std::exception&e){return Error{stop.stop_requested()?ErrorCode::Cancelled:ErrorCode::Offline,e.what()};}
}
Result<Unit> receive_xbdm_file(const std::string&a,const RemotePath&r,const std::filesystem::path&l,uint64_t size,std::stop_token s,Progress p){return transfer(a,r,l,size,s,p,false);}
Result<Unit> send_xbdm_file(const std::string&a,const RemotePath&r,const std::filesystem::path&l,uint64_t size,std::stop_token s,Progress p){return transfer(a,r,l,size,s,p,true);}
}
