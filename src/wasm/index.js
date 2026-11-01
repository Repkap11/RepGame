//clear_fullscreen_functions();
set_canvas_size();

// Report WebGPU availability on the startup screen. Same probe the C++ side
// uses (navigator.gpu + requestAdapter), so the message predicts whether the
// GPU mapgen backend will activate or chunks fall back to CPU. The backend is
// opt-in (?REPGAME_WEBGPU=1, matching getenv() in webgpu_mapgen.cpp) — when it
// isn't requested, don't touch WebGPU at all.
(function gpuStatusProbe() {
  var el = document.getElementById("gpu_status");
  if (!el) return;
  function show(text, unavailable) {
    el.textContent = text;
    el.className = unavailable ? "gpu-unavailable" : "";
  }
  var webgpuParam = parseInt(new URLSearchParams(location.search).get("REPGAME_WEBGPU"), 10);
  if (!(webgpuParam > 0)) {
    show("GPU-accelerated terrain gen disabled (add ?REPGAME_WEBGPU=1 to enable)", true);
    return;
  }
  if (!navigator.gpu || !navigator.gpu.requestAdapter) {
    show("WebGPU not supported by this browser - terrain gen will use the CPU", true);
    return;
  }
  navigator.gpu.requestAdapter().then(function(adapter) {
    if (adapter) {
      var name = "";
      try {
        var info = adapter.info || {};
        name = info.device || info.description || info.vendor || "";
      } catch (e) {}
      if (adapter.isFallbackAdapter) {
        // Chrome's software fallback is SwiftShader (its sanitized vendor
        // string is "google") - Vulkan emulated on the CPU, not a real GPU.
        show("WebGPU available - software adapter (SwiftShader), terrain gen runs on the CPU", true);
      } else {
        show("WebGPU available - GPU-accelerated terrain gen" + (name ? " (" + name + ")" : ""), false);
      }
    } else {
      show("WebGPU adapter unavailable - terrain gen will use the CPU", true);
    }
  }).catch(function() {
    show("WebGPU adapter unavailable - terrain gen will use the CPU", true);
  });
})();

// RepGame is developed and tested in Chrome; Firefox is not recommended
// (WebGPU off by default on some platforms, weaker wasm/pthread perf).
// Warn on the startup screen when Firefox is detected.
(function browserStatus() {
  var el = document.getElementById("browser_status");
  if (el && /Firefox|FxiOS/.test(navigator.userAgent)) {
    el.textContent = "RepGame runs best in Chrome - Firefox is not recommended.";
  }
})();

// Let F-keys (except F12, which the game uses for screenshots) pass through to
// the browser. SDL2/emscripten calls preventDefault on all key events, which
// steals F5 (reload), F11 (fullscreen), etc. This capture-phase listener runs
// before SDL2's handler and stops propagation for F1-F11 so the browser can
// handle them normally.
window.addEventListener("keydown", function(e) {
  // F1=112 ... F11=122, F12=123
  if (e.keyCode >= 112 && e.keyCode <= 122 && e.keyCode !== 123) {
    e.stopPropagation();
  }
}, true);

