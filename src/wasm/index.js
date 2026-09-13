//clear_fullscreen_functions();
set_canvas_size();

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

var Module = {
  print: function(msg) { console.log(msg); },
  printErr: function(msg) {
    // emscripten_set_main_loop uses a C++ exception to unwind back to the
    // browser event loop each frame. This shows up as "unwind" in printErr
    // and is harmless, so suppress it to avoid cluttering the error overlay.
    if (msg === "unwind") return;
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
  setup_click_handler();
  draw_mark();
  canvas.focus();
  window.onresize = set_canvas_size;
}

function reset_canvas() {
  var canvas_holder = document.getElementById("canvas_holder");
  while (canvas_holder.firstChild) {
    canvas_holder.removeChild(canvas_holder.firstChild);
  }
  var canvas = document.createElement("canvas");
  canvas.id = "canvas";
  canvas.addEventListener(
    "contextmenu",
    function(event) {
      event.preventDefault();
    },
    false
  );
  canvas_holder.appendChild(canvas);
  set_canvas_size();
  Module.canvas = canvas;
}

function draw_mark() {
  // Print out some pretty text on the canvas
  var intro = document.getElementById("canvas");
  intro.width = window.innerWidth;
  intro.height = window.innerHeight;

  var ctx = intro.getContext("2d");
  var img = new Image();
  img.onload = function() {
    var size = 400;
    ctx.drawImage(img, intro.width / 2 - size / 2, intro.height / 2 - size / 2, size, size);
  };
  img.src = "icon.png";
}

function setup_click_handler() {
  var first_time = 1;
  var canvas = document.getElementById("canvas");

  function callback(event) {
    console.log("Got a press");
    if (first_time) {
      reset_canvas();
    } else {
      console.log("got key" + event.keyCode);
      //F5
      if (event.keyCode == 116) {
        location.reload();
      }
      //Q
      if (event.keyCode == 81) {
        //Native game will stop, so release the okii lock
        //document.exitPointerLock();
        close();
      }
    }
    var canvas = document.getElementById("canvas");

    // var hasPointerLock = document.pointerLockElement === canvas;
    // if (!hasPointerLock) {
    canvas.requestPointerLock();
    // }
    if (first_time) {
      document.addEventListener("keydown", callback);

      first_time = 0;
      FS.mkdir("/repgame_wasm");
      FS.mount(IDBFS, {}, "/repgame_wasm");
      FS.syncfs(true, err => {
        console.log("Before:", err);
        Module.ccall("main");
      });
    }
  }
  // Click works on both desktop and mobile. A user gesture is required to
  // request pointer lock, so the game can't start automatically.
  document.addEventListener("click", callback);
}

// Webkit/Blink will fire this on load, but Gecko doesn't.
// So we fire it manually...
