#include "ScriptEngine.h"

ScriptEngine* ScriptEngine::instance_ = nullptr;

ScriptEngine::ScriptEngine() {
    instance_ = this;
}

ScriptEngine::~ScriptEngine() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
    if (instance_ == this) instance_ = nullptr;
}

bool ScriptEngine::begin() {
    if (L_) lua_close(L_);
    L_ = luaL_newstate();
    if (!L_) return false;
    luaL_openlibs(L_);
    registerApi();
    return true;
}

void ScriptEngine::append(const String& text) {
    if (activeOutput_) *activeOutput_ += text;
}

int ScriptEngine::l_print(lua_State* L) {
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (instance_ && s) instance_->append(String(s).substring(0, len));
        lua_pop(L, 1);
        if (instance_ && i != n) instance_->append("\t");
    }
    if (instance_) instance_->append("\n");
    return 0;
}

int ScriptEngine::l_delay_us(lua_State* L) {
    const lua_Integer us = luaL_checkinteger(L, 1);
    if (us > 0) delayMicroseconds((uint32_t)us);
    return 0;
}

int ScriptEngine::l_bus_write(lua_State* L) {
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
    const uint16_t data = (uint16_t)luaL_checkinteger(L, 2);
    if (instance_) {
        char line[80];
        snprintf(line, sizeof(line), "[BUS] WRITE addr=0x%04X data=0x%04X\n", addr, data);
        instance_->append(line);
    }
    return 0;
}

int ScriptEngine::l_bus_read(lua_State* L) {
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
    const uint16_t value = 0xFFFF; // аппаратный BusController подключается следующим слоем
    if (instance_) {
        char line[80];
        snprintf(line, sizeof(line), "[BUS] READ addr=0x%04X -> 0x%04X (stub)\n", addr, value);
        instance_->append(line);
    }
    lua_pushinteger(L, value);
    return 1;
}

int ScriptEngine::l_bus_ready(lua_State* L) {
    lua_pushboolean(L, 0); // будет заменено чтением READY
    return 1;
}

int ScriptEngine::l_bus_irq(lua_State* L) {
    lua_pushboolean(L, 0); // будет заменено чтением IRQ
    return 1;
}

int ScriptEngine::l_bus_invert_data(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1);
    if (instance_) instance_->append(String("[BUS] invert_data=") + (enabled ? "true\n" : "false\n"));
    return 0;
}

int ScriptEngine::l_bus_invert_addr(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1);
    if (instance_) instance_->append(String("[BUS] invert_addr=") + (enabled ? "true\n" : "false\n"));
    return 0;
}

void ScriptEngine::registerApi() {
    lua_pushcfunction(L_, l_print);
    lua_setglobal(L_, "print");

    lua_pushcfunction(L_, l_delay_us);
    lua_setglobal(L_, "delay_us");

    lua_newtable(L_);
    lua_pushcfunction(L_, l_bus_write);       lua_setfield(L_, -2, "write");
    lua_pushcfunction(L_, l_bus_read);        lua_setfield(L_, -2, "read");
    lua_pushcfunction(L_, l_bus_ready);       lua_setfield(L_, -2, "ready");
    lua_pushcfunction(L_, l_bus_irq);         lua_setfield(L_, -2, "irq");
    lua_pushcfunction(L_, l_bus_invert_data); lua_setfield(L_, -2, "invert_data");
    lua_pushcfunction(L_, l_bus_invert_addr); lua_setfield(L_, -2, "invert_addr");
    lua_setglobal(L_, "bus");
}

bool ScriptEngine::run(const String& code, String& output) {
    output = "";
    if (!L_ && !begin()) {
        output = "Lua init error\n";
        return false;
    }

    running_ = true;
    activeOutput_ = &output;

    const int loadResult = luaL_loadbuffer(L_, code.c_str(), code.length(), "web-script");
    if (loadResult != LUA_OK) {
        output += "Lua compile error: ";
        output += lua_tostring(L_, -1);
        output += "\n";
        lua_pop(L_, 1);
        activeOutput_ = nullptr;
        running_ = false;
        return false;
    }

    const int runResult = lua_pcall(L_, 0, LUA_MULTRET, 0);
    if (runResult != LUA_OK) {
        output += "Lua runtime error: ";
        output += lua_tostring(L_, -1);
        output += "\n";
        lua_pop(L_, 1);
        activeOutput_ = nullptr;
        running_ = false;
        return false;
    }

    activeOutput_ = nullptr;
    running_ = false;
    return true;
}

void ScriptEngine::stop() {
    // На следующем этапе сюда добавляется debug hook Lua для прерывания бесконечного цикла.
    running_ = false;
}
