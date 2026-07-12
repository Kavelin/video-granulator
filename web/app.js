function videoGranulatorApp(Module) {
  const upload = document.getElementById("video-upload");
  const status = document.getElementById("status");
  const canvas = Module.canvas;
  const extractionCanvas = document.createElement("canvas");
  const extractionCtx = extractionCanvas.getContext("2d", {
    willReadFrequently: true,
  });
  const video = document.createElement("video");

  video.preload = "auto";
  video.muted = true;
  video.playsInline = true;
  video.crossOrigin = "anonymous";
  video.style.display = "none";

  let sourceUrl = null;
  let completed = false;
  let extractedFrameCount = 0;
  let frameCapacity = 0;
  let frameWidth = 0;
  let frameHeight = 0;
  let rgbBuffer = null;

  function setStatus(message) {
    if (status) {
      status.textContent = message;
    }
  }

  function estimateFps() {
    return 30;
  }

  function rgbaToRgb24(rgba, rgb) {
    let readIndex = 0;
    let writeIndex = 0;
    while (readIndex < rgba.length) {
      rgb[writeIndex++] = rgba[readIndex++];
      rgb[writeIndex++] = rgba[readIndex++];
      rgb[writeIndex++] = rgba[readIndex++];
      readIndex++;
    }
  }

  function scheduleFrame(callback) {
    if (typeof video.requestVideoFrameCallback === "function") {
      video.requestVideoFrameCallback(callback);
      return;
    }

    requestAnimationFrame((time) => {
      callback(time, {
        mediaTime: video.currentTime,
        presentedFrames: extractedFrameCount,
      });
    });
  }

  function setUpControls() {
    document.getElementById("spray-control").addEventListener("input", (e) => {
      Module._set_granulator_spray(parseInt(e.target.value, 10));
    });

    document.getElementById("step-control").addEventListener("input", (e) => {
      Module._set_granulator_step(parseInt(e.target.value, 10));
    });

    document.getElementById("grain-control").addEventListener("input", (e) => {
      Module._set_granulator_grain_frames(parseInt(e.target.value, 10));
    });
    document.getElementById("overlay-control").addEventListener("input", (e) => {
      Module._set_granulator_overlay(parseInt(e.target.value, 10));
    });
  }

  function finishExtraction() {
    console.log("hi");
    completed = true;
    video.pause();
    Module._set_frame_count(extractedFrameCount);
    setStatus(
      `Extracted ${extractedFrameCount} frames. Launching granulator...`,
    );
    setUpControls();
    Module.ccall("video_granulator_run", "number", [], []);
    setStatus("Playback started.");
  }

  function captureFrame() {
    if (completed) {
      return;
    }

    if (!frameWidth || !frameHeight) {
      return;
    }

    extractionCtx.drawImage(video, 0, 0, frameWidth, frameHeight);
    const rgba = extractionCtx.getImageData(0, 0, frameWidth, frameHeight).data;
    rgbaToRgb24(rgba, rgbBuffer);

    const ptr = Module._get_frame_buffer_pointer(extractedFrameCount);
    if (ptr !== 0) {
      console.log("hello?");
      Module.HEAPU8.set(rgbBuffer, ptr);
      extractedFrameCount++;
    } else {
      setStatus("Frame buffer pointer lookup failed.");
      completed = true;
      return;
    }

    if (!video.ended && extractedFrameCount < frameCapacity) {
      scheduleFrame(captureFrame);
      return;
    }

    finishExtraction();
  }

  async function extractAllFrames() {
    const fps = estimateFps();
    const frameDuration = 1 / fps;

    while (extractedFrameCount < frameCapacity && !video.ended) {
      video.currentTime = extractedFrameCount * frameDuration;

      await new Promise((resolve) => {
        if (typeof video.requestVideoFrameCallback === "function") {
          video.requestVideoFrameCallback(resolve);
        } else {
          video.addEventListener("seeked", resolve, { once: true });
        }
      });

      if (completed) break;

      extractionCtx.drawImage(video, 0, 0, frameWidth, frameHeight);
      const rgba = extractionCtx.getImageData(
        0,
        0,
        frameWidth,
        frameHeight,
      ).data;
      rgbaToRgb24(rgba, rgbBuffer);

      const ptr = Module._get_frame_buffer_pointer(extractedFrameCount);
      if (ptr !== 0) {
        Module.HEAPU8.set(rgbBuffer, ptr);
        extractedFrameCount++;
        setStatus(
          `Extracting frame ${extractedFrameCount}/${frameCapacity}...`,
        );
      } else {
        setStatus("Frame buffer pointer lookup failed.");
        completed = true;
        return;
      }
    }

    finishExtraction();
  }

  async function loadVideo(file) {
    completed = false;
    extractedFrameCount = 0;
    frameCapacity = 0;
    frameWidth = 0;
    frameHeight = 0;
    rgbBuffer = null;

    if (sourceUrl) {
      URL.revokeObjectURL(sourceUrl);
      sourceUrl = null;
    }

    sourceUrl = URL.createObjectURL(file);
    video.src = sourceUrl;

    await new Promise((resolve) => {
      video.addEventListener("loadedmetadata", resolve, { once: true });
    });

    frameWidth = video.videoWidth;
    frameHeight = video.videoHeight;
    const fps = estimateFps();
    frameCapacity = Math.max(1, Math.ceil(video.duration * fps));
    rgbBuffer = new Uint8Array(frameWidth * frameHeight * 3);
    extractionCanvas.width = frameWidth;
    extractionCanvas.height = frameHeight;

    Module._prepare_frame_buffers(frameCapacity, frameWidth, frameHeight, fps);
    setStatus(`Extracting ${frameCapacity} frames sequentially...`);

    extractAllFrames();
  }

  upload.addEventListener("change", async (event) => {
    const file = event.target.files && event.target.files[0];
    if (!file) {
      return;
    }

    setStatus(`Loading ${file.name}...`);
    [...document.querySelectorAll("input[type=range]")].map(x=>x.value=x.getAttribute("data-initial"))
    await loadVideo(file);
  });

  canvas.oncontextmenu = (event) => event.preventDefault();
  setStatus("Wasm ready. Pick a video.");
  upload.disabled = false;
}

window.videoGranulatorApp = videoGranulatorApp;
