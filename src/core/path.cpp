#include "path.hpp"
#include <algorithm>
namespace crackhood {
bool valid_local_component(std::wstring_view name){
    if(name.empty()||name.size()>255||name==L"."||name==L".."||name.back()==L'.'||name.back()==L' ')return false;
    for(auto c:name)if(c<32||std::wstring_view(L"<>:\"/\\|?*").find(c)!=std::wstring_view::npos)return false;
    auto stem=std::wstring(name.substr(0,name.find(L'.')));for(auto&c:stem)if(c>=L'a'&&c<=L'z')c-=32;
    if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL")return false;
    return !(stem.size()==4&&(stem.starts_with(L"COM")||stem.starts_with(L"LPT"))&&stem[3]>=L'1'&&stem[3]<=L'9');
}
Result<RemotePath> normalize_remote(std::string_view input) {
    auto bad=[] { return Error{ErrorCode::InvalidName,"Invalid Xbox path or unsupported filename encoding"}; };
    if(input.empty() || input.size()>=260) return bad();
    for(unsigned char c:input) if(c<32 || c>126 || c=='"' || c=='*' || c=='?' || c=='|' || c=='<' || c=='>') return bad();
    std::string s(input); std::replace(s.begin(),s.end(),'/','\\');
    auto colon=s.find(':');
    if(colon==std::string::npos || colon==0 || colon>32 || colon+1>=s.size() || s[colon+1]!='\\') return bad();
    for(size_t i=0;i<colon;++i) if(!((s[i]>='A'&&s[i]<='Z') || (s[i]>='a'&&s[i]<='z') || (s[i]>='0'&&s[i]<='9') || s[i]=='_')) return bad();
    std::string normalized=s.substr(0,colon+2);
    size_t pos=colon+2;
    while(pos<s.size()) {
        auto end=s.find('\\',pos); if(end==std::string::npos) end=s.size();
        auto segment=s.substr(pos,end-pos);
        if(segment==".." || segment=="." || segment.find(':')!=std::string::npos || (!segment.empty()&&(segment.back()==' '||segment.back()=='.'))) return bad();
        if(!segment.empty()) { if(normalized.back()!='\\') normalized+='\\'; normalized+=segment; }
        pos=end+1;
    }
    return RemotePath{normalized};
}
}
