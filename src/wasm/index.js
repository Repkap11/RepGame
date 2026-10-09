//clear_fullscreen_functions();
set_canvas_size();

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
var errorOverlay = null;
function showError(msg) {
  console.error(msg);
  if (!errorOverlay) {
    errorOverlay = document.createElement("div");
    errorOverlay.style.cssText = "position:fixed;top:0;left:0;right:0;z-index:9999;background:red;color:white;font-family:monospace;font-size:12px;padding:8px;white-space:pre-wrap;max-height:50vh;overflow:auto;";
    document.body.appendChild(errorOverlay);
  }
  errorOverlay.textContent += msg + "\n";
}
window.addEventListener("error", function(e) {
  // emscripten_set_main_loop throws a C++ "unwind" exception each frame to
  // yield to the browser event loop. It's caught internally by emscripten,
  // but the global error handler fires first. Suppress it.
  if (e.message && e.message.includes("unwind")) return;
  showError(e.message + (e.filename ? " (" + e.filename + ":" + e.lineno + ")" : ""));
});
window.addEventListener("unhandledrejection", function(e) {
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
// the saved world (/repgame_wasm/World1 — chunk_* files plus player.dat)
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

// When pointer lock is active, ESC is consumed by the browser to break
// pointer lock — the game never sees the keypress. Listen for the resulting
// pointerlockchange and exit the game. Skip if the window lost focus (alt-tab),
// since that also breaks pointer lock but isn't an exit request. Also skip
// if the game released pointer lock on purpose (e.g. inventory open).
document.addEventListener("pointerlockchange", function() {
  if (gameRunning && !document.pointerLockElement && document.hasFocus()) {
    if (!gameWantsPointerLock) {
      // The game intentionally released pointer lock (e.g. inventory). Don't exit.
    } else {
      // The game wants pointer lock but lost it — this is ESC. Exit the game.
      request_exit();
    }
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
  noInitialRun: true
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

  function callback(event) {
    console.log("Got a press");
    canvas.requestPointerLock();
    if (first_time) {
      document.addEventListener("keydown", callback);

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
