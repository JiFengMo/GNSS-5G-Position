#pragma once
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace b1live {
struct Json {
  using Object=std::map<std::string,Json>;
  using Array=std::vector<Json>;
  std::string raw="null";
  Json()=default; Json(std::nullptr_t){}
  Json(bool v):raw(v?"true":"false"){} Json(int v):raw(std::to_string(v)){} Json(unsigned v):raw(std::to_string(v)){}
  Json(int64_t v):raw(std::to_string(v)){} Json(uint64_t v):raw(std::to_string(v)){}
  Json(double v){if(std::isfinite(v)){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<v;raw=s.str();}}
  Json(const char* v):Json(std::string(v)){} Json(const std::string& v){std::ostringstream s;quote(s,v);raw=s.str();}
  Json(const Object& v){std::ostringstream s;s<<'{';bool first=true;for(const auto& p:v){if(!first)s<<',';first=false;quote(s,p.first);s<<':'<<p.second.raw;}s<<'}';raw=s.str();}
  Json(const Array& v){std::ostringstream s;s<<'[';bool first=true;for(const auto& p:v){if(!first)s<<',';first=false;s<<p.raw;}s<<']';raw=s.str();}
  static void quote(std::ostream& s,const std::string& text) {
    s<<'"'; const char* hex="0123456789abcdef";
    for(unsigned char c:text) {
      if(c=='"'||c=='\\')s<<'\\'<<char(c);
      else if(c<32)s<<"\\u00"<<hex[c>>4]<<hex[c&15]; else s<<char(c);
    } s<<'"';
  }
  void write(std::ostream& s)const {
    s<<raw;
  }
  const std::string& dump()const {return raw;}
};
using Object=Json::Object;
using Array=Json::Array;
inline std::string counter(uint64_t v){return std::to_string(v);}
}
