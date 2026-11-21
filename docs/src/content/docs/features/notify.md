---
title: Notifications
description: A full-screen attention overlay any script can fire over HTTP, which takes over the panel for a few seconds and then puts back whatever was showing.
---

Everything else on this device is something it fetches on a timer. Notifications are the other direction: you push, and the screen reacts immediately. A single HTTP request takes over the whole panel with an animation and a message, holds it for as long as you asked, and then puts back whatever was on screen before.

It was built for Claude Code sessions, waving when a session needs an answer and celebrating when a task finishes, but nothing about it is specific to that. Anything that can make an HTTP request can fire one: a backup script, a doorbell, a long compile.

## Firing one

```bash
curl -X POST http://smalltv.local/api/notify \
  -H 'Content-Type: application/json' \
  -d '{"type":"alert","label":"/var is at 98% on nas-01"}'
```

Every field is optional. The smallest useful request is `{"label":"..."}`, which gets you the `info` preset.

| Field | Meaning |
|---|---|
| `type` | Which preset to start from: `info`, `warning`, `alert`, `done`, `waiting`. Defaults to `info`. An unknown type is rejected. |
| `label` | The message. Scrolls if it is wider than the panel. |
| `title` | A word above the label, up to 20 characters. There is none unless you ask for one (see below). |
| `anim` | Animation name, overriding the preset's: `info`, `warn`, `bang`, `pulse`, `jumping`, `waving`. An unknown name is rejected. |
| `color` | Accent colour for both text lines, overriding the preset's. `#rrggbb`, or one of `white` `black` `red` `green` `blue` `yellow` `cyan` `magenta` `orange` `gray`. |
| `ttl` | How long to hold the screen, in seconds. Clamped to 2 to 120. Defaults to the preset's. |
| `priority` | `0` to `3`, overriding the preset's. Decides queue order and what pre-empts what. |

A request that is understood answers `{"ok":true,"queued":N}`, where `N` is how many other overlays are waiting behind this one. A request naming a type, animation or colour that does not exist answers HTTP 400. One that arrived at a full queue with nothing less important in it to displace answers **HTTP 429** instead, so a script can tell "retry in a moment" apart from "you sent nonsense" and back off rather than give up. A request with no body at all, or one whose body is not valid JSON, answers HTTP 400 with a plain-text reason rather than JSON, so a script checking the response should look at the status code and not assume it can parse what comes back.

## Types are defaults, not rules

A `type` sets five things at once: the accent colour, the animation, the hold time, the priority, and a fallback word.

| Type | Colour | Animation | `ttl` | `priority` | Fallback word |
|---|---|---|---|---|---|
| `info` | blue | `info` | 20 | 0 | INFO |
| `warning` | yellow | `warn` | 30 | 1 | WARNING |
| `alert` | red | `bang` | 45 | 2 | ALERT |
| `done` | cream | `jumping` | 20 | 0 | TASK DONE |
| `waiting` | cream | `waving` | 20 | 1 | NEEDS YOU |

The fallback word is the one thing that is not simply a default. It appears **only when there is no label**, because a line reading "ALERT" above your message says nothing the colour and the animation have not already said, and it costs a line the message could use. Send a label and you get the label; send nothing and the word keeps the screen from being blank. Setting `title` explicitly overrides both cases. That is how you get a header when you actually want one.

None of the rest is binding either. Every field is one you can override independently in the same request: the preset decides only what you get when you say nothing. An information notice that arrived by mail, in white, holding for a minute, is one request:

```bash
curl -X POST http://smalltv.local/api/notify \
  -H 'Content-Type: application/json' \
  -d '{"type":"info","title":"MAIL","color":"white","ttl":60,
       "label":"3 new messages"}'
```

The two types named after the endpoint's original states, `done` and `waiting`, exist so that a script written before types did keeps working. The old field name is still read too: `{"state":"done"}` means the same as `{"type":"done"}`, and `anim` accepts `done` and `waiting` as names for the `jumping` and `waving` animations.

## Several at once

Overlays queue rather than overwrite. When one expires the next takes the panel, so a burst of events is shown in turn instead of leaving only the last one.

The queue holds 4 on the ESP32 boards and 2 on the ESP8266, on top of the one on screen. It is ordered by priority first and arrival second, so equal-priority events are shown in the order they were fired and a more important one goes to the front of the line. An arrival that is **strictly** more important than what is on screen does not wait at all: it takes the panel immediately, and the overlay it interrupted is dropped rather than requeued, because it has already had its time on screen.

When the queue is full, the arrival displaces the least important thing waiting. If everything already queued outranks it, the arrival is the one refused, with HTTP 429.