// On-screen error log for mobile debugging (no DevTools available).
// Errors are shown as a red overlay at the top of the page.
// Each message is its own entry that expires after ERROR_TIMEOUT_MS; at most
// ERROR_MAX_ENTRIES are shown (oldest dropped), repeats of a visible message
// bump its counter and restart its timer, and tapping an entry dismisses it.
var ERROR_TIMEOUT_MS = 10000;
var ERROR_MAX_ENTRIES = 5;
var errorOverlay = null;
var errorEntries = new Map();
function removeErrorEntry(msg) {
  var entry = errorEntries.get(msg);
  if (!entry) return;
  clearTimeout(entry.timer);
  entry.el.remove();
  errorEntries.delete(msg);
}
function showError(msg) {
  console.error(msg);
  msg = String(msg);
  if (!errorOverlay) {
    errorOverlay = document.createElement("div");
    errorOverlay.style.cssText = "position:fixed;top:0;left:0;right:0;z-index:9999;max-height:30vh;overflow:hidden;";
    document.body.appendChild(errorOverlay);
  }
  var entry = errorEntries.get(msg);
  if (entry) {
    entry.count++;
    entry.el.textContent = msg + " (x" + entry.count + ")";
    clearTimeout(entry.timer);
    errorEntries.delete(msg); // Re-insert so it counts as the newest.
  } else {
    var el = document.createElement("div");
    el.style.cssText = "background:red;color:white;font-family:monospace;font-size:12px;padding:4px 8px;margin-bottom:1px;white-space:pre-wrap;max-height:4.5em;overflow:hidden;cursor:pointer;";
    el.textContent = msg;
    el.addEventListener("click", function(e) {
      e.stopPropagation();
      removeErrorEntry(msg);
    });
    errorOverlay.appendChild(el);
    entry = { el: el, count: 1 };
  }
  errorOverlay.appendChild(entry.el); // Move to the bottom (newest).
  entry.timer = setTimeout(function() { removeErrorEntry(msg); }, ERROR_TIMEOUT_MS);
  errorEntries.set(msg, entry);
  while (errorEntries.size > ERROR_MAX_ENTRIES) {
    removeErrorEntry(errorEntries.keys().next().value);
  }
}
window.addEventListener("error", function(e) {
  // emscripten_set_main_loop throws a C++ "unwind" exception each frame to
  // yield to the browser event loop. It's caught internally by emscripten,
  // but the global error handler fires first. Suppress it.
  if (e.message && e.message.includes("unwind")) return;
  showError(e.message + (e.filename ? " (" + e.filename + ":" + e.lineno + ")" : ""));
});
window.addEventListener("unhandledrejection", function(e) {
  // SDL's emscripten port calls requestPointerLock() without handling the
  // returned promise; toggling the inventory/menu quickly trips the browser's
  // re-lock cooldown. Harmless — the next input retries — so keep it off the
  // overlay.
  var reasonMsg = e.reason && e.reason.message ? e.reason.message : String(e.reason);
  if (/pointer ?lock|exited the lock/i.test(reasonMsg)) {
    console.warn("Ignored pointer lock rejection: " + reasonMsg);
    e.preventDefault();
    return;
  }
  showError("Promise rejection: " + (e.reason && e.reason.message ? e.reason.message : e.reason));
});

// Called from C++ when the game exits. Replace the page to restore the
// startup page (title, download links, click-to-start). Using replace()
// instead of reload() replaces the current history entry, so no forward
// button appears after exiting.
function show_exit_screen() {
  document.exitPointerLock();
  gameRunning = false;
  location.replace(location.href);
}

// Ask the game to exit: sets exitGame so the main loop runs cleanup()
// (saving world + player data to OPFS) and then calls show_exit_screen().
function request_exit() {
  Module.ccall("repgame_wasm_request_exit");
}

// The wasmfs OPFS backend mounts the browser's OPFS root at /repgame_wasm, so
// the saved world (/repgame_wasm/World1 - chunk_* files plus player.dat)
// appears as a top-level "World1" directory in OPFS. Deleting it recursively
// resets the world exactly like `rm -rf World1` on the native build. This can
// run before the module even loads since it talks to OPFS directly.
async function clearSavedWorld() {
  var status = document.getElementById("clear_status");
  if (!window.confirm("Delete the saved world and player data? This cannot be undone.")) {
    return;
  }
  if (!navigator.storage || !navigator.storage.getDirectory) {
    status.textContent = "Nothing to clear (persistent storage unsupported).";
    return;
  }
  try {
    var root = await navigator.storage.getDirectory();
    await root.removeEntry("World1", { recursive: true });
    status.textContent = "Saved world deleted.";
    console.log("Deleted OPFS World1");
  } catch (e) {
    if (e && e.name === "NotFoundError") {
      status.textContent = "No saved world found.";
    } else {
      status.textContent = "Clear failed: " + e.message;
      showError("clearSavedWorld: " + (e && e.message ? e.message : e));
    }
  }
}

