add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
if is_host("windows") then
    set_defaultplat("mingw")
end

set_languages("c++23")
set_warnings("all")
set_policy("build.c++.modules", true)
set_policy("build.c++.modules.std", false)

local function demo_targets()
    return {
        "future_promise_demo",
        "lazy_demo",
        "uthread_guard_demo",
        "test_module_demo",
        "minifuture_demo"
    }
end

local function clangd_sync_targets()
    return demo_targets()
end

local function clangd_builddir()
    return "build_clangd"
end

local function default_builddir()
    return "build_run"
end

local function get_clang_bin()
    return "D:/SystemEnvironment/LLVM/bin"
end

local function get_mingw_sdk()
    return "D:/SystemEnvironment/LLVM"
end

local function get_mingw_triplet()
    return "x86_64-w64-mingw32"
end

local function get_runtime_dll_dir()
    if is_plat("mingw") then
        return path.join(get_mingw_sdk(), get_mingw_triplet(), "bin")
    end
    return get_clang_bin()
end

if is_plat("mingw") then
    set_toolchains("mingw", {clang = true, mingw = get_mingw_sdk()})
    add_cxflags("-finput-charset=UTF-8", "-fexec-charset=UTF-8", {tools = {"clang", "clangxx"}})
elseif is_plat("windows") then
    local compilerBin = get_clang_bin()
    set_toolset("cc", compilerBin .. "/clang.exe")
    set_toolset("cxx", compilerBin .. "/clang++.exe")
    set_toolset("ld", compilerBin .. "/clang++.exe")
    set_toolset("sh", compilerBin .. "/clang++.exe")
    add_cxflags("-finput-charset=UTF-8", "-fexec-charset=UTF-8", {tools = {"clang", "clangxx"}})
end

target("lazy_demo")
    set_kind("binary")
    add_includedirs(".")
    add_files(
        "D:/SystemEnvironment/LLVM/share/libc++/v1/std.cppm",
        "async_simple/experimental/coroutine.cppm",
        "async_simple/common.cppm",
        "async_simple/executor.cppm",
        "async_simple/signal.cppm",
        "async_simple/unit.cppm",
        "async_simple/try.cppm",
        "async_simple/local_state.cppm",
        "async_simple/future_state.cppm",
        "async_simple/util/move_only_function.cppm",
        "async_simple/future.cppm",
        "async_simple/util/condition.cppm",
        "async_simple/util/queue.cppm",
        "async_simple/util/thread_pool.cppm",
        "async_simple/coro/detached_coroutine.cppm",
        "async_simple/coro/lazy_local.cppm",
        "async_simple/coro/lazy.cppm",
        "async_simple/coro/future_awaiter.cppm",
        "async_simple/coro/collect.cppm",
        "async_simple/coro/mutex.cppm",
        "async_simple/coro/condition_variable.cppm",
        "async_simple/coro/latch.cppm",
        "async_simple/coro/semaphore.cppm",
        "async_simple/coro/sync_await.cppm",
        "async_simple/executors/simple_executor.cppm",
        "demo/lazy_demo.cpp"
    )

    on_link(function (target)
        local clangxx = get_clang_bin() .. "/clang++.exe"
        local argv = {"-o", target:targetfile()}
        table.join2(argv, target:objectfiles())
        if is_mode("debug") then
            table.insert(argv, "-g")
        end
        os.mkdir(path.directory(target:targetfile()))
        os.vrunv(clangxx, argv)
    end)

    after_build(function (target)
        local runtimeDir = get_runtime_dll_dir()
        local runtimeDlls = {
            "libc++.dll",
            "libunwind.dll",
            "libwinpthread-1.dll"
        }
        for _, dll in ipairs(runtimeDlls) do
            local src = path.join(runtimeDir, dll)
            if os.isfile(src) then
                os.cp(src, path.join(path.directory(target:targetfile()), dll))
            end
        end
    end)

target("future_promise_demo")
    set_kind("binary")
    add_includedirs(".")
    add_files(
        "D:/SystemEnvironment/LLVM/share/libc++/v1/std.cppm",
        "async_simple/experimental/coroutine.cppm",
        "async_simple/common.cppm",
        "async_simple/executor.cppm",
        "async_simple/signal.cppm",
        "async_simple/unit.cppm",
        "async_simple/try.cppm",
        "async_simple/local_state.cppm",
        "async_simple/future_state.cppm",
        "async_simple/collect.cppm",
        "async_simple/util/queue.cppm",
        "async_simple/util/move_only_function.cppm",
        "async_simple/util/thread_pool.cppm",
        "async_simple/future.cppm",
        "async_simple/executors/simple_executor.cppm",
        "demo/future_promise_demo.cpp"
    )

    on_link(function (target)
        local clangxx = get_clang_bin() .. "/clang++.exe"
        local argv = {"-o", target:targetfile()}
        table.join2(argv, target:objectfiles())
        if is_mode("debug") then
            table.insert(argv, "-g")
        end
        os.mkdir(path.directory(target:targetfile()))
        os.vrunv(clangxx, argv)
    end)

    after_build(function (target)
        local runtimeDir = get_runtime_dll_dir()
        local runtimeDlls = {
            "libc++.dll",
            "libunwind.dll",
            "libwinpthread-1.dll"
        }
        for _, dll in ipairs(runtimeDlls) do
            local src = path.join(runtimeDir, dll)
            if os.isfile(src) then
                os.cp(src, path.join(path.directory(target:targetfile()), dll))
            end
        end
    end)

