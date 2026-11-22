// Web: the hand-gesture half of Inputs (onHands, track, holdGlass).
//
// Reads each hand the way you would expect a hand to be read:
//   a raised finger is a painter (point for one light, open the hand for five),
//   a curled finger rests, so a fist lifts every pen,
//   a pinch holds the glass, and the fingers not pinching can go on painting.
//   One pinched hand turns it like a dial and dives by moving toward the camera
//   (its apparent size grows); two pinched hands turn and stretch it like a wheel.
// And the hand acts on the picture as a hand would:
//   a quick pinch and release is a tap (the click), and so is a pointing finger
//   poked toward the screen; a pinched hand punched toward the camera bursts through,
//   pulled back sharply it draws the light in; an open hand on the move stirs the
//   picture like a hand through water; two pinched hands hold a bridge of light that
//   charges as it stretches and snaps when both let go.
// The view is mirrored, and the middle 80% of the camera reaches the whole screen.
#include "engine_impl.h"

// [tip, lower joint] per finger, thumb first
static const int FINGERS[5][2] = {{4, 3}, {8, 6}, {12, 10}, {16, 14}, {20, 18}};

static inline double reach(double u) { return 0.5 + (u - 0.5) * 1.25; }

void Hands::track(H& h, const HandObs& o) {
  const double va = this->va;
  auto D = [&](int a, int b) { return std::hypot((o.x[a] - o.x[b]) * va, (double)o.y[a] - o.y[b]); };
  const double size = std::max(D(0, 9), 1e-3);
  const double pr = D(4, 8) / size;
  h.pinch = h.pinch ? pr < 0.5 : pr < 0.3;
  bool open = true;
  for (int f = 1; f < 5; f++) {
    const double r = D(FINGERS[f][0], 0) / std::max(D(FINGERS[f][1], 0), 1e-3);
    h.ext[f] = h.ext[f] ? r > 1.08 : r > 1.2;
    open = open && h.ext[f];
  }
  // A thumb wanders when you point, so it only paints as part of an open hand.
  const double tr = D(4, 17) / std::max(D(3, 17), 1e-3);
  h.ext[0] = open && (h.ext[0] ? tr > 1.02 : tr > 1.1);
  h.seen = true;
  h.scale = size;
  h.gx = reach(1 - (o.x[4] + o.x[8]) / 2.0);
  h.gy = reach((o.y[4] + o.y[8]) / 2.0);
  h.ang = std::atan2((double)o.y[9] - o.y[0], -((double)o.x[9] - o.x[0]) * va);
  for (int k = 0; k < 21; k++) { h.lx[k] = (float)reach(1 - o.x[k]); h.ly[k] = (float)reach(o.y[k]); }
}