document.getElementById("clear_button").addEventListener("click", function(e) {
  e.stopPropagation(); // Never let this count as a play gesture.
  clearSavedWorld();
});

// Multiplayer server as a WebSocket URL. Override with ?server=ws://host:port/
// or disable multiplayer entirely with ?server=off. Defaults:
//  - served from localhost: a local websockify bridge on :25567 (in front of
//    a `make server` TCP server on :25566).
//  - anywhere else (e.g. repkap11.com): wss://<host>/repgame-ws, where nginx
//    terminates TLS and proxies to websockify -> TCP :25566.
function repgameServerUrl() {
  var param = new URLSearchParams(location.search).get("server");
  if (param === "off") return null;
  if (param) return param;
  if (location.hostname === "localhost" || location.hostname === "127.0.0.1") {
    return "ws://" + location.hostname + ":25567";
  }
  return "wss://" + location.host + "/repgame-ws";
}

// Called from C++ once the /repgame_wasm OPFS mount is ready (or failed, in
// which case it fell back to in-memory and the game runs without saving).
function repgameStorageReady(persistent) {
  console.log("World storage ready, persistent:", persistent);
  var serverUrl = repgameServerUrl();
  if (serverUrl) {
    console.log("Multiplayer server:", serverUrl);
    Module.ccall("repgame_wasm_set_server", null, ["string"], [serverUrl]);
  }
  // Defer: this runs inside a worker->main-thread proxied op, so
  // PThread.currentProxiedOperationCallerThread is the (soon-exited) storage
  // thread. SDL's html5 event handlers register with
  // EM_CALLBACK_THREAD_CONTEXT_CALLING_THREAD and would capture that dead
  // thread as their dispatch target; every later DOM event would then fail
  // emscripten_proxy_async. A fresh task sees a clean calling context.
  setTimeout(function() { Module.ccall("main"); }, 0);
}

// Ask the browser not to evict our OPFS data under storage pressure.
if (navigator.storage && navigator.storage.persist) {
  navigator.storage.persist();
}

// Track whether the game is running so we only react to pointer lock loss
// during gameplay, not on the intro page.
var gameRunning = false;
// Updated by C++ every frame: true when the game wants pointer lock (normal
// gameplay), false when it doesn't (inventory open). The pointerlockchange
// listener uses this to distinguish ESC from game-initiated unlocks.
var gameWantsPointerLock = false;
// pointerlockchange events arrive asynchronously, so an unlock event
// generated while the game briefly wanted the lock off (inventory open)
// can be delivered *after* the game already flipped the flag back on and
// requested the lock again - a stale unlock arriving under flag=1 looks
// exactly like an ESC and used to request_exit() mid-toggle. Only count an
// unlock as ESC if the pointer was actually observed locked during the
// current "wants lock" epoch.
var hadPointerLockForCurrentWant = false;
// Set once any lock is granted. Devices without pointer lock (touch) fail
// every request, so only treat failures as recoverable where locking works.
var pointerLockSupported = false;

// Called from C++ every frame via EM_ASM.
function repgame_set_game_wants_pointer_lock(wants) {
  if (wants && !gameWantsPointerLock) {
    // Starting a new "wants lock" epoch: wait for a fresh acquire event
    // before trusting later unlock events.
    hadPointerLockForCurrentWant = false;
  }
  gameWantsPointerLock = !!wants;
}

