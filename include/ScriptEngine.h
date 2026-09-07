#pragma once

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <lua.hpp>

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    bool begin();
    bool submit(const String& code);
    void stop();
    bool isRunning() const { return running_; }
    bool hasPending() const { return pending_; }
    String output();

private:
    lua_State* L_ = nullptr;
    volatile bool running_ = false;
    volatile bool pending_ = false;
    volatile bool stopRequested_ = false;

    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t outputMutex_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
    String output_;

    struct ScriptJob {
        char* code;
        size_t length;
    };

    static ScriptEngine* instance_;
    static void taskEntry(void* arg);
    void taskLoop();
    bool execute(const char* code, size_t length);

    static void luaHook(lua_State* L, lua_Debug* ar);
    static int l_print(lua_State* L);
    static int l_delay_us(lua_State* L);
    static int l_bus_write(lua_State* L);
    static int l_bus_read(lua_State* L);
    static int l_bus_ready(lua_State* L);
    static int l_bus_wait_ready(lua_State* L);
    static int l_bus_irq(lua_State* L);
    static int l_bus_timing(lua_State* L);
    static int l_bus_invert_data(lua_State* L);
    static int l_bus_invert_addr(lua_State* L);

    void clearOutput();
    void append(const String& text);
    void registerApi();
};