void Hands::gestures(H& h, int slot, bool wasPinch, double now) {
  // How fast the hand grows (toward the camera) or shrinks, smoothed: log size per second.
  const double ls = std::log(std::max(h.scale, 1e-4));
  if (h.sAt > 0) {
    const double secs = std::clamp((now - h.sAt) / 1000, 0.01, 0.2);
    h.rate += ((ls - h.ls) / secs - h.rate) * 0.5;
  } else h.rate = 0;
  h.ls = ls; h.sAt = now;

  // Pinch and let go quickly, without moving: a tap where the pinch was.
  if (h.pinch && !wasPinch) { h.pinchT = now; h.pinchU = h.gx; h.pinchV = h.gy; h.pinchMoved = 0; }
  if (h.pinch) h.pinchMoved = std::max(h.pinchMoved, std::hypot(h.gx - h.pinchU, h.gy - h.pinchV));
  if (!h.pinch && wasPinch && now - h.pinchT < 300 && h.pinchMoved < 0.04 && grab.mode < 2) pens->airTap(h.pinchU, h.pinchV, 1);

  // A pointing finger poked toward the screen: the hand grows fast while it points.
  const bool pointing = h.ext[1] && !h.ext[2] && !h.ext[3] && !h.ext[4] && !h.pinch;
  if (pointing && h.rate > 1.6 && now > h.pokeCool) { pens->airTap(h.lx[8], h.ly[8], 1.6); h.pokeCool = now + 450; }

  // A pinched hand punched toward the camera bursts through; pulled back hard, implodes.
  if (h.pinch && grab.mode == 1 && std::fabs(h.rate) > 2.4 && now > h.punchCool) {
    const bool in = h.rate > 0;
    pens->airTap(h.gx, h.gy, in ? 3.0 : -2.2);
    motion->dive(in ? 0.6 : -0.6);
    h.punchCool = now + 600;
  }

  // An open hand on the move stirs the picture.
  const int open = h.ext[1] + h.ext[2] + h.ext[3] + h.ext[4];
  const double pu = (h.lx[0] + h.lx[5] + h.lx[9] + h.lx[13] + h.lx[17]) / 5, pv = (h.ly[0] + h.ly[5] + h.ly[9] + h.ly[13] + h.ly[17]) / 5;
  if (open >= 3 && !h.pinch && h.palmAt > 0) {
    const double secs = std::clamp((now - h.palmAt) / 1000, 0.01, 0.2);
    const double vu = (pu - h.palmU) / secs, vv = (pv - h.palmV) / secs;
    pens->setStir(slot, std::hypot(vu, vv) > 0.08, pu, pv, vu, vv);
  } else pens->setStir(slot, false, 0, 0, 0, 0);
  h.palmU = pu; h.palmV = pv; h.palmAt = now;
}

void Hands::holdGlass(double now) {
  const H &a = hs[0], &b = hs[1];
  const bool pa = a.seen && a.pinch, pb = b.seen && b.pinch;
  const int mode = pa && pb ? 2 : pa || pb ? 1 : 0;
  double ang = 0, dist = 1;
  if (mode == 2) {
    const double dx = (b.gx - a.gx) * viewAspect, dy = b.gy - a.gy;
    ang = std::atan2(dy, dx); dist = std::max(std::hypot(dx, dy), 1e-3);
  } else if (mode == 1) {
    const H& h = pa ? a : b;
    ang = h.ang; dist = h.scale;
  }
  if (bridge.pending && (mode == 0 || now - bridge.at > 350)) {
    if (mode == 0) { pens->airTap(bridge.mu, bridge.mv, bridge.power); palm.quietUntil = now + 2000; }   // both let go: it snaps (and open hands right after don't step on)
    bridge.pending = false;
  }
  if (mode == 2) {
    if (grab.mode != 2) { bridge.startDist = dist; bridge.held = 0; }
    bridge.held += std::max(0.0, (now - grab.t) / 1000);
    // Charged by a deliberate stretch (or squeeze), and slowly by holding on.
    bridge.charge = std::min(1.5, 1.6 * std::fabs(std::log(dist / bridge.startDist)) + std::min(0.4, bridge.held * 0.1));
    bridge.mu = (a.gx + b.gx) / 2; bridge.mv = (a.gy + b.gy) / 2;
  } else if (grab.mode == 2) {
    if (bridge.charge > 0.35) { bridge.pending = true; bridge.at = now; bridge.power = 2 + 3 * bridge.charge; }
    bridge.charge = 0;
  }
  if (mode != grab.mode) {
    if (mode && !grab.mode) { motion->spinVel = 0; events->push({EV_HAPTIC, 0, 0, 0, HAPTIC_TICK, 0}); }
    motion->held = mode > 0;
    grab.mode = mode; grab.ang = ang; grab.dist = dist; grab.t = now;
    grab.acc = grab.sm = grab.lacc = grab.lsm = 0; motion->drop(Motion::cA, Motion::cD);
    return;
  }
  if (!mode) return;
  double da = ang - grab.ang;
  if (da > PI) da -= TAU; else if (da < -PI) da += TAU;
  const double secs = std::max(0.01, (now - grab.t) / 1000);
  if (std::fabs(da) < 0.6) {          // a larger jump is the tracker swapping hands, not a turn
    // Smooth what the camera measured, then let only deliberate movement through.
    grab.acc += da; grab.sm += (grab.acc - grab.sm) * 0.55;
    const double turn = motion->take(Motion::cA, grab.sm, mode == 2 ? 0.03 : 0.045) * 0.85;
    if (turn) {
      motion->feed(-turn);
      motion->spinVel += (clampd(-turn / secs, -2.5, 2.5) - motion->spinVel) * 0.2;
    }
    const double r = std::log(dist / grab.dist);
    if (std::fabs(r) < 0.5) {
      grab.lacc += r; grab.lsm += (grab.lacc - grab.lsm) * 0.5;
      const double push = motion->take(Motion::cD, grab.lsm, 0.035);
      if (push) motion->dive(push * (mode == 2 ? 7 : 9));
    }
  }
  grab.ang = ang; grab.dist = dist; grab.t = now;
}

