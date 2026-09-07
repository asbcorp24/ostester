#include "ScriptEngine.h"
#include "BusEngine.h"

ScriptEngine* ScriptEngine::instance_ = nullptr;

ScriptEngine::ScriptEngine() { instance_ = this; }

ScriptEngine::~ScriptEngine() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
    if (instance_ == this) instance_ = nullptr;
}

bool ScriptEngine::begin() {
    if (!outputMutex_) outputMutex_ = xSemaphoreCreateMutex();
    if (!queue_) queue_ = xQueueCreate(2, sizeof(ScriptJob));
    if (!outputMutex_ || !queue_) return false;

    if (!taskHandle_) {
        BaseType_t ok = xTaskCreate(taskEntry, "LuaTask", 8192, this, 4, &taskHandle_);
        if (ok != pdPASS) return false;
    }
    return true;
}

bool ScriptEngine::submit(const String& code) {
    if (!queue_ || running_ || pending_ || code.length() == 0) return false;

    char* copy = static_cast<char*>(pvPortMalloc(code.length() + 1));
    if (!copy) return false;
    memcpy(copy, code.c_str(), code.length());
    copy[code.length()] = 0;

    ScriptJob job{copy, code.length()};
    pending_ = true;
    clearOutput();
    append("[RTOS] Lua script queued\n");

    if (xQueueSend(queue_, &job, 0) != pdPASS) {
        pending_ = false;
        vPortFree(copy);
        return false;
    }
    return true;
}

void ScriptEngine::taskEntry(void* arg) {
    static_cast<ScriptEngine*>(arg)->taskLoop();
}

void ScriptEngine::taskLoop() {
    for (;;) {
        ScriptJob job{};
        if (xQueueReceive(queue_, &job, portMAX_DELAY) == pdPASS) {
            pending_ = false;
            running_ = true;
            stopRequested_ = false;
            append("[RTOS] LuaTask started\n");
            execute(job.code, job.length);
            vPortFree(job.code);
            running_ = false;
            append(stopRequested_ ? "[RTOS] LuaTask stopped\n" : "[RTOS] LuaTask finished\n");
        }
    }
}

bool ScriptEngine::execute(const char* code, size_t length) {
    if (L_) lua_close(L_);
    L_ = luaL_newstate();
    if (!L_) {
        append("Lua init error\n");
        return false;
    }

    luaL_openlibs(L_);
    registerApi();
    lua_sethook(L_, luaHook, LUA_MASKCOUNT, 2000);

    const int loadResult = luaL_loadbuffer(L_, code, length, "web-script");
    if (loadResult != LUA_OK) {
        append("Lua compile error: ");
        append(lua_tostring(L_, -1));
        append("\n");
        lua_pop(L_, 1);
        return false;
    }

    const int runResult = lua_pcall(L_, 0, LUA_MULTRET, 0);
    if (runResult != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        if (err) {
            append(stopRequested_ ? "Lua stopped: " : "Lua runtime error: ");
            append(err);
            append("\n");
        }
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

void ScriptEngine::luaHook(lua_State* L, lua_Debug*) {
    if (instance_ && instance_->stopRequested_) {
        luaL_error(L, "STOP requested");
    }
    taskYIELD();
}

void ScriptEngine::stop() { stopRequested_ = true; }

String ScriptEngine::output() {
    if (!outputMutex_) return output_;
    xSemaphoreTake(outputMutex_, portMAX_DELAY);
    String copy = output_;
    xSemaphoreGive(outputMutex_);
    return copy;
}

void ScriptEngine::clearOutput() {
    if (!outputMutex_) { output_ = ""; return; }
    xSemaphoreTake(outputMutex_, portMAX_DELAY);
    output_ = "";
    xSemaphoreGive(outputMutex_);
}

void ScriptEngine::append(const String& text) {
    if (!outputMutex_) { output_ += text; return; }
    xSemaphoreTake(outputMutex_, portMAX_DELAY);
    output_ += text;
    if (output_.length() > 24000) output_.remove(0, output_.length() - 24000);
    xSemaphoreGive(outputMutex_);
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
    const bool ok = BusEngine::instance().write(addr, data);
    if (instance_) {
        char line[96];
        snprintf(line, sizeof(line), "[BUS] WRITE addr=0x%04X data=0x%04X %s\n", addr, data, ok ? "OK" : "TIMEOUT");
        instance_->append(line);
    }
    lua_pushboolean(L, ok);
    return 1;
}

int ScriptEngine::l_bus_read(lua_State* L) {
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t value = 0;
    const bool ok = BusEngine::instance().read(addr, value);
    if (instance_) {
        char line[96];
        snprintf(line, sizeof(line), "[BUS] READ addr=0x%04X -> 0x%04X %s\n", addr, value, ok ? "OK" : "TIMEOUT");
        instance_->append(line);
    }
    if (!ok) {
        lua_pushnil(L);
        lua_pushstring(L, "bus read timeout");
        return 2;
    }
    lua_pushinteger(L, value);
    return 1;
}

int ScriptEngine::l_bus_ready(lua_State* L) {
    lua_pushboolean(L, BusEngine::instance().ready());
    return 1;
}

int ScriptEngine::l_bus_irq(lua_State* L) {
    lua_pushboolean(L, BusEngine::instance().irq());
    return 1;
}

int ScriptEngine::l_bus_invert_data(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1);
    BusEngine::instance().setInvertData(enabled);
    if (instance_) instance_->append(String("[BUS] invert_data=") + (enabled ? "true\n" : "false\n"));
    return 0;
}

int ScriptEngine::l_bus_invert_addr(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1);
    BusEngine::instance().setInvertAddress(enabled);
    if (instance_) instance_->append(String("[BUS] invert_addr=") + (enabled ? "true\n" : "false\n"));
    return 0;
}

void ScriptEngine::registerApi() {
    lua_pushcfunction(L_, l_print); lua_setglobal(L_, "print");
    lua_pushcfunction(L_, l_delay_us); lua_setglobal(L_, "delay_us");

    lua_newtable(L_);
    lua_pushcfunction(L_, l_bus_write);       lua_setfield(L_, -2, "write");
    lua_pushcfunction(L_, l_bus_read);        lua_setfield(L_, -2, "read");
    lua_pushcfunction(L_, l_bus_ready);       lua_setfield(L_, -2, "ready");
    lua_pushcfunction(L_, l_bus_irq);         lua_setfield(L_, -2, "irq");
    lua_pushcfunction(L_, l_bus_invert_data); lua_setfield(L_, -2, "invert_data");
    lua_pushcfunction(L_, l_bus_invert_addr); lua_setfield(L_, -2, "invert_addr");
    lua_setglobal(L_, "bus");
}
