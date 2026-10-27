#pragma once
#include <functional>
#include <vector>
#include <string>
#include <stdexcept>
struct Test { std::string name; std::function<void()> run; };
inline std::vector<Test>& tests() { static std::vector<Test> t; return t; }
struct Register { Register(const char* name, void(*f)()) { tests().push_back({name,f}); } };
#define TEST(name) void name(); static Register reg_##name(#name,name); void name()
#define CHECK(expr) do { if(!(expr)) throw std::runtime_error("CHECK failed: " #expr); } while(false)