// When pointer lock is active, ESC is consumed by the browser to break
// pointer lock - the game never sees the keypress. Listen for the resulting
// pointerlockchange and exit the game. Skip if the window lost focus (alt-tab),
// since that also breaks pointer lock but isn't an exit request. Also skip
// if the game released pointer lock on purpose (e.g. inventory open).
document.addEventListener("pointerlockchange", function() {
  if (document.pointerLockElement) {
    hadPointerLockForCurrentWant = true;
    pointerLockSupported = true;
    return;
  }
  if (!gameRunning || !document.hasFocus()) {
    return;
  }
  if (!gameWantsPointerLock) {
    // The game intentionally released pointer lock (e.g. inventory). Don't exit.
    return;
  }
  if (!hadPointerLockForCurrentWant) {
    // Stale unlock racing a just-issued lock request (rapid inventory
    // toggles) - the pointer was never locked under the current flag, so
    // this isn't an ESC.
    return;
  }
  // The game wants pointer lock, had it, and lost it - this is ESC.
  Module.ccall("repgame_wasm_request_menu");
});

// A lock request the browser refused (e.g. the ~1s re-lock cooldown after
// closing the inventory/menu quickly) would leave the game in play mode with
// a free cursor. Fall back to the pause menu instead; Resume retries the lock
// from a fresh click. Ignored while the game doesn't want the lock (the user
// already reopened a menu) or once the lock is held. Gated-close failures
// are reported per request via repgame_gate_request_pointer_lock instead.
document.addEventListener("pointerlockerror", function() {
  if (gameRunning && pointerLockSupported && gameWantsPointerLock && !document.pointerLockElement) {
    Module.ccall("repgame_wasm_request_menu");
  }
});

// Called from C++ (the pointer-lock gate) to request a lock whose result is
// reported back per request id via repgame_wasm_pointer_lock_result, so a
// stale rejection can't be confused with the latest request.
function repgame_gate_request_pointer_lock(id) {
  var canvas = Module.canvas || document.getElementById("canvas");
  function report(ok) {
    Module.ccall("repgame_wasm_pointer_lock_result", null, ["number", "number"], [id, ok]);
  }
  var r = canvas.requestPointerLock();
  if (r && r.then) {
    r.then(function() { report(1); }, function() { report(0); });
  } else {
    // Older browsers return undefined: watch for a one-shot pointerlockerror,
    // removed again if a lock change arrives first.
    var onError = function() {
      document.removeEventListener("pointerlockchange", onChange);
      report(0);
    };
    var onChange = function() { document.removeEventListener("pointerlockerror", onError); };
    document.addEventListener("pointerlockerror", onError, { once: true });
    document.addEventListener("pointerlockchange", onChange, { once: true });
  }
}

// Browsers that do deliver the lock-breaking ESC keydown to the page would
// otherwise let SDL toggle the menu right after the pointerlockchange path
// opened it (closing it again). Swallow ESC while locked and open the menu
// here instead; repgame_wasm_request_menu is idempotent.
window.addEventListener("keydown", function(e) {
  if (gameRunning && e.key === "Escape" && document.pointerLockElement) {
    e.stopPropagation();
    Module.ccall("repgame_wasm_request_menu");
  }
}, true);

// When the pointer isn't locked (e.g. inventory open), a right-click opens
// the browser's context menu. The menu swallows the mouseup, so SDL never
// sees the button released and the game treats right-click as held forever
// (continuous block placement, broken inventory clicks). Suppress the menu
// during gameplay so down/up events always arrive in pairs.
document.addEventListener("contextmenu", function(e) {
  if (gameRunning) {
    e.preventDefault();
  }
});

// On mobile, the back button should exit the game back to the startup page.
// Push a history state when the game starts so pressing back fires popstate
// instead of navigating away from the page.
window.addEventListener("popstate", function() {
  if (gameRunning) {
    // Back button was pressed during the game. Exit the game (the C++ cleanup
    // path calls show_exit_screen, which uses location.replace() and so
    // clears forward entries).
    request_exit();
  }
});

