#pragma once

#include <Arduino.h>
#include <lua.hpp>

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    bool begin();
    bool run(const String& code, String& output);
    void stop();
    bool isRunning() const { return running_; }

private:
    lua_State* L_ = nullptr;
    bool running_ = false;
    String* activeOutput_ = nullptr;

    static ScriptEngine* instance_;

    static int l_print(lua_State* L);
    static int l_delay_us(lua_State* L);
    static int l_bus_write(lua_State* L);
    static int l_bus_read(lua_State* L);
    static int l_bus_ready(lua_State* L);
    static int l_bus_irq(lua_State* L);
    static int l_bus_invert_data(lua_State* L);
    static int l_bus_invert_addr(lua_State* L);

    void append(const String& text);
    void registerApi();
};