target("uthread_guard_demo")
    set_kind("binary")
    add_includedirs(".")
    add_files(
        "D:/SystemEnvironment/LLVM/share/libc++/v1/std.cppm",
        "async_simple/experimental/coroutine.cppm",
        "async_simple/common.cppm",
        "async_simple/executor.cppm",
        "async_simple/signal.cppm",
        "async_simple/unit.cppm",
        "async_simple/try.cppm",
        "async_simple/local_state.cppm",
        "async_simple/future_state.cppm",
        "async_simple/coro/detached_coroutine.cppm",
        "async_simple/coro/lazy_local.cppm",
        "async_simple/coro/lazy.cppm",
        "async_simple/util/queue.cppm",
        "async_simple/util/move_only_function.cppm",
        "async_simple/util/thread_pool.cppm",
        "async_simple/future.cppm",
        "async_simple/executors/simple_executor.cppm",
        "async_simple/uthread/runtime.cppm",
        "async_simple/uthread/uthread.cppm",
        "async_simple/uthread/async.cppm",
        "async_simple/uthread/await.cppm",
        "demo/uthread_guard_demo.cpp"
    )

    on_link(function (target)
        local clangxx = get_clang_bin() .. "/clang++.exe"
        local argv = {"-o", target:targetfile()}
        table.join2(argv, target:objectfiles())
        if is_mode("debug") then
            table.insert(argv, "-g")
        end
        os.mkdir(path.directory(target:targetfile()))
        os.vrunv(clangxx, argv)
    end)

    after_build(function (target)
        local runtimeDir = get_runtime_dll_dir()
        local runtimeDlls = {
            "libc++.dll",
            "libunwind.dll",
            "libwinpthread-1.dll"
        }
        for _, dll in ipairs(runtimeDlls) do
            local src = path.join(runtimeDir, dll)
            if os.isfile(src) then
                os.cp(src, path.join(path.directory(target:targetfile()), dll))
            end
        end
    end)

target("test_module_demo")
    set_kind("binary")
    add_includedirs(".")
    add_files(
        "async_simple/test_a.cppm",
        "async_simple/test_b.cppm",
        "demo/test_module_demo.cpp"
    )

    on_link(function (target)
        local clangxx = get_clang_bin() .. "/clang++.exe"
        local argv = {"-o", target:targetfile()}
        table.join2(argv, target:objectfiles())
        if is_mode("debug") then
            table.insert(argv, "-g")
        end
        os.mkdir(path.directory(target:targetfile()))
        os.vrunv(clangxx, argv)
    end)

target("minifuture_demo")
    set_kind("binary")
    add_includedirs(".")
    add_files(
        "D:/SystemEnvironment/LLVM/share/libc++/v1/std.cppm",
        "async_simple/minifuture.cppm",
        "demo/minifuture_demo.cpp"
    )

    on_link(function (target)
        local clangxx = get_clang_bin() .. "/clang++.exe"
        local argv = {"-o", target:targetfile()}
        table.join2(argv, target:objectfiles())
        if is_mode("debug") then
            table.insert(argv, "-g")
        end
        os.mkdir(path.directory(target:targetfile()))
        os.vrunv(clangxx, argv)
    end)

task("clangd")
    set_category("plugin")
    set_menu {
        usage = "xmake clangd",
        description = "Refresh compile_commands.json and build module artifacts for clangd in an isolated build directory.",
    }

    on_run(function ()
        local xmake = os.programfile()
        local clangdBuildDir = clangd_builddir()
        local normalBuildDir = default_builddir()
        os.execv(xmake, {"f", "-c", "--builddir=" .. clangdBuildDir})
        os.execv(xmake, {"project", "-k", "compile_commands", "--lsp=clangd"})
        for _, targetname in ipairs(clangd_sync_targets()) do
            os.execv(xmake, {"build", targetname})
        end
        os.execv(xmake, {"f", "-c", "--builddir=" .. normalBuildDir})
    end)

task("demos")
    set_category("plugin")
    set_menu {
        usage = "xmake demos",
        description = "Build all demo targets and then refresh clangd artifacts.",
    }

    on_run(function ()
        local xmake = os.programfile()
        for _, targetname in ipairs(demo_targets()) do
            os.execv(xmake, {"build", targetname})
        end
        os.execv(xmake, {"clangd"})
    end)