var Module = {
  print: function(msg) { console.log(msg); },
  printErr: function(msg) {
    // emscripten_set_main_loop uses a C++ exception to unwind back to the
    // browser event loop each frame. This shows up as "unwind" in printErr
    // and is harmless, so suppress it to avoid cluttering the error overlay.
    if (msg === "unwind") return;
    // WasmFS ops on the OPFS-backed /repgame_wasm proxy to a dedicated worker
    // and briefly block the main thread by design; emscripten warns about it
    // once ("Blocking on the main thread is very dangerous..."). Harmless
    // here, so don't show it on the overlay.
    if (typeof msg === "string" && msg.includes("Blocking on the main thread")) return;
    showError(msg);
  },
  onRuntimeInitialized: onModuleReady,
  noInitialRun: true,
  // Forward REPGAME_* URL query params into Emscripten's ENV so getenv()
  // sees them (e.g. REPGAME_CAPTURE, REPGAME_MSAA). ENV is a top-level var in
  // the non-modularized RepGame.js, so it's already defined when preRun runs.
  preRun: [function() {
    var params = new URLSearchParams(location.search);
    params.forEach(function(value, key) {
      if (key.indexOf("REPGAME_") === 0) {
        ENV[key] = value;
      }
    });
  }]
};

var script = document.createElement("script");
script.src = "RepGame.js";
document.body.appendChild(script);

function clear_fullscreen_functions() {
  //   document["exitFullscreen"] = function() {};
  //   document["cancelFullScreen"] = function() {};
  //   document["mozCancelFullScreen"] = function() {};
  //   document["webkitCancelFullScreen"] = function() {};
}

function set_canvas_size() {
  var canvas = document.getElementById("canvas");
  canvas.style.width = window.innerWidth + "px";
  canvas.style.height = window.innerHeight + "px";
}

function onModuleReady() {
  console.log("Module ready");
  Module.canvas = document.getElementById("canvas");
  setup_click_handler();
  window.onresize = set_canvas_size;
}

function setup_click_handler() {
  var first_time = 1;
  var canvas = document.getElementById("canvas");
  var playButton = document.getElementById("play_button");

  // Pointer lock can only be (re-)acquired inside a user gesture, so request
  // it on input events — needed to re-lock after the browser drops the lock
  // without the game asking (alt-tab). But only when the game wants it:
  // locking while the inventory is open hides the cursor and freezes the
  // mouse position, breaking clicks on inventory slots (e.g. pressing Shift
  // to shift-click). Keys that *create* the want — 'e' closing the
  // inventory — only flip gameWantsPointerLock on the next game tick, so
  // re-check once shortly after the keypress while its user activation
  // still applies.
  function request_pointer_lock_if_wanted() {
    if (gameWantsPointerLock && !document.pointerLockElement) {
      var result = canvas.requestPointerLock();
      if (result && result.catch) {
        // Rejected requests (gesture cooldowns, platforms without pointer
        // lock) are fine — the next input retries. Keep them off the error
        // overlay.
        result.catch(function() {});
      }
    }
  }

  function callback(event) {
    console.log("Got a press");
    var lockResult = canvas.requestPointerLock();
    if (lockResult && lockResult.catch) lockResult.catch(function() {});
    if (first_time) {
      document.addEventListener("keydown", function() {
        request_pointer_lock_if_wanted();
        setTimeout(request_pointer_lock_if_wanted, 50);
      });
      canvas.addEventListener("mousedown", request_pointer_lock_if_wanted);

      first_time = 0;
      gameRunning = true;
      // Hide the startup screen so the game canvas is visible.
      document.getElementById("startup").classList.add("hidden");
      document.getElementById("download").classList.add("hidden");
      // Push a history state so the back button fires popstate (which exits
      // the game) instead of navigating away from the page.
      history.pushState({ game: true }, "");
      // Mount /repgame_wasm on OPFS (done on a helper thread in C++ since the
      // backend constructor can't run on the browser main thread). C++ calls
      // repgameStorageReady() when the mount is in place.
      Module.ccall("repgame_wasm_start_storage");
    }
  }
  // A user gesture is required to request pointer lock, so the game can't
  // start automatically. Listen on the button so download links still work.
  playButton.addEventListener("click", callback);
}

// Webkit/Blink will fire this on load, but Gecko doesn't.
// So we fire it manually...