**A repeat refreshes rather than queues.** An arrival naming the same `type` and the same `label` as something already on screen restarts that overlay in place, with whatever the new request asked for; the same match against something already queued replaces it where it sits. This is what keeps a heartbeat working: a script firing `waiting` every ten seconds with `ttl:20` keeps one overlay alive, as it always did, instead of building a queue that holds the panel for the sum of those hold times and then replays events the session has long moved past.

This is what priority is for: an `alert` fired while three `info` notices are queued behind a fourth is on the panel immediately, and the `info` notices resume afterwards.

## Long labels

A label that fits on one line is centred and still. A longer one scrolls right to left, continuously, for as long as the overlay is up, after a short pause on the first frame so a label that only just overflows is readable before it moves.

The buffer is 96 characters on the ESP32 boards and 48 on the ESP8266; past that the label is cut. Anything outside plain printable ASCII is dropped, so accents and emoji disappear rather than drawing as blanks.

It scrolls at 100 px/s, which is 120 ms per character. A 90-character label is 1108 px including the gap before it repeats, so one full pass takes about 11 seconds: at the default `ttl` of 20 it goes round just under twice. Anything much longer than that, or a shorter `ttl`, is worth pairing with a raised `ttl`, otherwise the end of the message may never reach the screen.

## What happens to the screen underneath

The overlay is not a mode. It cannot be selected in the Display tab and it never joins the carousel rotation; it simply pre-empts whatever is running, then hands back.

Handing back is careful about the carousel. The time the overlay spent on screen is credited back to the rotation timer, so if the ticker had four seconds left when the alert arrived, it still has four seconds left afterwards, on the same symbol. That credit spans the whole run rather than the last request in it, so a queue that chained four overlays gives back all four. The underlying feature repaints from what it already had rather than re-fetching, so an alert costs no extra network traffic.

Nothing about a notification is saved. There is no history, and a reboot leaves no trace of one, a queue included.

## Worth knowing

- The feature underneath does not fetch while the overlay is up, and a full queue can hold the panel considerably longer than one `ttl`. Five queued alerts at the 120-second maximum is ten minutes without a refresh.
- Two states swallow the alert while still answering `{"ok":true}`: a device in SETUP MODE, which keeps the hotspot screen up, and one sitting on the crash screen after a fault. Neither draws the overlay, so a script cannot read a success here as "it appeared".
- Night mode still applies. An alert that arrives with the night brightness at 0 animates behind a backlight that is off, which means it is invisible until morning. That is consistent with every other mode, but it does mean notifications are not a way to be woken up.
- The endpoint needs the web UI password when you have set one, so a script firing alerts has to send those credentials too.

## Adding your own type or animation

Both tables are meant to be edited.

A type is one row in `src/features/notify/NotifyTypes.h`: name, word, colour, animation, hold time, priority. Nothing else in the firmware indexes into that table.

An animation is one row in `src/features/notify/NotifyAnims.cpp`, and there are two kinds.

A **procedural** one is a function returning the colour of a single pixel, given its position, the frame number and the accent colour. `info`, `warn`, `bang` and `pulse` are these, which is why they follow whatever colour the request asks for and cost no flash beyond their own code. Write it with integer arithmetic, because it is called once per pixel per frame and the ESP8266 has no FPU.

It works per-pixel rather than by drawing shapes for a reason worth knowing before you add one: the panel has no framebuffer. A routine that clears its box and then draws into it leaves that box black on screen for the milliseconds the drawing takes, every frame, which reads as heavy flicker rather than as animation. Returning one pixel at a time lets the renderer write each pixel exactly once per frame, in place.

The same trap has a second form that no rendering strategy will save you from: **animating a large filled area at all**. Change its colour and it reads as a fault rather than as an animation: a gentler ramp between shades does not rescue it, because amplitude was never the problem. Move it instead and you trade the flicker for tearing: translating a 168-pixel shape while the panel scans out and the firmware writes row by row puts the new position at the top of the screen and the old one below it.

So the rule is: **keep the body still and constant, and animate a thin outline.** All four procedural animations follow it: a fixed shape with a ring or outline emanating from its edge and fading out. What separates them is tempo, and that turns out to be enough: `warn` emanates over about 880 ms and reads as "look at this", `bang` over about 320 ms and reads as "act now". Both arrived at that shape only after a strobing version and a tearing one.

A **pixel** one is a frame set from `src/features/notify/notify_frames.h`; `jumping` and `waving` are these, taken from Clawdmeter's own splash set. That header is generated by `tools/extract_notify.py` and is never hand-edited: the registry wraps it rather than growing it, so regenerating it does not disturb anything you added.
