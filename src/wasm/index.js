//clear_fullscreen_functions();
set_canvas_size();

var Module = {
  print: console.log,
  printErr: console.error,
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
  document.addEventListener("keydown", callback);
  //canvas.onkeydown = callback;
}

// Webkit/Blink will fire this on load, but Gecko doesn't.
// So we fire it manually...
