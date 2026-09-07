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

void ScriptEngine::stop() {
    stopRequested_ = true;
    BusEngine::instance().emergencyStop();
}

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

uint32_t ScriptEngine::elapsedUs(uint32_t startedCycles) {
    const uint32_t cycles = static_cast<uint32_t>(DWT->CYCCNT - startedCycles);
    const uint32_t cyclesPerUs = SystemCoreClock / 1000000u;
    return cyclesPerUs ? (cycles / cyclesPerUs) : 0;
}

void ScriptEngine::pushResultTable(lua_State* L,
                                   bool ok,
                                   uint16_t address,
                                   uint16_t data,
                                   bool ready,
                                   bool irq,
                                   uint32_t timeUs,
                                   const char* error,
                                   bool hasExpected,
                                   uint16_t expected,
                                   uint16_t mask,
                                   bool matched) {
    lua_newtable(L);

    lua_pushboolean(L, ok); lua_setfield(L, -2, "ok");
    lua_pushinteger(L, address); lua_setfield(L, -2, "address");
    lua_pushinteger(L, data); lua_setfield(L, -2, "data");
    lua_pushboolean(L, ready); lua_setfield(L, -2, "ready");
    lua_pushboolean(L, irq); lua_setfield(L, -2, "irq");
    lua_pushinteger(L, timeUs); lua_setfield(L, -2, "time_us");

    if (error) lua_pushstring(L, error);
    else lua_pushnil(L);
    lua_setfield(L, -2, "error");

    if (hasExpected) {
        lua_pushinteger(L, expected); lua_setfield(L, -2, "expected");
        lua_pushinteger(L, mask); lua_setfield(L, -2, "mask");
        lua_pushboolean(L, matched); lua_setfield(L, -2, "matched");
    }
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

int ScriptEngine::l_bus_write_ex(lua_State* L) {
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
    const uint16_t data = (uint16_t)luaL_checkinteger(L, 2);
    const uint32_t started = DWT->CYCCNT;
    const bool ok = BusEngine::instance().write(addr, data);
    const uint32_t timeUs = elapsedUs(started);
    const bool ready = BusEngine::instance().ready();
    const bool irq = BusEngine::instance().irq();

    if (instance_) {
        char line[128];
        snprintf(line, sizeof(line), "[BUS] WRITE_EX addr=0x%04X data=0x%04X %s time=%lu us READY=%u IRQ=%u\n",
                 addr, data, ok ? "OK" : "TIMEOUT", (unsigned long)timeUs, ready ? 1u : 0u, irq ? 1u : 0u);
        instance_->append(line);
    }

    pushResultTable(L, ok, addr, data, ready, irq, timeUs, ok ? nullptr : "bus write timeout");
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

int ScriptEngine::l_bus_read_ex(lua_State* L) {
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t value = 0;
    const uint32_t started = DWT->CYCCNT;
    const bool ok = BusEngine::instance().read(addr, value);
    const uint32_t timeUs = elapsedUs(started);
    const bool ready = BusEngine::instance().ready();
    const bool irq = BusEngine::instance().irq();

    if (instance_) {
        char line[128];
        snprintf(line, sizeof(line), "[BUS] READ_EX addr=0x%04X -> 0x%04X %s time=%lu us READY=%u IRQ=%u\n",
                 addr, value, ok ? "OK" : "TIMEOUT", (unsigned long)timeUs, ready ? 1u : 0u, irq ? 1u : 0u);
        instance_->append(line);
    }

    pushResultTable(L, ok, addr, value, ready, irq, timeUs, ok ? nullptr : "bus read timeout");
    return 1;
}

int ScriptEngine::l_bus_expect(lua_State* L) {
    const uint16_t addr = (uint16_t)luaL_checkinteger(L, 1);
    const uint16_t expected = (uint16_t)luaL_checkinteger(L, 2);
    const uint16_t mask = (uint16_t)luaL_optinteger(L, 3, 0xFFFFu);

    uint16_t value = 0;
    const uint32_t started = DWT->CYCCNT;
    const bool readOk = BusEngine::instance().read(addr, value);
    const uint32_t timeUs = elapsedUs(started);
    const bool ready = BusEngine::instance().ready();
    const bool irq = BusEngine::instance().irq();
    const bool matched = readOk && ((value & mask) == (expected & mask));
    const bool ok = readOk && matched;
    const char* error = !readOk ? "bus read timeout" : (!matched ? "value mismatch" : nullptr);

    if (instance_) {
        char line[160];
        snprintf(line, sizeof(line),
                 "[BUS] EXPECT addr=0x%04X expected=0x%04X actual=0x%04X mask=0x%04X %s time=%lu us\n",
                 addr, expected, value, mask, ok ? "OK" : (readOk ? "MISMATCH" : "TIMEOUT"),
                 (unsigned long)timeUs);
        instance_->append(line);
    }

    pushResultTable(L, ok, addr, value, ready, irq, timeUs, error, true, expected, mask, matched);
    return 1;
}

int ScriptEngine::l_bus_ready(lua_State* L) {
    lua_pushboolean(L, BusEngine::instance().ready());
    return 1;
}

int ScriptEngine::l_bus_wait_ready(lua_State* L) {
    const lua_Integer timeoutUs = luaL_checkinteger(L, 1);
    if (timeoutUs < 0) return luaL_error(L, "timeout_us must be >= 0");

    const bool ok = BusEngine::instance().waitReady((uint32_t)timeoutUs);
    if (instance_) {
        char line[96];
        snprintf(line, sizeof(line), "[BUS] WAIT_READY %ld us -> %s\n", (long)timeoutUs, ok ? "READY" : "TIMEOUT");
        instance_->append(line);
    }
    lua_pushboolean(L, ok);
    return 1;
}

int ScriptEngine::l_bus_irq(lua_State* L) {
    lua_pushboolean(L, BusEngine::instance().irq());
    return 1;
}

int ScriptEngine::l_bus_timing(lua_State* L) {
    const lua_Integer setupUs = luaL_checkinteger(L, 1);
    const lua_Integer pulseUs = luaL_checkinteger(L, 2);
    const lua_Integer holdUs = luaL_checkinteger(L, 3);
    if (setupUs < 0 || pulseUs < 0 || holdUs < 0) {
        return luaL_error(L, "timing values must be >= 0");
    }

    BusEngine::instance().setWriteTimingUs((uint32_t)setupUs, (uint32_t)pulseUs, (uint32_t)holdUs);
    if (instance_) {
        char line[112];
        snprintf(line, sizeof(line), "[BUS] timing setup=%ld us pulse=%ld us hold=%ld us\n",
                 (long)setupUs, (long)pulseUs, (long)holdUs);
        instance_->append(line);
    }
    return 0;
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
    lua_pushcfunction(L_, l_bus_write_ex);    lua_setfield(L_, -2, "write_ex");
    lua_pushcfunction(L_, l_bus_read);        lua_setfield(L_, -2, "read");
    lua_pushcfunction(L_, l_bus_read_ex);     lua_setfield(L_, -2, "read_ex");
    lua_pushcfunction(L_, l_bus_expect);      lua_setfield(L_, -2, "expect");
    lua_pushcfunction(L_, l_bus_ready);       lua_setfield(L_, -2, "ready");
    lua_pushcfunction(L_, l_bus_wait_ready);  lua_setfield(L_, -2, "wait_ready");
    lua_pushcfunction(L_, l_bus_irq);         lua_setfield(L_, -2, "irq");
    lua_pushcfunction(L_, l_bus_timing);      lua_setfield(L_, -2, "timing");
    lua_pushcfunction(L_, l_bus_invert_data); lua_setfield(L_, -2, "invert_data");
    lua_pushcfunction(L_, l_bus_invert_addr); lua_setfield(L_, -2, "invert_addr");
    lua_setglobal(L_, "bus");
}
