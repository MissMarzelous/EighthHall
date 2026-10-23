/*
 * playground.js - the interactive reverb demo on the Eighth Hall page.
 * Plays a looping sample through the browser port of the reverb engine
 * (reverb-engine.js) and maps the sliders to its parameters.
 */
(function () {
  "use strict";

  // ---- controls: same ranges, defaults and knob curves as the plugin ---------
  // `centre` reproduces JUCE's setSkewForCentre: that value sits mid-slider.
  const CONTROLS = [
    { group: "SPACE",  id: "size",      label: "Size",      min: 0,    max: 100,   def: 60,    unit: "%" },
    { group: "SPACE",  id: "decay",     label: "Decay",     min: 0.2,  max: 20,    def: 2.5,   unit: "s",  centre: 2.5 },
    { group: "SPACE",  id: "predelay",  label: "Pre-Delay", min: 0,    max: 250,   def: 20,    unit: "ms", centre: 40 },
    { group: "SPACE",  id: "diffusion", label: "Diffusion", min: 0,    max: 100,   def: 75,    unit: "%" },
    { group: "TONE",   id: "damping",   label: "Damping",   min: 1000, max: 20000, def: 7000,  unit: "Hz", centre: 5000 },
    { group: "TONE",   id: "lowcut",    label: "Low Cut",   min: 20,   max: 1000,  def: 120,   unit: "Hz", centre: 150 },
    { group: "TONE",   id: "highcut",   label: "High Cut",  min: 1000, max: 20000, def: 12000, unit: "Hz", centre: 6000 },
    { group: "MOTION", id: "modrate",   label: "Rate",      min: 0.05, max: 5,     def: 0.6,   unit: "Hz", centre: 0.8 },
    { group: "MOTION", id: "moddepth",  label: "Depth",     min: 0,    max: 100,   def: 35,    unit: "%" },
    { group: "OUTPUT", id: "width",     label: "Width",     min: 0,    max: 200,   def: 100,   unit: "%" },
    { group: "OUTPUT", id: "mix",       label: "Mix",       min: 0,    max: 100,   def: 30,    unit: "%" }
  ];

  // Factory presets (Source/Presets.cpp): mix pre size decay damp diff rate depth lowcut hicut width
  const PRESETS = [
    ["Default",      30, 20, 60, 2.5, 7000, 75, 0.60, 35, 120, 12000, 100],
    ["Tight Room",   22, 4, 20, 0.6, 6000, 80, 0.40, 15, 180, 10000, 80],
    ["Vocal Plate",  28, 35, 45, 1.8, 11000, 90, 0.90, 30, 220, 14000, 110],
    ["Drum Chamber", 25, 8, 35, 1.1, 8000, 70, 0.50, 20, 150, 11000, 100],
    ["Concert Hall", 35, 28, 75, 3.2, 6500, 80, 0.50, 35, 90, 10000, 110],
    ["Cathedral",    45, 60, 95, 8.5, 4500, 85, 0.35, 45, 70, 8000, 125],
    ["Ambient Wash", 55, 80, 100, 14.0, 5000, 90, 0.25, 75, 150, 9000, 150],
    ["Bright Bloom", 60, 45, 85, 10.0, 16000, 95, 1.40, 85, 300, 18000, 170],
    ["Dark Space",   40, 30, 80, 6.0, 1800, 75, 0.30, 40, 60, 4000, 120]
  ];
  const PRESET_KEYS = ["mix", "predelay", "size", "decay", "damping", "diffusion", "modrate", "moddepth", "lowcut", "highcut", "width"];

  const SAMPLES = { drums: "samples/drums.wav", guitar: "samples/guitar.mp3", keys: "samples/keys.mp3", snare: "samples/snare.mp3" };

  // ---- value <-> slider position ----------------------------------------------
  const STEPS = 1000;
  function skewOf(c) { return c.centre === undefined ? 1 : Math.log(0.5) / Math.log((c.centre - c.min) / (c.max - c.min)); }
  function toValue(c, pos) { const p = pos / STEPS; return c.min + (c.max - c.min) * Math.exp(Math.log(Math.max(p, 1e-9)) / skewOf(c)); }
  function toPos(c, v) { const p = (v - c.min) / (c.max - c.min); return Math.round(STEPS * Math.pow(Math.max(p, 0), skewOf(c))); }

  function fmt(c, v) {
    if (c.unit === "%") return Math.round(v) + "%";
    if (c.unit === "ms") return Math.round(v) + " ms";
    if (c.unit === "s") return v < 1 ? Math.round(v * 1000) + " ms" : v.toFixed(v < 10 ? 2 : 1) + " s";
    if (v >= 1000) return (v / 1000).toFixed(v >= 10000 ? 1 : 2) + " kHz";
    if (v < 10) return v.toFixed(2) + " Hz";
    return Math.round(v) + " Hz";
  }

  // ---- state -----------------------------------------------------------------
  const values = {};
  CONTROLS.forEach(c => { values[c.id] = c.def; });
  let freeze = false, bypass = false;

  let ctx = null, node = null, engine = null, source = null, playing = false;
  let currentSample = "drums", userBuffer = null;
  const buffers = {};

  const $ = id => document.getElementById(id);
  const statusEl = $("pgStatus");
  const setStatus = t => { statusEl.textContent = t || ""; };

  function engineParams() {
    return {
      mix: values.mix / 100, preDelayMs: values.predelay, size: values.size / 100, decaySec: values.decay,
      dampingHz: values.damping, diffusion: values.diffusion / 100, modRateHz: values.modrate,
      modDepth: values.moddepth / 100, lowCutHz: values.lowcut, highCutHz: values.highcut,
      width: values.width / 100, freeze: freeze
    };
  }

  function sendParams() {
    const p = engineParams();
    if (node && node.port) node.port.postMessage({ params: p });
    if (engine) engine.setParameters(p);
    drawGraph();
  }

  function sendBypass() {
    if (node && node.port) node.port.postMessage({ bypass: bypass });
  }

  // ---- audio setup (on first Play, as browsers require a click first) ----------
  async function ensureAudio() {
    if (ctx) return;
    const AC = window.AudioContext || window.webkitAudioContext;
    ctx = new AC();
    try {
      // Preferred: run the engine on the audio thread (needs HTTPS).
      if (!ctx.audioWorklet) throw new Error("no worklet");
      await ctx.audioWorklet.addModule("reverb-engine.js");
      node = new AudioWorkletNode(ctx, "eighth-hall", {
        numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
        channelCount: 2, channelCountMode: "explicit", channelInterpretation: "speakers"
      });
    } catch (err) {
      // Fallback: run it on the page's main thread.
      engine = new window.FdnEngine(ctx.sampleRate);
      node = ctx.createScriptProcessor(2048, 2, 2);
      node.onaudioprocess = e => {
        const inL = e.inputBuffer.getChannelData(0), inR = e.inputBuffer.getChannelData(1);
        const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
        L.set(inL); R.set(inR);
        if (!bypass) engine.process(L, R);
      };
    }
    node.connect(ctx.destination);
    sendParams();
    sendBypass();
  }

  async function loadSample(name) {
    if (name === "user") return userBuffer;
    if (buffers[name]) return buffers[name];
    setStatus("Loading sound…");
    const res = await fetch(SAMPLES[name]);
    if (!res.ok) throw new Error("Could not load " + SAMPLES[name]);
    const data = await res.arrayBuffer();
    buffers[name] = await new Promise((ok, fail) => ctx.decodeAudioData(data, ok, fail));
    setStatus("");
    return buffers[name];
  }

  function stopSource() {
    if (source) { try { source.stop(); } catch (e) { /* already stopped */ } source.disconnect(); source = null; }
  }

  async function startSource() {
    const buf = await loadSample(currentSample);
    if (!buf) return;
    stopSource();
    source = ctx.createBufferSource();
    source.buffer = buf;
    source.loop = true;
    source.connect(node);
    source.start();
  }

  function setPlaying(on) {
    playing = on;
    $("pgIcon").innerHTML = on ? '<path fill="currentColor" d="M6 4h4v16H6zm8 0h4v16h-4z"/>'
                               : '<path fill="currentColor" d="M7 4.5v15l13-7.5z"/>';
    $("pgPlay").setAttribute("aria-label", on ? "Stop" : "Play");
  }

  $("pgPlay").addEventListener("click", async () => {
    try {
      await ensureAudio();
      if (ctx.state === "suspended") await ctx.resume();
      if (playing) { stopSource(); setPlaying(false); return; }
      await startSource();
      setPlaying(true);
    } catch (err) {
      setStatus(location.protocol === "file:"
        ? "Open this page from your website (or a local web server) - browsers block audio files opened straight from disk."
        : "Sorry, the audio couldn't start: " + err.message);
    }
  });

  // ---- sound picker ----------------------------------------------------------
  const sampleButtons = document.querySelectorAll("#playground [data-sample]");
  const uploadLabel = document.querySelector("#playground .upload");
  function selectSample(name) {
    currentSample = name;
    sampleButtons.forEach(b => b.setAttribute("aria-pressed", b.dataset.sample === name ? "true" : "false"));
    uploadLabel.classList.toggle("active", name === "user");
    if (playing) startSource().catch(err => setStatus(err.message));
  }
  sampleButtons.forEach(b => b.addEventListener("click", () => selectSample(b.dataset.sample)));

  $("pgFile").addEventListener("change", async e => {
    const file = e.target.files && e.target.files[0];
    if (!file) return;
    try {
      await ensureAudio();
      setStatus("Decoding " + file.name + "…");
      const data = await file.arrayBuffer();
      userBuffer = await new Promise((ok, fail) => ctx.decodeAudioData(data, ok, fail));
      $("pgFileLabel").textContent = file.name;
      setStatus("");
      selectSample("user");
    } catch (err) {
      setStatus("That file couldn't be read. Try a WAV or MP3.");
    }
  });

  // ---- toggles ---------------------------------------------------------------
  function toggle(btn, on) { btn.setAttribute("aria-pressed", on ? "true" : "false"); }
  $("pgBypass").addEventListener("click", () => { bypass = !bypass; toggle($("pgBypass"), bypass); sendBypass(); });
  $("pgFreeze").addEventListener("click", () => { freeze = !freeze; toggle($("pgFreeze"), freeze); sendParams(); });

  // ---- sliders ---------------------------------------------------------------
  const sliders = {};
  const groupsEl = $("pgGroups");
  const groupEls = {};
  CONTROLS.forEach(c => {
    if (!groupEls[c.group]) {
      const g = document.createElement("div");
      g.className = "pg-group";
      g.innerHTML = "<h3>" + c.group + "</h3>";
      groupsEl.appendChild(g);
      groupEls[c.group] = g;
    }
    const wrap = document.createElement("div");
    wrap.className = "ctl";
    const inputId = "pg-" + c.id;
    wrap.innerHTML = '<label for="' + inputId + '"><span>' + c.label.toUpperCase() + '</span><output id="' + inputId + '-out"></output></label>' +
                     '<input type="range" id="' + inputId + '" min="0" max="' + STEPS + '" step="1">';
    groupEls[c.group].appendChild(wrap);
    const input = wrap.querySelector("input"), out = wrap.querySelector("output");
    sliders[c.id] = { c: c, input: input, out: out };

    input.addEventListener("input", () => {
      values[c.id] = toValue(c, +input.value);
      refreshSlider(c.id);
      $("pgPreset").value = "";
      sendParams();
    });
    input.addEventListener("dblclick", () => { values[c.id] = c.def; refreshSlider(c.id, true); sendParams(); });
  });

  function refreshSlider(id, movePosition) {
    const s = sliders[id];
    if (movePosition) s.input.value = toPos(s.c, values[id]);
    s.out.textContent = fmt(s.c, values[id]);
    s.input.setAttribute("aria-valuetext", fmt(s.c, values[id]));
    s.input.style.setProperty("--fill", (100 * s.input.value / STEPS) + "%");
  }

  // ---- presets ---------------------------------------------------------------
  const presetSel = $("pgPreset");
  presetSel.innerHTML = '<option value="" disabled hidden>Custom</option>' +
    PRESETS.map((p, i) => '<option value="' + i + '">' + p[0] + "</option>").join("");
  presetSel.value = "0";
  presetSel.addEventListener("change", () => {
    const p = PRESETS[+presetSel.value];
    PRESET_KEYS.forEach((k, i) => { values[k] = p[i + 1]; refreshSlider(k, true); });
    freeze = false; toggle($("pgFreeze"), false);
    sendParams();
  });

  // ---- decay graph (same maths as the plugin's DecayDisplay) -------------------
  const canvas = $("pgGraph");
  function highDecay() {
    const fs = 48000, f = 8000;
    const a = 1 - Math.exp(-2 * Math.PI * values.damping / fs), w = 2 * Math.PI * f / fs;
    const mag = a / Math.sqrt(1 - 2 * (1 - a) * Math.cos(w) + (1 - a) * (1 - a));
    const lossDb = -20 * Math.log10(Math.max(1e-6, mag));
    const s = values.size / 100, trip = 0.0515 * (0.25 + 1.5 * s * s);
    return 60 / (60 / values.decay + lossDb / trip);
  }
  function drawGraph() {
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth || 600, cssH = 150;
    if (canvas.width !== Math.round(cssW * dpr)) { canvas.width = Math.round(cssW * dpr); canvas.height = Math.round(cssH * dpr); }
    const g = canvas.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, cssW, cssH);

    const padL = 34, padR = 12, padT = 10, padB = 22, w = cssW - padL - padR, h = cssH - padT - padB;
    const pre = values.predelay / 1000, hi = highDecay();
    const span = freeze ? 4 : Math.max(0.5, (pre + values.decay) * 1.15);
    const maxLabels = Math.max(3, Math.floor((cssW - 46) / 64));   // fewer labels on narrow screens
    const step = [0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10].find(st => span / st <= maxLabels) || 10;
    const tMax = Math.ceil(span / step) * step;
    const X = t => padL + w * Math.min(1, Math.max(0, t / tMax));
    const Y = db => padT + h * Math.min(1, Math.max(0, db / -72));

    g.font = "11px Inter, sans-serif"; g.fillStyle = "#8a91a3"; g.strokeStyle = "rgba(42,48,61,.8)"; g.lineWidth = 1;
    g.textAlign = "right"; g.textBaseline = "middle";
    [0, -20, -40, -60].forEach(db => {
      g.beginPath(); g.moveTo(padL, Y(db) + .5); g.lineTo(padL + w, Y(db) + .5); g.stroke();
      g.fillText(String(db), padL - 6, Y(db));
    });
    g.textAlign = "center"; g.textBaseline = "top";
    for (let t = 0; t <= tMax + 1e-6; t += step) {
      g.fillText(t < 1 ? Math.round(t * 1000) + " ms" : t.toFixed(t % 1 ? 2 : 0) + " s",
                 Math.min(Math.max(X(t), padL + 14), padL + w - 14), padT + h + 6);
    }

    const s = values.size / 100;
    const build = 0.01 + 0.05 * (0.25 + 1.5 * s * s) * (0.4 + 0.6 * values.diffusion / 100);
    function env(rt) {
      g.beginPath(); g.moveTo(X(pre), Y(-72)); g.lineTo(X(pre + build), Y(0));
      if (freeze) g.lineTo(X(tMax), Y(0)); else g.lineTo(X(pre + build + rt * 1.2), Y(-72));
    }
    if (!freeze) {
      g.setLineDash([5, 4]); g.strokeStyle = "#f0b46a"; g.lineWidth = 1.6; env(hi); g.stroke(); g.setLineDash([]);
    }
    env(values.decay);
    const grad = g.createLinearGradient(0, padT, 0, padT + h);
    grad.addColorStop(0, "rgba(92,200,220,.35)"); grad.addColorStop(1, "rgba(92,200,220,.02)");
    g.lineTo(X(freeze ? tMax : pre), Y(-72)); g.closePath(); g.fillStyle = grad; g.fill();
    env(values.decay); g.strokeStyle = "#5cc8dc"; g.lineWidth = 2; g.stroke();

    $("pgRt").textContent = freeze ? "Frozen: tail held indefinitely" : "Mids RT60 " + fmt({ unit: "s" }, values.decay);
    $("pgHighs").textContent = freeze ? "" : "- - Highs (8 kHz) " + fmt({ unit: "s" }, hi);
  }

  CONTROLS.forEach(c => refreshSlider(c.id, true));
  drawGraph();
  window.addEventListener("resize", drawGraph);
})();