void Hands::onHands(const HandObs* list, int n, double now) {
  hs[0].seen = hs[1].seen = false;
  for (int i = 0; i < n && i < 2; i++) {
    // Left and right keep their own slot, so fingers keep their colors when hands cross.
    // `right` is MediaPipe's handedness label, as the web's side === 'Right'.
    int slot = list[i].right ? 1 : 0;
    if (hs[slot].seen) slot = hs[0].seen ? 1 : 0;
    if (hs[slot].seen) continue;
    H& h = hs[slot];
    const bool wasPinch = h.pinch;   // cleared when the hand was last unseen
    track(h, list[i]);
    gestures(h, slot, wasPinch, now);
    for (int f = 0; f < 5; f++) {
      const int t = FINGERS[f][0];
      // A pinching hand holds the glass with thumb and forefinger; its other three
      // fingers are still free to paint, so one hand can turn and draw at once.
      const bool paints = h.ext[f] && (!h.pinch || f >= 2);
      tipOn[slot * 5 + f] = paints;
      if (paints) { tips[slot * 5 + f][0] = (float)reach(1 - list[i].x[t]); tips[slot * 5 + f][1] = (float)reach(list[i].y[t]); }
    }
  }
  for (int sl = 0; sl < 2; sl++) {
    H& h = hs[sl];
    if (!h.seen) {
      h.pinch = false; for (bool& e : h.ext) e = false; for (int f = 0; f < 5; f++) tipOn[sl * 5 + f] = false;
      h.sAt = h.palmAt = 0; h.rate = 0;
      pens->setStir(sl, false, 0, 0, 0, 0);
    }
  }
  holdGlass(now);
  pens->setHands(tipOn, tips);
  if (!enabled) return;

  const H &a = hs[0], &b = hs[1];
  if (a.seen || b.seen) seenAt = now;

  // Both palms shown and held still: step to the next chamber, hands-free.
  auto flat = [](const H& h) { return h.seen && !h.pinch && h.ext[1] && h.ext[2] && h.ext[3] && h.ext[4]; };
  if (!flat(a) || !flat(b) || now < palm.quietUntil) { palm.since = 0; palm.fired = false; }
  else if (!palm.since || std::hypot(a.gx - palm.ax, a.gy - palm.ay) + std::hypot(b.gx - palm.bx, b.gy - palm.by) > 0.06) {
    palm.since = now; palm.ax = a.gx; palm.ay = a.gy; palm.bx = b.gx; palm.by = b.gy;
  } else if (!palm.fired && now - palm.since > 900) {
    palm.fired = true;
    events->push({EV_NEXT_CHAMBER, 0, 0, 0, 0, 0});
    events->push({EV_HAPTIC, 0, 0, 0, HAPTIC_CONFIRM, 0});
  }
}
