// Injected only into the test browser. Does not change device firmware or settings.
(() => {
  const state = window.__deviceTest = {received: {}, draws: {}, progress: {}};
  const originalFetch = window.fetch.bind(window);
  window.fetch = async (...args) => {
    const response = await originalFetch(...args);
    const path = new URL(String(args[0]), location.href).pathname;
    const stream = path === '/api/readings' ? 'readings' : path === '/api/motion' ? 'motion' : null;
    if (stream) {
      const originalJson = response.json.bind(response);
      response.json = async () => {
        const data = await originalJson();
        state.received[stream] = {at: performance.now(), data, sequence: stream === 'motion' ? data.motion?.sequence : data.sequence};
        return data;
      };
    }
    return response;
  };
  const stroke = CanvasRenderingContext2D.prototype.stroke;
  CanvasRenderingContext2D.prototype.stroke = function (...args) {
    const value = stroke.apply(this, args);
    try {
      state.draws[this.canvas.id] = {at: performance.now(), env: lastSequence, motion: lastMotionSequence};
    } catch (_) { /* A canvas drawn before application initialization has no sequence yet. */ }
    return value;
  };
  window.__deviceTestSnapshot = () => {
    const now = performance.now(), output = {at: now};
    for (const stream of ['readings', 'motion']) {
      const received = state.received[stream];
      if (!received) continue;
      const seq = stream === 'motion' ? lastMotionSequence : lastSequence;
      let progress = state.progress[stream];
      if (!progress || progress.sequence !== seq) progress = state.progress[stream] = {sequence: seq, at: now};
      const draw = state.draws[stream === 'motion' ? 'motionChart' : 'tempChart'];
      const drawnSequence = draw?.[stream === 'motion' ? 'motion' : 'env'];
      const pending = seq < received.sequence || !draw || drawnSequence < received.sequence;
      let mismatch = false;
      const data = received.data;
      if (seq === received.sequence) {
        if (stream === 'readings') {
          const valid = data.sensor_ok && Number.isFinite(data.temperature_c);
          mismatch = document.getElementById('temperature')?.textContent !== (valid ? data.temperature_c.toFixed(2) : '—');
          const m = data.motion;
          const tempOK = m?.temperature_ok && Number.isFinite(m.temperature_c);
          mismatch ||= document.getElementById('mpuTemperature')?.textContent !== (tempOK ? m.temperature_c.toFixed(2) : '—');
        } else {
          const m = data.motion;
          const expected = m?.ok && m.acceleration_m_s2?.every(Number.isFinite) ?
            m.acceleration_m_s2.map((v, i) => ['X', 'Y', 'Z'][i] + ': ' + v.toFixed(3)).join(' · ') : '—';
          mismatch = !document.getElementById('motionValues')?.textContent.startsWith(expected);
        }
      }
      output[stream] = {sequence: seq, received_sequence: received.sequence,
        pending_ms: pending ? now-received.at : 0, advance_age_ms: now-progress.at,
        draw_age_ms: draw ? now-draw.at : now,
        render_delay_ms: !pending && draw ? Math.max(0, draw.at-received.at) : null,
        draw_at: draw?.at, value_mismatch: mismatch};
    }
    output.environment_ranges = history.map(p => ({first: p.firstSequence ?? p.sequence, last: p.sequence, resolution: p.resolution}));
    output.motion_ranges = motionHistory.map(p => ({first: p.sequence, last: p.sequence}));
    output.environment_points = history.length;
    output.motion_points = motionHistory.length;
    output.environment_time_span_ms = history.length > 1 ? history.at(-1).at-history[0].at : 0;
    return output;
  };
})();
